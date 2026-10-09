#include "minic/ir.hpp"
#include "minic/constant_pool.hpp"
#include "minic/diagnostic.hpp"
#include "minic/semantic.hpp"
#include "internal.hpp"

#include <utility>
#include <algorithm>
#include <unordered_map>

namespace minic {
namespace {

class Generator {
    const SymbolTableData& symbols_; // 本次语义分析的符号信息。
    IRGenerationOptions options_; // 本次是否启用常量折叠。
    DiagnosticEngine diagnostics_{Phase::IR};
    ConstantPool constants_; // 所有函数共用的常量池。
    IRProgram program_;
    std::vector<Quadruple>* quads_ = &program_.global_initializers;
    std::vector<SourceRange>* locations_ = &program_.global_locations;
    std::vector<TemporaryEntry>* temporaries_ = &program_.global_temporaries;
    std::vector<std::pair<std::string, std::string>> loops_; // 循环结束和继续标号。
    std::size_t next_label_ = 0;
    std::size_t depth_ = 0;
    std::vector<std::unordered_map<const ASTNode*, std::string>> switches_; // case 节点对应的标号。
    SourceRange current_range_; // 生成失败时的源码位置。

    const ASTNode& child(const ASTNode& node, std::size_t index) const {
        if (index >= node.children.size() || !node.children[index]) throw std::runtime_error("语法树孩子缺失");
        return *node.children[index];
    }

    void count(const ASTNode& node, std::size_t low, std::size_t high) const {
        if (node.children.size() < low || node.children.size() > high) throw std::runtime_error("语法树孩子数量不正确");
        for (std::size_t i = 0; i < node.children.size(); ++i) child(node, i);
    }

    const SymbolEntry& binding(const ASTNode& node) const {
        if (node.symbol_id >= symbols_.symbols.size()) throw std::runtime_error("语法树缺少有效符号编号");
        const auto& entry = symbols_.symbols[node.symbol_id];
        if (entry.id != node.symbol_id || entry.name != node.name || !same_type(entry.type, node.type))
            throw std::runtime_error("语法树与符号表不属于同一次成功的语义分析");
        return entry;
    }

    void emit(const std::string& op, const std::string& first, const std::string& second,
              const std::string& result, const SourceRange& range) {
        quads_->push_back({op, first, second, result});
        locations_->push_back(range);
    }

    std::string temporary(TypePtr type) {
        if (!type || type->kind == TypeKind::Error) throw std::runtime_error("临时量类型尚未检查");
        const auto name = "%t" + std::to_string(temporaries_->size());
        temporaries_->push_back({name, std::move(type)});
        return name;
    }

    std::string label() { return "L" + std::to_string(next_label_++); }
    void mark(const std::string& value, const SourceRange& range) { emit("label", "-", "-", value, range); }

    std::string integer(std::int64_t value) {
        return constant_operand(constants_.intern(detail::type(TypeKind::Int), value, std::to_string(value)));
    }

    std::string snapshot(const std::string& source, const TypePtr& type, const SourceRange& range) {
        const auto value = temporary(type);
        emit("=", source, "-", value, range);
        return value;
    }

    // 左值求地址，赋值不提前读取尚未初始化的目标。
    std::string address(const ASTNode& node, std::size_t depth = 0) {
        if (depth > 128 || !node.type) throw std::runtime_error("地址表达式类型无效或过深");
        const auto result = temporary(detail::pointer(node.type));
        if (node.kind == NodeType::Identifier) {
            const auto& entry = binding(node); emit(entry.kind == SymbolKind::Function ? "faddr" : "addr", detail::symbol_name(node.symbol_id), "-", result, node.range);
        } else if (node.kind == NodeType::UnaryOp && node.name == "*") {
            count(node, 1, 1);
            emit("=", expression(child(node, 0)), "-", result, node.range);
        } else if (node.kind == NodeType::ArrayAccess) {
            count(node, 2, 2);
            const bool pointer = child(node, 0).type->kind == TypeKind::Pointer;
            const auto base = pointer ? snapshot(expression(child(node, 0)), child(node, 0).type, node.range) : address(child(node, 0), depth + 1);
            const auto index = expression(child(node, 1));
            emit(pointer ? "ptradd" : "indexaddr", base, index, result, node.range);
        } else if (node.kind == NodeType::MemberAccess) {
            count(node, 1, 1);
            if (node.record_id >= symbols_.records.size() || !node.member_index ||
                *node.member_index >= symbols_.records[node.record_id].members.size()) throw std::runtime_error("成员绑定无效");
            const auto& member = symbols_.records[node.record_id].members[*node.member_index];
            if (member.name != node.name || !member.offset) throw std::runtime_error("成员名字或布局无效");
            const auto base = address(child(node, 0), depth + 1);
            emit("memberaddr", base, std::to_string(*member.offset), result, node.range);
        } else if (detail::aggregate(node.type) && node.category == ValueCategory::RValue) {
            const auto value = snapshot(expression(node), node.type, node.range);
            emit("tempaddr", value, "-", result, node.range);
        } else throw std::runtime_error("此表达式不能求对象地址");
        return result;
    }

    void initialize(const std::string& destination, const TypePtr& target, const ASTNode& init) {
        if (init.kind == NodeType::InitList) {
            for (std::size_t i = 0; i < init.children.size(); ++i) {
                if (detail::numeric(target) || target->kind == TypeKind::Pointer) { initialize(destination, target, child(init, i)); continue; }
                TypePtr element;
                std::string selected;
                if (target->kind == TypeKind::Array) {
                    element = target->base;
                    selected = temporary(detail::pointer(element));
                    emit("indexaddr", destination, integer(static_cast<std::int64_t>(i)), selected, init.range);
                } else {
                    const auto& member = symbols_.records.at(target->record_id).members.at(i);
                    element = member.type;
                    selected = temporary(detail::pointer(element));
                    emit("memberaddr", destination, std::to_string(*member.offset), selected, init.range);
                }
                initialize(selected, element, child(init, i));
            }
        } else if (target->kind == TypeKind::Array && init.kind == NodeType::StringLiteral) {
            const auto& text = std::get<std::string>(init.value);
            for (std::size_t i = 0; i < text.size(); ++i) {
                const auto selected = temporary(detail::pointer(target->base));
                emit("indexaddr", destination, integer(static_cast<std::int64_t>(i)), selected, init.range);
                const auto byte = static_cast<unsigned char>(text[i]);
                const auto character = byte <= 127 ? static_cast<std::int64_t>(byte) : static_cast<std::int64_t>(byte) - 256;
                const auto byte_value = constant_operand(constants_.intern(detail::type(TypeKind::Char), character, std::to_string(character), init.range));
                emit("store", byte_value, "-", selected, init.range);
            }
        } else emit("store", expression(init), "-", destination, init.range);
    }

    std::string expression(const ASTNode& node) {
        current_range_ = node.range;
        if (++depth_ > 512) throw std::runtime_error("语法树嵌套过深");
        if (!node.type || node.type->kind == TypeKind::Unknown || node.type->kind == TypeKind::Error ||
            node.scope_id >= symbols_.scopes.size()) throw std::runtime_error("表达式尚未通过语义检查");
        std::string result;
        // 仅折叠无副作用且不会溢出/除零的数值常量表达式。
        if (options_.constant_folding && (node.kind == NodeType::BinaryOp || node.kind == NodeType::UnaryOp ||
            node.kind == NodeType::ImplicitCast || node.kind == NodeType::Cast)) {
            if (const auto value = detail::folded_value(node)) {
                --depth_;
                return constant_operand(constants_.intern(node.type, *value, "<折叠常量>", node.range));
            }
        }
        switch (node.kind) {
        case NodeType::ArrayAccess: case NodeType::MemberAccess:
            result = temporary(node.type);
            emit("load", address(node), "-", result, node.range);
            break;
        case NodeType::Conditional: {
            count(node, 3, 3);
            const auto no = label(), end = label();
            result = temporary(node.type);
            emit("jz", expression(child(node, 0)), "-", no, node.range);
            emit("=", expression(child(node, 1)), "-", result, node.range);
            emit("jmp", "-", "-", end, node.range);
            mark(no, node.range);
            emit("=", expression(child(node, 2)), "-", result, node.range);
            mark(end, node.range);
            break;
        }
        case NodeType::Identifier:
            count(node, 0, 0);
            binding(node);
            if (node.type->kind == TypeKind::Function) result = address(node);
            else result = detail::symbol_name(node.symbol_id);
            break;
        case NodeType::IntLiteral: case NodeType::FloatLiteral:
        case NodeType::CharLiteral: case NodeType::StringLiteral:
            count(node, 0, 0);
            result = constant_operand(constants_.intern(node.type, node.value, node.name, node.range));
            break;
        case NodeType::ImplicitCast: case NodeType::Cast: {
            count(node, 1, 1);
            if (child(node, 0).type->kind == TypeKind::Function && node.type->kind == TypeKind::Pointer) {
                result = expression(child(node, 0)); break;
            }
            if (child(node, 0).type->kind == TypeKind::Array && child(node, 0).kind != NodeType::StringLiteral && node.type->kind == TypeKind::Pointer) {
                result = temporary(node.type); emit("decay", address(child(node, 0)), "-", result, node.range); break;
            }
            const auto source = expression(child(node, 0));
            result = temporary(node.type);
            const bool i2f = node.type->kind == TypeKind::Float && child(node, 0).type->kind == TypeKind::Int;
            emit(i2f ? "cvt_i2f" : "cvt", source, "-", result, node.range);
            break;
        }
        case NodeType::BinaryOp: {
            count(node, 2, 2);
            const auto left = expression(child(node, 0));
            if (node.name == ",") { result = expression(child(node, 1)); break; }
            result = temporary(node.type);
            if (node.name == "&&" || node.name == "||") {
                const auto branch = label();
                const auto end = label();
                const auto jump = node.name == "&&" ? "jz" : "jnz";
                emit(jump, left, "-", branch, node.range);
                const auto right = expression(child(node, 1));
                emit(jump, right, "-", branch, node.range);
                emit("=", integer(node.name == "&&" ? 1 : 0), "-", result, node.range);
                emit("jmp", "-", "-", end, node.range);
                mark(branch, node.range);
                emit("=", integer(node.name == "&&" ? 0 : 1), "-", result, node.range);
                mark(end, node.range);
            } else {
                const auto saved = snapshot(left, child(node, 0).type, node.range);
                const auto right = expression(child(node, 1));
                if ((node.name == "+" || node.name == "-") && node.type->kind == TypeKind::Pointer) {
                    const bool first_pointer = child(node, 0).type->kind == TypeKind::Pointer;
                    emit(node.name == "+" ? "ptradd" : "ptrsub", first_pointer ? saved : right, first_pointer ? right : saved, result, node.range);
                } else if (node.name == "-" && child(node, 0).type->kind == TypeKind::Pointer)
                    emit("ptrdiff", saved, right, result, node.range);
                else emit(node.name, saved, right, result, node.range);
            }
            break;
        }
        case NodeType::Assign: {
            count(node, 2, 2);
            const auto& target = child(node, 0);
            if (target.category != ValueCategory::LValue) throw std::runtime_error("赋值目标必须是左值");
            const auto destination = address(target);
            std::string original;
            if (node.name != "=") {
                original = temporary(target.type);
                emit("load", destination, "-", original, node.range);
            }
            const auto source = expression(child(node, 1));
            if (node.name == "=") emit("store", source, "-", destination, node.range);
            else {
                if (node.name != "+=" && node.name != "-=" && node.name != "*=" && node.name != "/=" && node.name != "%=" &&
                    node.name != "&=" && node.name != "|=" && node.name != "^=" && node.name != "<<=" && node.name != ">>=")
                    throw std::runtime_error("不支持的复合赋值");
                const auto value = temporary(node.type);
                emit(node.type->kind == TypeKind::Pointer ? (node.name == "+=" ? "ptradd" : "ptrsub") : node.name.substr(0, node.name.size() - 1), original, source, value, node.range);
                emit("store", value, "-", destination, node.range);
            }
            result = temporary(node.type);
            emit("load", destination, "-", result, node.range);
            break;
        }
        case NodeType::UnaryOp: {
            count(node, 1, 1);
            const auto& operand = child(node, 0);
            if (node.name == "*") {
                if (node.type->kind == TypeKind::Function) result = expression(operand);
                else { result = temporary(node.type); emit("load", address(node), "-", result, node.range); }
                break;
            }
            if (node.name == "&") { result = address(operand); break; }
            const bool update = node.name == "pre++" || node.name == "post++" || node.name == "pre--" || node.name == "post--";
            if (update) {
                const auto destination = address(operand);
                const auto original = temporary(operand.type);
                emit("load", destination, "-", original, node.range);
                const auto updated = temporary(node.type);
                const bool increment = node.name.find("++") != std::string::npos;
                emit(node.type->kind == TypeKind::Pointer ? (increment ? "ptradd" : "ptrsub") : (increment ? "+" : "-"), original, integer(1), updated, node.range);
                emit("store", updated, "-", destination, node.range);
                result = node.name.compare(0, 4, "post") == 0 ? original : updated;
                break;
            }
            const auto source = expression(operand);
            if (node.name == "+") { result = source; break; }
            result = temporary(node.type);
            if (node.name == "-" || node.name == "!" || node.name == "~")
                emit(node.name == "-" ? "neg" : node.name == "!" ? "not" : "bnot", source, "-", result, node.range);
            else throw std::runtime_error("不支持的一元运算");
            break;
        }
        case NodeType::Call: {
            count(node, 1, invalid_id);
            const auto& callee = child(node, 0);
            const bool direct = callee.kind == NodeType::Identifier && callee.type->kind == TypeKind::Function;
            const auto target = direct ? detail::symbol_name(binding(callee).id) : snapshot(expression(callee),
                callee.type->kind == TypeKind::Function ? detail::pointer(callee.type) : callee.type, node.range);
            std::vector<std::string> arguments;
            for (std::size_t i = 1; i < node.children.size(); ++i) {
                const auto value = expression(child(node, i));
                // 实参逐个快照，嵌套调用在外层 arg 之前完成。
                arguments.push_back(snapshot(value, child(node, i).type, child(node, i).range));
            }
            for (const auto& value : arguments) emit("arg", value, "-", "-", node.range);
            result = node.type->kind == TypeKind::Void ? "-" : temporary(node.type);
            emit(direct ? "call" : "callind", target, std::to_string(arguments.size()), result, node.range);
            break;
        }
        default: throw std::runtime_error("此表达式不能生成 M1 四元式");
        }
        --depth_;
        return result;
    }

    void statement(const ASTNode& node) {
        current_range_ = node.range;
        if (++depth_ > 512) throw std::runtime_error("语法树嵌套过深");
        if (node.scope_id >= symbols_.scopes.size()) throw std::runtime_error("语句缺少作用域标注");
        switch (node.kind) {
        case NodeType::DoWhile: {
            count(node, 2, 2);
            const auto start = label(), condition = label(), end = label();
            mark(start, node.range);
            loops_.push_back({end, condition}); statement(child(node, 0)); loops_.pop_back();
            mark(condition, node.range);
            emit("jnz", expression(child(node, 1)), "-", start, node.range);
            mark(end, node.range);
            break;
        }
        case NodeType::Switch: {
            count(node, 2, 2);
            const auto value = snapshot(expression(child(node, 0)), child(node, 0).type, node.range);
            const auto end = label();
            std::unordered_map<const ASTNode*, std::string> labels;
            collect_cases(child(node, 1), labels);
            std::string default_label = end;
            for (const auto& item : labels) {
                if (item.first->kind == NodeType::Default) default_label = item.second;
                else {
                    const auto constant = std::get_if<std::int64_t>(&item.first->value);
                    if (!constant) throw std::runtime_error("case 缺少整型常量标注");
                    const auto match = temporary(detail::type(TypeKind::Int));
                    emit("==", value, integer(*constant), match, item.first->range);
                    emit("jnz", match, "-", item.second, item.first->range);
                }
            }
            emit("jmp", "-", "-", default_label, node.range);
            switches_.push_back(std::move(labels)); loops_.push_back({end, ""});
            statement(child(node, 1));
            switches_.pop_back(); loops_.pop_back();
            mark(end, node.range);
            break;
        }
        case NodeType::Case: case NodeType::Default:
            count(node, node.kind == NodeType::Case ? 2 : 1, node.kind == NodeType::Case ? 2 : 1);
            if (switches_.empty() || !switches_.back().count(&node)) throw std::runtime_error("case/default 缺少 switch 绑定");
            mark(switches_.back().at(&node), node.range);
            statement(*node.children.back());
            break;
        case NodeType::Block:
            count(node, 0, invalid_id);
            for (const auto& value : node.children) statement(*value);
            break;
        case NodeType::VarDecl: {
            count(node, 0, 1);
            const auto& entry = binding(node);
            if (node.storage == StorageClass::Extern && node.children.empty()) break;
            const auto destination = detail::symbol_name(entry.id);
            auto* saved_quads = quads_; auto* saved_locations = locations_; auto* saved_temporaries = temporaries_;
            const bool local_static = entry.scope != 0 && entry.storage == StorageClass::Static;
            if (local_static) {
                program_.globals.push_back(entry.id);
                quads_ = &program_.global_initializers; locations_ = &program_.global_locations; temporaries_ = &program_.global_temporaries;
            } else if (entry.scope != 0) emit("local", "-", "-", destination, node.range);
            if (!node.children.empty()) {
                const auto selected = temporary(detail::pointer(node.type));
                emit("addr", destination, "-", selected, node.range);
                if (detail::aggregate(node.type) && (child(node, 0).kind == NodeType::InitList || child(node, 0).kind == NodeType::StringLiteral))
                    emit("zero", "-", "-", destination, node.range);
                initialize(selected, node.type, child(node, 0));
            }
            quads_ = saved_quads; locations_ = saved_locations; temporaries_ = saved_temporaries;
            break;
        }
        case NodeType::StructDef: case NodeType::UnionDef: case NodeType::EnumDef: case NodeType::TypedefDecl: break;
        case NodeType::Label:
            count(node, 1, 1);
            mark("user_" + node.name, node.range); statement(child(node, 0)); break;
        case NodeType::Goto:
            count(node, 0, 0); emit("jmp", "-", "-", "user_" + node.name, node.range); break;
        case NodeType::ExprStmt:
            count(node, 1, 1); expression(child(node, 0)); break;
        case NodeType::Return:
            count(node, 0, 1);
            emit("ret", node.children.empty() ? "-" : expression(child(node, 0)), "-", "-", node.range);
            break;
        case NodeType::If: {
            count(node, 2, 3);
            const auto no = label();
            const auto end = label();
            emit("jz", expression(child(node, 0)), "-", no, node.range);
            statement(child(node, 1));
            emit("jmp", "-", "-", end, node.range);
            mark(no, node.range);
            if (node.children.size() == 3) statement(child(node, 2));
            mark(end, node.range);
            break;
        }
        case NodeType::While: case NodeType::For: {
            const bool for_loop = node.kind == NodeType::For;
            count(node, for_loop ? 4 : 2, for_loop ? 4 : 2);
            const auto start = label();
            const auto update = label();
            const auto end = label();
            if (for_loop) component(child(node, 0));
            mark(start, node.range);
            const auto& condition = child(node, for_loop ? 1 : 0);
            if (condition.kind != NodeType::Empty)
                emit("jz", expression(condition), "-", end, node.range);
            loops_.push_back({end, for_loop ? update : start});
            statement(*node.children.back());
            loops_.pop_back();
            if (for_loop) { mark(update, node.range); component(child(node, 2)); }
            emit("jmp", "-", "-", start, node.range);
            mark(end, node.range);
            break;
        }
        case NodeType::Break: case NodeType::Continue:
            count(node, 0, 0);
            if (loops_.empty()) throw std::runtime_error("循环跳转缺少循环上下文");
            if (node.kind == NodeType::Break) emit("jmp", "-", "-", loops_.back().first, node.range);
            else {
                auto found = loops_.rbegin();
                while (found != loops_.rend() && found->second.empty()) ++found;
                if (found == loops_.rend()) throw std::runtime_error("continue 缺少循环上下文");
                emit("jmp", "-", "-", found->second, node.range);
            }
            break;
        case NodeType::Empty: count(node, 0, 0); break;
        default: throw std::runtime_error("此语句不能生成 M1 四元式");
        }
        --depth_;
    }

    void component(const ASTNode& node) {
        if (node.kind == NodeType::Empty || node.kind == NodeType::ExprStmt || node.kind == NodeType::VarDecl)
            statement(node);
        else expression(node);
    }

    void collect_cases(const ASTNode& node, std::unordered_map<const ASTNode*, std::string>& labels, std::size_t depth = 0) {
        if (depth > 512) throw std::runtime_error("switch 嵌套过深");
        if (node.kind == NodeType::Switch) return;
        if (node.kind == NodeType::Case || node.kind == NodeType::Default) labels.emplace(&node, label());
        for (const auto& child : node.children) {
            if (!child) throw std::runtime_error("switch 孩子为空");
            collect_cases(*child, labels, depth + 1);
        }
    }

public:
    Generator(const SymbolTableData& symbols, IRGenerationOptions options) : symbols_(symbols), options_(options) {}

    IRResult generate(const Program& root) {
        try {
            current_range_ = root.range;
            if (root.kind != NodeType::Program || root.scope_id != 0 || symbols_.scopes.empty())
                throw std::runtime_error("IR 生成需要已检查的 Program 和符号表");
            count(root, 0, invalid_id);
            for (const auto& child_node : root.children) {
                const auto& node = *child_node;
                current_range_ = node.range;
                if (node.kind == NodeType::VarDecl) {
                    if (binding(node).scope != 0) throw std::runtime_error("顶层变量不属于全局作用域");
                    const auto& entry = binding(node);
                    if (entry.storage == StorageClass::Extern && !entry.is_defined) continue;
                    if (std::find(program_.globals.begin(), program_.globals.end(), node.symbol_id) == program_.globals.end()) program_.globals.push_back(node.symbol_id);
                    statement(node);
                } else if (node.kind == NodeType::TypedefDecl) {
                    if (node.symbol_id >= symbols_.symbols.size()) throw std::runtime_error("类型别名缺少符号绑定");
                } else if (node.kind == NodeType::StructDef || node.kind == NodeType::UnionDef || node.kind == NodeType::EnumDef) {
                    if (node.record_id >= symbols_.records.size()) throw std::runtime_error("结构体缺少记录绑定");
                } else if (node.kind == NodeType::FunctionDecl) {
                    if (binding(node).kind != SymbolKind::Function) throw std::runtime_error("函数声明绑定无效");
                } else if (node.kind == NodeType::FunctionDef) {
                    const auto& entry = binding(node);
                    if (!entry.is_defined || entry.kind != SymbolKind::Function) throw std::runtime_error("函数定义绑定无效");
                    count(node, entry.type->params.size() + 1, entry.type->params.size() + 1);
                    IRFunction function;
                    function.symbol_id = node.symbol_id;
                    for (std::size_t i = 0; i < entry.type->params.size(); ++i) {
                        const auto& parameter = child(node, i);
                        if (parameter.kind != NodeType::ParamDecl || binding(parameter).kind != SymbolKind::Parameter)
                            throw std::runtime_error("形参符号绑定无效");
                        function.parameters.push_back(parameter.symbol_id);
                    }
                    quads_ = &function.quads;
                    locations_ = &function.locations;
                    temporaries_ = &function.temporaries;
                    if (node.children.back()->kind != NodeType::Block) throw std::runtime_error("函数体必须是 Block");
                    statement(*node.children.back());
                    if (entry.type->base->kind == TypeKind::Void) emit("ret", "-", "-", "-", node.range);
                    program_.functions.push_back(std::move(function));
                    if (entry.name == "main") program_.entry_function = entry.id;
                    quads_ = &program_.global_initializers;
                    locations_ = &program_.global_locations;
                    temporaries_ = &program_.global_temporaries;
                } else throw std::runtime_error("不支持的顶层语法节点");
            }
        } catch (const std::runtime_error& exception) {
            diagnostics_.report(Level::Error, current_range_, exception.what(), "IR_INVALID_AST");
        } catch (const std::invalid_argument& exception) {
            diagnostics_.report(Level::Error, current_range_, exception.what(), "IR_INVALID_CONSTANT");
        }
        program_.constants = std::move(constants_).release();
        return {std::move(program_), diagnostics_.take_diagnostics()};
    }
};

}

IRResult generate(const Program& program, const SymbolTableData& symbols) {
    return generate(program, symbols, {});
}

IRResult generate(const Program& program, const SymbolTableData& symbols, const IRGenerationOptions& options) {
    return Generator(symbols, options).generate(program);
}

}
