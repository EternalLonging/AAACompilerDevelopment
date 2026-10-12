// ===== merged from backend/constant_pool.cpp =====
#include "minic/constant_pool.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace minic {
namespace {

// 将对象表示的字节加入内部键；仅在本次编译进程内使用，不作为磁盘序列化格式。
template <typename T>
void append_bytes(std::string& key, const T& value) {
    key.append(reinterpret_cast<const char*>(&value), sizeof(value));
}

bool integer_kind(TypeKind kind) {
    return kind == TypeKind::Char || kind == TypeKind::Short ||
           kind == TypeKind::Int || kind == TypeKind::Long;
}

// 复制合法类型，避免调用者修改原对象后改变已登记常量的类型。
TypePtr snapshot_type(const TypePtr& type) {
    if (!type || type->base || type->array_length || !type->params.empty() ||
        type->variadic || !type->has_prototype || !type->name.empty() ||
        type->record_id != invalid_id) {
        throw std::invalid_argument("常量标量类型的字段不符合合同");
    }
    return make_type_info(*type);
}

void append_scalar_type(std::string& key, const TypeInfo& type) {
    append_bytes(key, type.kind);
    append_bytes(key, type.is_unsigned);
    append_bytes(key, type.is_const);
}

} // 内部辅助函数不作为跨模块接口。

ConstantId ConstantPool::intern(TypePtr type, ConstantValue value,
                                const std::string& spelling,
                                const SourceRange& range) {
    if (!type) throw std::invalid_argument("常量类型不能为空");
    std::string key;
    if (integer_kind(type->kind)) {
        type = snapshot_type(type);
        append_scalar_type(key, *type);
        if (type->is_unsigned) {
            const auto number = constant_get_if<unsigned long long>(value);
            if (!number) throw std::invalid_argument("无符号整数常量需要 unsigned long long 值");
            append_bytes(key, *number);
        } else {
            const auto number = constant_get_if<long long>(value);
            if (!number) throw std::invalid_argument("有符号整数常量需要 long long 值");
            append_bytes(key, *number);
        }
    } else if (type->kind == TypeKind::Float || type->kind == TypeKind::Double || type->kind == TypeKind::LongDouble) {
        type = snapshot_type(type);
        if (type->is_unsigned) throw std::invalid_argument("浮点类型不能带 unsigned");
        auto number = constant_get_if<double>(value);
        if (!number || !std::isfinite(*number)) {
            throw std::invalid_argument("浮点常量需要有限的 double 值");
        }
        append_scalar_type(key, *type);
        if (type->kind == TypeKind::Float) {
            // 先判断范围，避免把超出 float 范围的值直接窄化转换。
            if (std::fabs(*number) > std::numeric_limits<float>::max()) {
                throw std::invalid_argument("浮点常量超出 float 可表示范围");
            }
            const float rounded = static_cast<float>(*number);
            *number = static_cast<double>(rounded);
        }
        // 不做 == 或近似比较：正负零必须分开，不同浮点位表示不能误合并。
        append_bytes(key, *number);
    } else if (type->kind == TypeKind::Array) {
        const auto bytes = constant_get_if<std::string>(value);
        if (!bytes || !type->base || type->base->kind != TypeKind::Char ||
            type->is_unsigned || !type->array_length ||
            *type->array_length == 0 || *type->array_length - 1 != bytes->size() ||
            !type->params.empty() || type->variadic || !type->has_prototype ||
            !type->name.empty() || type->record_id != invalid_id) {
            throw std::invalid_argument("字符串常量需要长度包含终止零的窄 char[N] 类型");
        }
        const auto base = snapshot_type(type->base);
        if (base->is_unsigned) throw std::invalid_argument("字符串元素必须为普通 char");
        auto copy = make_type_info(*type);
        copy->base = base;
        type = std::move(copy);
        append_scalar_type(key, *type);
        append_bytes(key, *type->array_length);
        append_scalar_type(key, *type->base);
        key.append(*bytes); // std::string 按长度追加，内容中的零字节不会被截断。
    } else {
        throw std::invalid_argument("该常量类型尚未支持，不能丢失类型或精度后入池");
    }

    const auto found = index_.find(key);
    if (found != index_.end()) return found->second;
    if (data_.entries.size() >= invalid_id) {
        throw std::length_error("常量编号已耗尽");
    }
    const auto id = static_cast<ConstantId>(data_.entries.size());
    data_.entries.push_back({id, std::move(type), std::move(value), spelling, range});
    try {
        index_.emplace(std::move(key), id);
    } catch (...) {
        data_.entries.pop_back(); // 索引分配失败时不留下没有去重索引的半条记录。
        throw;
    }
    return id;
}

const ConstantEntry* ConstantPool::get(ConstantId id) const noexcept {
    return id < data_.entries.size() ? &data_.entries[id] : nullptr;
}

std::size_t ConstantPool::size() const noexcept { return data_.entries.size(); }

const ConstantPoolData& ConstantPool::data() const noexcept { return data_; }

ConstantPoolData ConstantPool::release() && {
    ConstantPoolData result;
    result.entries.swap(data_.entries);
    index_.clear();
    return result;
}

std::string constant_operand(ConstantId id) {
    if (id == invalid_id) throw std::invalid_argument("无效常量编号不能生成操作数");
    return "%c" + std::to_string(id);
}

} // minic 命名空间

// ===== merged from backend/ir.cpp =====
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

    std::string integer(long long value) {
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
        const auto result = temporary(detail::address_type(node.type));
        if (node.kind == NodeType::Identifier) {
            binding(node); emit("addr", detail::symbol_name(node.symbol_id), "-", result, node.range);
        } else if (node.kind == NodeType::ArrayAccess) {
            count(node, 2, 2);
            const bool array_parameter = child(node, 0).type->kind == TypeKind::Address;
            const auto base = array_parameter ? snapshot(expression(child(node, 0)), child(node, 0).type, node.range) : address(child(node, 0), depth + 1);
            const auto index = expression(child(node, 1));
            emit(array_parameter ? "offsetaddr" : "indexaddr", base, index, result, node.range);
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
                if (detail::numeric(target) || target->kind == TypeKind::Address) { initialize(destination, target, child(init, i)); continue; }
                TypePtr element = nullptr;
                std::string selected;
                if (target->kind == TypeKind::Array) {
                    element = target->base;
                    selected = temporary(detail::address_type(element));
                    emit("indexaddr", destination, integer(static_cast<long long>(i)), selected, init.range);
                } else {
                    const auto& member = symbols_.records.at(target->record_id).members.at(i);
                    element = member.type;
                    selected = temporary(detail::address_type(element));
                    emit("memberaddr", destination, std::to_string(*member.offset), selected, init.range);
                }
                initialize(selected, element, child(init, i));
            }
        } else if (target->kind == TypeKind::Array && init.kind == NodeType::StringLiteral) {
            const auto& text = constant_get<std::string>(init.value);
            for (std::size_t i = 0; i < text.size(); ++i) {
                const auto selected = temporary(detail::address_type(target->base));
                emit("indexaddr", destination, integer(static_cast<long long>(i)), selected, init.range);
                const auto byte = static_cast<unsigned char>(text[i]);
                const auto character = byte <= 127 ? static_cast<long long>(byte) : static_cast<long long>(byte) - 256;
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
            if (node.type->kind == TypeKind::Function) throw std::runtime_error("函数名字不能作为值生成四元式");
            result = detail::symbol_name(node.symbol_id);
            break;
        case NodeType::IntLiteral: case NodeType::FloatLiteral:
        case NodeType::CharLiteral: case NodeType::StringLiteral:
            count(node, 0, 0);
            result = constant_operand(constants_.intern(node.type, node.value, node.name, node.range));
            break;
        case NodeType::ImplicitCast: case NodeType::Cast: {
            count(node, 1, 1);
            if (child(node, 0).type->kind == TypeKind::Array && child(node, 0).kind != NodeType::StringLiteral && node.type->kind == TypeKind::Address) {
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
                emit(node.name, saved, right, result, node.range);
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
                emit(node.name.substr(0, node.name.size() - 1), original, source, value, node.range);
                emit("store", value, "-", destination, node.range);
            }
            result = temporary(node.type);
            emit("load", destination, "-", result, node.range);
            break;
        }
        case NodeType::UnaryOp: {
            count(node, 1, 1);
            const auto& operand = child(node, 0);
            if (node.name == "&") { result = address(operand); break; }
            const bool update = node.name == "pre++" || node.name == "post++" || node.name == "pre--" || node.name == "post--";
            if (update) {
                const auto destination = address(operand);
                const auto original = temporary(operand.type);
                emit("load", destination, "-", original, node.range);
                const auto updated = temporary(node.type);
                const bool increment = node.name.find("++") != std::string::npos;
                emit(increment ? "+" : "-", original, integer(1), updated, node.range);
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
            const auto target = detail::symbol_name(binding(callee).id);
            std::vector<std::string> arguments;
            for (std::size_t i = 1; i < node.children.size(); ++i) {
                const auto value = expression(child(node, i));
                // 实参逐个快照，嵌套调用在外层 arg 之前完成。
                arguments.push_back(snapshot(value, child(node, i).type, child(node, i).range));
            }
            for (const auto& value : arguments) emit("arg", value, "-", "-", node.range);
            result = node.type->kind == TypeKind::Void ? "-" : temporary(node.type);
            emit("call", target, std::to_string(arguments.size()), result, node.range);
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
        case NodeType::Block:
            count(node, 0, invalid_id);
            for (const auto& value : node.children) statement(*value);
            break;
        case NodeType::VarDecl: {
            count(node, 0, 1);
            const auto& entry = binding(node);
            const auto destination = detail::symbol_name(entry.id);
            if (entry.scope != 0) emit("local", "-", "-", destination, node.range);
            if (!node.children.empty()) {
                const auto selected = temporary(detail::address_type(node.type));
                emit("addr", destination, "-", selected, node.range);
                if (detail::aggregate(node.type) && (child(node, 0).kind == NodeType::InitList || child(node, 0).kind == NodeType::StringLiteral))
                    emit("zero", "-", "-", destination, node.range);
                initialize(selected, node.type, child(node, 0));
            }
            break;
        }
        case NodeType::StructDef: break;
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
            else emit("jmp", "-", "-", loops_.back().second, node.range);
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
                    if (std::find(program_.globals.begin(), program_.globals.end(), node.symbol_id) == program_.globals.end()) program_.globals.push_back(node.symbol_id);
                    statement(node);
                } else if (node.kind == NodeType::StructDef) {
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

// ===== merged from backend/interpreter.cpp =====
#include "minic/interpreter.hpp"
#include "minic/constant_pool.hpp"
#include "minic/diagnostic.hpp"
#include "minic/semantic.hpp"
#include "internal.hpp"

#include <cctype>
#include <cstring>
#include <unordered_set>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <type_traits>
#include <deque>

namespace minic {
namespace {

struct Address {
    unsigned long long frame = 0; // 0 是全局区，其余是调用帧的唯一编号。
    SymbolId symbol = invalid_id; // 所指对象编号，invalid_id 表示没有绑定变量。
    std::size_t offset = 0; // 子对象相对根对象的字节偏移。
    TypePtr type = nullptr; // 当前地址所指向的子对象类型。
    std::size_t first = 0; // 可移动范围的起点，包含此偏移。
    std::size_t last = 0; // 数组存储范围的终点，不包含此位置。
    const std::string* literal = nullptr; // 指向常量字符串池中的只读字节。
    std::string temporary; // 临时聚合对象的名字，普通地址为空。
};
struct Aggregate;
enum class ValueKind { Empty, Integer, Floating, Text, Address, Aggregate };
struct Value {
    ValueKind kind = ValueKind::Empty;
    long long integer = 0;
    double floating = 0;
    std::string text;
    Address address;
    Aggregate* aggregate = nullptr;
    Value() = default;
    Value(long long value) : kind(ValueKind::Integer), integer(value) {}
    Value(double value) : kind(ValueKind::Floating), floating(value) {}
    Value(const std::string& value) : kind(ValueKind::Text), text(value) {}
    Value(std::string&& value) : kind(ValueKind::Text), text(std::move(value)) {}
    Value(const Address& value) : kind(ValueKind::Address), address(value) {}
    Value(Aggregate* value) : kind(ValueKind::Aggregate), aggregate(value) {}
};
struct Aggregate {
    std::unordered_map<std::size_t, Value> cells; // 按字节偏移保存已写入的数值叶子。
    std::unordered_map<std::size_t, unsigned char> bytes; // 按字节偏移保存数值对象的小端字节。
    std::unordered_set<std::size_t> unknown; // 默认零对象中仍未初始化的字节。
    bool zero = false; // 未写入的叶子是否已零初始化。
};

template <typename T> bool holds(const Value& value);
template <> inline bool holds<long long>(const Value& value) { return value.kind == ValueKind::Integer; }
template <> inline bool holds<double>(const Value& value) { return value.kind == ValueKind::Floating; }
template <> inline bool holds<std::string>(const Value& value) { return value.kind == ValueKind::Text; }
template <> inline bool holds<Address>(const Value& value) { return value.kind == ValueKind::Address; }
template <> inline bool holds<Aggregate*>(const Value& value) { return value.kind == ValueKind::Aggregate; }
inline bool value_empty(const Value& value) { return value.kind == ValueKind::Empty; }
template <typename T> T* get_if(Value* value);
template <> inline long long* get_if<long long>(Value* value) { return value->kind == ValueKind::Integer ? &value->integer : nullptr; }
template <> inline double* get_if<double>(Value* value) { return value->kind == ValueKind::Floating ? &value->floating : nullptr; }
template <> inline std::string* get_if<std::string>(Value* value) { return value->kind == ValueKind::Text ? &value->text : nullptr; }
template <> inline Address* get_if<Address>(Value* value) { return value->kind == ValueKind::Address ? &value->address : nullptr; }
template <> inline Aggregate** get_if<Aggregate*>(Value* value) { return value->kind == ValueKind::Aggregate ? &value->aggregate : nullptr; }
template <typename T> const T* get_if(const Value* value) { return get_if<T>(const_cast<Value*>(value)); }
template <typename T> T& get(Value& value);
template <> inline long long& get<long long>(Value& value) { return value.integer; }
template <> inline double& get<double>(Value& value) { return value.floating; }
template <> inline std::string& get<std::string>(Value& value) { return value.text; }
template <> inline Address& get<Address>(Value& value) { return value.address; }
template <> inline Aggregate*& get<Aggregate*>(Value& value) { return value.aggregate; }
template <typename T> const T& get(const Value& value) { return get<T>(const_cast<Value&>(value)); }

struct RuntimeArena {
    std::vector<Aggregate*> aggregates;
    std::deque<std::string> strings;
    ~RuntimeArena() { for (Aggregate* value : aggregates) delete value; }
    Aggregate* make_aggregate() { Aggregate* value = new Aggregate(); aggregates.push_back(value); return value; }
    const std::string* stable_string(const std::string& value) { strings.push_back(value); return &strings.back(); }
};
RuntimeArena runtime_arena;
Aggregate* make_aggregate() { return runtime_arena.make_aggregate(); }
Aggregate* copy_aggregate(const Aggregate& source) {
    Aggregate* result = runtime_arena.make_aggregate();
    *result = source;
    return result;
}
const std::string* stable_string(const std::string& value) { return runtime_arena.stable_string(value); }

struct Code {
    const std::vector<Quadruple>* quads = nullptr;
    const std::vector<SourceRange>* locations = nullptr;
    const std::vector<TemporaryEntry>* temporaries = nullptr;
    SymbolId function = invalid_id;
    std::unordered_map<std::string, std::size_t> labels;
    std::unordered_map<std::string, TypePtr> types;
};

struct Frame {
    const Code* code = nullptr;
    unsigned long long id = 0;
    std::size_t pc = 0; // 下一条待执行指令。
    std::unordered_map<std::string, Value> values; // 局部对象和临时量的值。
    std::vector<Value> arguments; // 当前调用的实参队列。
    std::string destination = "-"; // 返回值写入调用者的位置。
};

std::size_t decimal(const std::string& text) {
    if (text.empty()) throw std::runtime_error("指令编号为空");
    std::size_t value = 0;
    for (const unsigned char character : text) {
        if (character < '0' || character > '9' ||
            value > (std::numeric_limits<std::size_t>::max() - (character - '0')) / 10)
            throw std::runtime_error("指令编号不是有效的十进制数");
        value = value * 10 + character - '0';
    }
    return value;
}

std::size_t operand_id(const std::string& value, char kind) {
    if (value.size() < 3 || value[0] != '%' || value[1] != kind)
        throw std::runtime_error("四元式操作数格式无效：" + value);
    return decimal(value.substr(2));
}

double number(const Value& value) {
    if (const auto* integer = get_if<long long>(&value)) return static_cast<double>(*integer);
    if (const auto* floating = get_if<double>(&value)) return *floating;
    throw std::runtime_error("指令需要数值操作数");
}

bool truth(const Value& value) {
    if (const auto* address = get_if<Address>(&value)) return address->symbol != invalid_id || address->literal || !address->temporary.empty();
    if (holds<std::string>(value)) return true;
    return number(value) != 0;
}

bool assign_type(const TypePtr& target, const TypePtr& source) {
    return same_type(target, source) || can_assign(target, source) || detail::address_compatible(target, source) ||
        (target && source && detail::aggregate(target) && detail::same_unqualified(target, source));
}

bool compatible_subobject(const TypePtr& target, const TypePtr& member, bool parent_const) {
    if (!target || !member) return false;
    auto qualified = *member;
    qualified.is_const = qualified.is_const || parent_const;
    return same_type(target, &qualified);
}

Value convert_value(const Value& value, const TypePtr& target) {
    if (!target) throw std::runtime_error("目标类型为空");
    if (detail::numeric(target)) {
        const double numeric = number(value);
        if (!std::isfinite(numeric)) throw std::runtime_error("不允许非有限数值");
        if (detail::floating(target)) return target->kind == TypeKind::Float ? detail::checked_float(numeric) : numeric;
        const double truncated = std::trunc(numeric);
        const auto bits = detail::integer_bits(target);
        const auto modulus = static_cast<unsigned long long>(1) << bits;
        if (target->is_unsigned) {
            if (holds<long long>(value)) return static_cast<long long>(static_cast<unsigned long long>(get<long long>(value)) % modulus);
            if (truncated < 0 || truncated >= static_cast<double>(modulus)) throw std::runtime_error("浮点转无符号整数超出范围");
            return static_cast<long long>(truncated);
        }
        const auto low = -(static_cast<long long>(1) << (bits - 1)), high = (static_cast<long long>(1) << (bits - 1)) - 1;
        if (truncated < static_cast<double>(low) || truncated > static_cast<double>(high))
            throw std::runtime_error("浮点转整数或整数结果超出范围");
        return static_cast<long long>(truncated);
    }
    if (target->kind == TypeKind::Address && target->base) {
        if (const auto* text = get_if<std::string>(&value); text && target->base->kind == TypeKind::Char)
            return Address{0, invalid_id, 0, target->base, 0, text->size() + 1, stable_string(*text), {}};
        if (const auto* address = get_if<Address>(&value)) {
            auto converted = *address; converted.type = target->base; return converted;
        }
    }
    if (detail::aggregate(target) && holds<Aggregate*>(value))
        return copy_aggregate(*get<Aggregate*>(value));
    throw std::runtime_error("不支持的运行值转换");
}

class Machine {
    const IRProgram& program_;
    const SymbolTableData& symbols_;
    std::istream& input_;
    std::ostream& output_;
    RunOptions options_;
    DiagnosticEngine diagnostics_{Phase::Runtime};
    std::unordered_map<SymbolId, Code> functions_;
    Code initializer_;
    std::unordered_set<SymbolId> globals_;
    std::unordered_map<std::string, Value> global_values_;
    std::vector<Frame> frames_; // 显式调用栈，递归不占用宿主 C++ 调用栈。
    unsigned long long next_frame_ = 1;
    RunResult result_;
    SourceRange location_;

    const SymbolEntry& symbol(std::size_t id) const {
        if (id >= symbols_.symbols.size() || symbols_.symbols[id].id != id)
            throw std::runtime_error("IR 引用了无效符号编号");
        return symbols_.symbols[id];
    }

    const SymbolEntry& function(const std::string& value) const {
        const auto& entry = symbol(operand_id(value, 's'));
        if (entry.kind != SymbolKind::Function || !entry.type || entry.type->kind != TypeKind::Function || !entry.type->base)
            throw std::runtime_error("call 引用了非函数符号");
        if (entry.builtin == BuiltinKind::None && functions_.find(entry.id) == functions_.end())
            throw std::runtime_error("被调用的函数没有中间代码");
        return entry;
    }

    ScopeId function_scope(ScopeId scope) const {
        for (std::size_t count = 0; scope != invalid_id && count <= symbols_.scopes.size(); ++count) {
            if (scope >= symbols_.scopes.size()) throw std::runtime_error("符号作用域编号无效");
            if (symbols_.scopes[scope].kind == ScopeKind::Function) return scope;
            scope = symbols_.scopes[scope].parent;
        }
        return invalid_id;
    }

    TypePtr operand_type(const std::string& name, const Code& code, ScopeId& owner) const {
        if (name.size() > 2 && name[0] == '%' && name[1] == 'c') {
            const auto id = operand_id(name, 'c');
            if (id >= program_.constants.entries.size()) throw std::runtime_error("常量编号越界");
            return program_.constants.entries[id].type;
        }
        const auto temp = code.types.find(name);
        if (temp != code.types.end()) return temp->second;
        const auto& entry = symbol(operand_id(name, 's'));
        if (entry.kind != SymbolKind::Variable && entry.kind != SymbolKind::Parameter && entry.kind != SymbolKind::Array)
            throw std::runtime_error("操作数必须是数值对象或临时量");
        detail::layout(entry.type, symbols_);
        if (entry.scope == 0) {
            if (globals_.find(entry.id) == globals_.end()) throw std::runtime_error("全局对象没有分配存储");
        } else {
            if (code.function == invalid_id) throw std::runtime_error("全局初始化不能引用局部对象");
            const auto scope = function_scope(entry.scope);
            if (scope == invalid_id || (owner != invalid_id && owner != scope))
                throw std::runtime_error("同一函数 IR 混入了其他函数的局部对象");
            owner = scope;
        }
        return entry.type;
    }

    TypePtr target_type(const std::string& name, const Code& code, ScopeId& owner) const {
        if (name.size() > 1 && name[0] == '%' && name[1] == 'c') throw std::runtime_error("不能写入常量池");
        return operand_type(name, code, owner);
    }

    void validate_code(Code& code, const IRFunction* ir_function) {
        if (code.quads->size() != code.locations->size()) throw std::runtime_error("指令与源码位置数量不一致");
        for (const auto& entry : *code.temporaries) {
            if (entry.name != "%t" + std::to_string(code.types.size()) ||
                !(detail::numeric(entry.type) || detail::aggregate(entry.type) ||
                  (entry.type && entry.type->kind == TypeKind::Address && entry.type->base)))
                throw std::runtime_error("临时量名字、顺序或类型无效");
            if (detail::aggregate(entry.type)) detail::layout(entry.type, symbols_);
            code.types.emplace(entry.name, entry.type);
        }
        ScopeId owner = invalid_id;
        if (ir_function) {
            const auto& entry = symbol(ir_function->symbol_id);
            if (ir_function->parameters.size() != entry.type->params.size()) throw std::runtime_error("IR 形参数量与函数签名不一致");
            std::unordered_set<SymbolId> seen;
            for (std::size_t i = 0; i < ir_function->parameters.size(); ++i) {
                const auto& parameter = symbol(ir_function->parameters[i]);
                if (parameter.kind != SymbolKind::Parameter || !seen.insert(parameter.id).second ||
                    !same_type(detail::unqualified(parameter.type), entry.type->params[i])) throw std::runtime_error("IR 形参编号或类型无效");
                operand_type(detail::symbol_name(parameter.id), code, owner);
            }
        }
        for (std::size_t i = 0; i < code.quads->size(); ++i) {
            const auto& quad = (*code.quads)[i];
            if (quad.op == "label" && (quad.result.empty() || quad.result == "-" ||
                !code.labels.emplace(quad.result, i).second)) throw std::runtime_error("标号为空或重复");
        }
        std::vector<TypePtr> queued;
        for (std::size_t i = 0; i < code.quads->size(); ++i) {
            location_ = (*code.locations)[i];
            const auto& q = (*code.quads)[i];
            if (!queued.empty() && q.op != "arg" && q.op != "call") throw std::runtime_error("arg 指令必须紧邻本次 call");
            const auto input_type = [&](const std::string& name) { return operand_type(name, code, owner); };
            const auto destination_type = [&]() { return target_type(q.result, code, owner); };
            const auto require_numeric = [&](const std::string& name) {
                if (!detail::numeric(input_type(name))) throw std::runtime_error("操作数类型必须为数值");
            };
            const auto require_scalar = [&](const std::string& name) {
                const auto type = input_type(name);
                if (!detail::numeric(type) && type->kind != TypeKind::Address)
                    throw std::runtime_error("操作数类型必须为数值或内部地址");
            };
            if (q.op == "label" || q.op == "jmp") {
                if (q.arg1 != "-" || q.arg2 != "-" || (q.op == "jmp" && !code.labels.count(q.result)))
                    throw std::runtime_error("标号或跳转目标无效");
            } else if (q.op == "jz" || q.op == "jnz") {
                require_scalar(q.arg1);
                if (q.arg2 != "-" || !code.labels.count(q.result)) throw std::runtime_error("条件跳转目标无效");
            } else if (q.op == "local" || q.op == "zero") {
                const auto& entry = symbol(operand_id(q.result, 's'));
                destination_type();
                if ((q.op == "local" && globals_.count(entry.id)) ||
                    (entry.kind != SymbolKind::Variable && entry.kind != SymbolKind::Array) || q.arg1 != "-" || q.arg2 != "-")
                    throw std::runtime_error("local 只能重置局部变量");
            } else if (q.op == "load" || q.op == "store") {
                const auto address_value = input_type(q.op == "load" ? q.arg1 : q.result);
                if (!address_value || address_value->kind != TypeKind::Address || !address_value->base || q.arg2 != "-")
                    throw std::runtime_error("load/store 需要合法对象地址");
                const auto value = q.op == "load" ? destination_type() : input_type(q.arg1);
                if (!assign_type(q.op == "load" ? value : address_value->base, q.op == "load" ? address_value->base : value))
                    throw std::runtime_error("load/store 的值与所指类型不兼容");
            } else if (q.op == "indexaddr") {
                const auto base = input_type(q.arg1), target = destination_type();
                const auto index = input_type(q.arg2);
                if (base->kind != TypeKind::Address || !base->base || base->base->kind != TypeKind::Array ||
                    target->kind != TypeKind::Address || !detail::numeric(index) || detail::floating(index) ||
                    !compatible_subobject(target->base, base->base->base, base->base->is_const))
                    throw std::runtime_error("indexaddr 类型无效");
            } else if (q.op == "decay") {
                const auto base = input_type(q.arg1), target = destination_type();
                if (q.arg2 != "-" || base->kind != TypeKind::Address || !base->base || base->base->kind != TypeKind::Array ||
                    target->kind != TypeKind::Address || !compatible_subobject(target->base, base->base->base, base->base->is_const))
                    throw std::runtime_error("decay 类型无效");
            } else if (q.op == "offsetaddr") {
                const auto base = input_type(q.arg1), index = input_type(q.arg2), target = destination_type();
                if (base->kind != TypeKind::Address || !base->base || !detail::address_compatible(target, base) ||
                    !detail::numeric(index) || detail::floating(index)) throw std::runtime_error("数组形参寻址类型无效");
                detail::layout(base->base, symbols_);
            } else if (q.op == "memberaddr") {
                const auto base = input_type(q.arg1), target = destination_type();
                if (base->kind != TypeKind::Address || !base->base || (base->base->kind != TypeKind::Struct) ||
                    target->kind != TypeKind::Address || !target->base)
                    throw std::runtime_error("memberaddr 类型无效");
                const auto offset = decimal(q.arg2);
                bool found = false;
                for (const auto& member : symbols_.records.at(base->base->record_id).members)
                    if (member.offset == offset && compatible_subobject(target->base, member.type, base->base->is_const)) found = true;
                if (!found) throw std::runtime_error("memberaddr 偏移或类型不对应已定义的成员");
            } else if (q.op == "arg") {
                const auto argument = input_type(q.arg1);
                if (q.arg2 != "-" || q.result != "-") throw std::runtime_error("arg 格式无效");
                queued.push_back(argument);
            } else if (q.op == "call") {
                const auto callee = function(q.arg1);
                const auto count = decimal(q.arg2);
                if (count != queued.size() || (callee.builtin == BuiltinKind::None && count != callee.type->params.size()) ||
                    (callee.builtin != BuiltinKind::None && count < 1)) throw std::runtime_error("调用的实参数量无效");
                if (callee.builtin == BuiltinKind::None) {
                    for (std::size_t argument = 0; argument < queued.size(); ++argument)
                        if (!assign_type(callee.type->params[argument], queued[argument]))
                            throw std::runtime_error("call 的实参类型与签名不兼容");
                } else if (queued.front()->kind != TypeKind::Address || !queued.front()->base ||
                           queued.front()->base->kind != TypeKind::Char)
                    throw std::runtime_error("输入输出的首个实参必须是字符串地址");
                if (q.result != "-" && !same_type(destination_type(), callee.type->base))
                    throw std::runtime_error("函数返回值目标类型无效");
                queued.clear();
            } else if (q.op == "ret") {
                if (code.function == invalid_id || q.arg2 != "-" || q.result != "-") throw std::runtime_error("ret 格式无效");
                const auto returned = symbol(code.function).type->base;
                if ((returned->kind == TypeKind::Void) != (q.arg1 == "-")) throw std::runtime_error("返回值与函数类型不一致");
                if (q.arg1 != "-" && !assign_type(returned, input_type(q.arg1))) throw std::runtime_error("返回值类型不兼容");
            } else if (q.op == "tempaddr") {
                const auto source = input_type(q.arg1), target = destination_type();
                if (q.arg2 != "-" || !code.types.count(q.arg1) || !detail::aggregate(source) || target->kind != TypeKind::Address || !same_type(source, target->base))
                    throw std::runtime_error("临时聚合地址类型无效");
            } else if (q.op == "addr") {
                const auto& entry = symbol(operand_id(q.arg1, 's'));
                input_type(q.arg1);
                const auto target = destination_type();
                if (target->kind != TypeKind::Address ||
                    !same_type(entry.type, target->base) || q.arg2 != "-") throw std::runtime_error("addr 类型无效");
            } else if (q.op == "=" || q.op == "cvt" || q.op == "cvt_i2f") {
                const auto source = input_type(q.arg1);
                const auto target = destination_type();
                if (q.arg2 != "-") throw std::runtime_error("复制或转换指令格式无效");
                if (q.op == "=" && !assign_type(target, source))
                    throw std::runtime_error("复制类型不兼容");
                if (q.op == "cvt_i2f" && !(source->kind == TypeKind::Int && target->kind == TypeKind::Float))
                    throw std::runtime_error("cvt_i2f 类型无效");
                if (q.op == "cvt" && !((detail::numeric(source) && detail::numeric(target)) ||
                    detail::address_compatible(target, source) ||
                    (source->kind == TypeKind::Array && source->base && source->base->kind == TypeKind::Char &&
                     target->kind == TypeKind::Address && target->base->kind == TypeKind::Char)))
                    throw std::runtime_error("不支持该转换指令");
            } else if (q.op == "neg" || q.op == "not" || q.op == "bnot") {
                if (q.op == "not") require_scalar(q.arg1); else require_numeric(q.arg1);
                if (q.arg2 != "-" || !detail::numeric(destination_type()) ||
                    (q.op == "not" && destination_type()->kind != TypeKind::Int) ||
                    (q.op == "bnot" && (!detail::integral(input_type(q.arg1)) || !detail::integral(destination_type())))) throw std::runtime_error("一元指令类型无效");
            } else if (q.op == "+" || q.op == "-" || q.op == "*" || q.op == "/" || q.op == "%" ||
                       q.op == "<" || q.op == "<=" || q.op == ">" || q.op == ">=" || q.op == "==" || q.op == "!=" ||
                       q.op == "&" || q.op == "|" || q.op == "^" || q.op == "<<" || q.op == ">>") {
                require_numeric(q.arg1); require_numeric(q.arg2);
                const bool comparison = q.op == "<" || q.op == "<=" || q.op == ">" || q.op == ">=" || q.op == "==" || q.op == "!=";
                const bool integral = q.op == "%" || q.op == "&" || q.op == "|" || q.op == "^" || q.op == "<<" || q.op == ">>";
                if (!detail::numeric(destination_type()) || (comparison && destination_type()->kind != TypeKind::Int) ||
                    (integral && (detail::floating(input_type(q.arg1)) || detail::floating(input_type(q.arg2)) || detail::floating(destination_type()))))
                    throw std::runtime_error("二元指令类型无效");
            } else throw std::runtime_error("未知指令：" + q.op);
        }
        if (!queued.empty()) throw std::runtime_error("函数末尾存在未消费的实参");
    }

    void validate() {
        for (std::size_t i = 0; i < symbols_.scopes.size(); ++i) {
            const auto& scope = symbols_.scopes[i];
            if (scope.id != i || (i == 0 ? scope.parent != invalid_id : scope.parent >= i))
                throw std::runtime_error("作用域身份或父链无效");
        }
        ConstantPool checker;
        for (std::size_t i = 0; i < program_.constants.entries.size(); ++i) {
            const auto& entry = program_.constants.entries[i];
            if (entry.id != i) throw std::runtime_error("常量池编号无效");
            checker.intern(entry.type, entry.value, entry.spelling, entry.range);
            if (detail::numeric(entry.type)) {
                if (detail::floating(entry.type)) convert_value(constant_get<double>(entry.value), entry.type);
                else if (entry.type->is_unsigned) {
                    const auto value = constant_get<unsigned long long>(entry.value);
                    if (value >= (static_cast<unsigned long long>(1) << detail::integer_bits(entry.type))) throw std::runtime_error("无符号常量超出范围");
                } else convert_value(constant_get<long long>(entry.value), entry.type);
            } else if (!(entry.type->kind == TypeKind::Array && entry.type->base->kind == TypeKind::Char))
                throw std::runtime_error("M1 常量池类型不受支持");
        }
        for (const auto id : program_.globals) {
            const auto& entry = symbol(id);
            if ((entry.kind != SymbolKind::Variable && entry.kind != SymbolKind::Array) ||
                entry.scope != 0 || !globals_.insert(id).second)
                throw std::runtime_error("全局变量清单无效");
            detail::layout(entry.type, symbols_);
        }
        for (const auto& function_ir : program_.functions) {
            const auto& entry = symbol(function_ir.symbol_id);
            if (entry.kind != SymbolKind::Function || !entry.is_defined || entry.builtin != BuiltinKind::None ||
                !entry.type || entry.type->kind != TypeKind::Function || !entry.type->base || entry.type->variadic ||
                !(detail::numeric(entry.type->base) || entry.type->base->kind == TypeKind::Void || entry.type->base->kind == TypeKind::Address || detail::aggregate(entry.type->base)))
                throw std::runtime_error("函数清单含有无效签名");
            Code code;
            code.quads = &function_ir.quads;
            code.locations = &function_ir.locations;
            code.temporaries = &function_ir.temporaries;
            code.function = entry.id;
            if (!functions_.emplace(entry.id, std::move(code)).second) throw std::runtime_error("函数 IR 重复");
        }
        const auto& main = symbol(program_.entry_function);
        if (main.name != "main" || !main.type || main.type->kind != TypeKind::Function ||
            !main.type->base || main.type->base->kind != TypeKind::Int || !main.type->params.empty() ||
            main.type->variadic || !functions_.count(main.id)) throw std::runtime_error("入口必须是已定义的无参 int main");
        initializer_.quads = &program_.global_initializers;
        initializer_.locations = &program_.global_locations;
        initializer_.temporaries = &program_.global_temporaries;
        validate_code(initializer_, nullptr);
        for (const auto& function_ir : program_.functions) validate_code(functions_.at(function_ir.symbol_id), &function_ir);
    }

    TypePtr destination_type(const std::string& name, const Frame& frame) const {
        ScopeId owner = invalid_id;
        return target_type(name, *frame.code, owner);
    }

    Value read(const std::string& name, const Frame& frame) const {
        if (name.size() > 2 && name[1] == 'c') {
            const auto& constant = program_.constants.entries.at(operand_id(name, 'c'));
            if (const auto* value = constant_get_if<long long>(constant.value)) return *value;
            if (const auto* value = constant_get_if<unsigned long long>(constant.value)) return static_cast<long long>(*value);
            if (const auto* value = constant_get_if<double>(constant.value)) return *value;
            if (const auto* value = constant_get_if<std::string>(constant.value)) return *value;
            throw std::runtime_error("常量值无效");
        }
        const bool global = name.size() > 2 && name[1] == 's' && globals_.count(static_cast<SymbolId>(operand_id(name, 's')));
        const auto& values = global ? global_values_ : frame.values;
        const auto found = values.find(name);
        if (found == values.end() || value_empty(found->second))
            throw std::runtime_error("读取了尚未初始化的对象或临时量：" + name);
        return found->second;
    }

    Value initial_value(const TypePtr& type, bool zero) const {
        if (detail::aggregate(type)) {
            auto object = make_aggregate(); object->zero = zero; return object;
        }
        return zero ? convert_value(static_cast<long long>(0), type) : Value{};
    }

    Value& root_value(const Address& address) {
        if (!address.temporary.empty()) {
            for (auto& frame : frames_) if (frame.id == address.frame) {
                const auto total = detail::layout(frame.code->types.at(address.temporary), symbols_).size;
                const auto size = detail::layout(address.type, symbols_).size;
                if (address.last > total || address.offset < address.first || address.offset > address.last || size > address.last - address.offset)
                    throw std::runtime_error("临时对象地址越界");
                return frame.values.at(address.temporary);
            }
            throw std::runtime_error("临时对象所属调用帧已结束");
        }
        if (address.symbol == invalid_id) throw std::runtime_error("内部地址没有绑定对象");
        const auto& entry = symbol(address.symbol);
        if (!address.type) throw std::runtime_error("地址缺少所指类型");
        const auto total = detail::layout(entry.type, symbols_).size;
        const auto selected = detail::layout(address.type, symbols_).size;
        if (address.offset % detail::layout(address.type, symbols_).alignment) throw std::runtime_error("内部地址不满足类型对齐");
        if (address.last > total || address.offset < address.first || address.offset > address.last || selected > address.last - address.offset)
            throw std::runtime_error("地址超出所属对象边界");
        if (address.frame == 0 && globals_.count(entry.id))
            return global_values_.at(detail::symbol_name(entry.id));
        for (auto& frame : frames_) {
            if (frame.id != address.frame) continue;
            const auto found = frame.values.find(detail::symbol_name(entry.id));
            if (found == frame.values.end()) throw std::runtime_error("目标对象的声明尚未执行");
            return found->second;
        }
        throw std::runtime_error("地址指向已经结束的调用帧");
    }

    Value read_address(const Address& address) {
        if (address.literal) {
            if (!address.type || address.type->kind != TypeKind::Char || address.offset >= address.last)
                throw std::runtime_error("字符串地址越界或类型无效");
            const auto byte = address.offset == address.literal->size() ? 0 : static_cast<unsigned char>((*address.literal)[address.offset]);
            return static_cast<long long>(byte > 127 ? byte - 256 : byte);
        }
        auto& root = root_value(address);
        TypeInfo temporary_root; temporary_root.kind = TypeKind::Struct;
        const auto root_type = address.temporary.empty() ? symbol(address.symbol).type : &temporary_root;
        if (!detail::aggregate(root_type)) {
            if (value_empty(root)) throw std::runtime_error("读取尚未初始化的变量");
            if (!detail::same_unqualified(root_type, address.type)) {
                if (address.type->kind != TypeKind::Char || !detail::numeric(root_type)) throw std::runtime_error("内部地址类型与根对象不兼容");
                unsigned long long bits;
                if (detail::integral(root_type)) bits = static_cast<unsigned long long>(get<long long>(root));
                else if (root_type->kind == TypeKind::Float) { const auto value = static_cast<float>(get<double>(root)); unsigned word; std::memcpy(&word, &value, 4); bits = word; }
                else { const auto value = get<double>(root); std::memcpy(&bits, &value, 8); }
                const auto byte = static_cast<long long>((bits >> (8 * address.offset)) & 255);
                return address.type->is_unsigned || byte < 128 ? byte : byte - 256;
            }
            return root;
        }
        const auto object = get<Aggregate*>(root);
        if (detail::aggregate(address.type)) {
            auto result = make_aggregate(); result->zero = object->zero;
            const auto size = detail::layout(address.type, symbols_).size;
            for (const auto& cell : object->cells)
                if (cell.first >= address.offset && cell.first - address.offset < size)
                    result->cells.emplace(cell.first - address.offset, cell.second);
            for (const auto& byte : object->bytes)
                if (byte.first >= address.offset && byte.first - address.offset < size) result->bytes.emplace(byte.first - address.offset, byte.second);
            for (const auto offset : object->unknown)
                if (offset >= address.offset && offset - address.offset < size) result->unknown.insert(offset - address.offset);
            return result;
        }
        if (detail::numeric(address.type)) {
            const auto size = detail::layout(address.type, symbols_).size;
            for (const auto& cell : object->cells)
                if (cell.first < address.offset + size && cell.first + 8 > address.offset)
                    throw std::runtime_error("不能把地址信息读取为数值");
            unsigned long long bits = 0;
            for (std::size_t i = 0; i < size; ++i) {
                const auto byte = object->bytes.find(address.offset + i);
                if (object->unknown.count(address.offset + i) || (byte == object->bytes.end() && !object->zero)) throw std::runtime_error("读取尚未初始化的数值字节");
                if (object->cells.count(address.offset + i)) throw std::runtime_error("不能把地址信息读取为数值");
                bits |= static_cast<unsigned long long>(byte == object->bytes.end() ? 0 : byte->second) << (8 * i);
            }
            if (detail::floating(address.type)) {
                if (size == 4) { const auto word = static_cast<unsigned>(bits); float value; std::memcpy(&value, &word, 4); return convert_value(static_cast<double>(value), address.type); }
                double value; std::memcpy(&value, &bits, 8); return convert_value(value, address.type);
            }
            if (!address.type->is_unsigned && (bits & (static_cast<unsigned long long>(1) << (size * 8 - 1)))) return static_cast<long long>(bits) - static_cast<long long>(static_cast<unsigned long long>(1) << (size * 8));
            return static_cast<long long>(bits);
        }
        const auto cell = object->cells.find(address.offset);
        if (cell != object->cells.end()) {
            if (value_empty(cell->second)) throw std::runtime_error("读取尚未初始化的聚合子对象");
            return cell->second;
        }
        for (std::size_t i = 0; i < detail::layout(address.type, symbols_).size; ++i)
            if (object->bytes.count(address.offset + i) || object->unknown.count(address.offset + i)) throw std::runtime_error("不能把数值表示读取为内部地址");
        if (!object->zero) throw std::runtime_error("读取尚未初始化的数组元素或结构体成员");
        return convert_value(static_cast<long long>(0), address.type);
    }

    void store_address(const Address& address, const Value& value) {
        if (address.literal) throw std::runtime_error("不能修改字符串字面量");
        auto converted = convert_value(value, address.type);
        auto& root = root_value(address);
        if (address.temporary.empty() && !detail::aggregate(symbol(address.symbol).type)) {
            const auto root_type = symbol(address.symbol).type;
            if (!detail::same_unqualified(root_type, address.type)) {
                if (address.type->kind != TypeKind::Char || !detail::numeric(root_type) || value_empty(root))
                    throw std::runtime_error("地址写入类型不兼容或原对象未初始化");
                unsigned long long bits;
                if (detail::integral(root_type)) bits = static_cast<unsigned long long>(get<long long>(root));
                else if (root_type->kind == TypeKind::Float) { const auto value = static_cast<float>(get<double>(root)); unsigned word; std::memcpy(&word, &value, 4); bits = word; }
                else { const auto value = get<double>(root); std::memcpy(&bits, &value, 8); }
                bits = (bits & ~(static_cast<unsigned long long>(255) << (8 * address.offset))) | ((static_cast<unsigned long long>(get<long long>(converted)) & 255) << (8 * address.offset));
                if (detail::floating(root_type)) {
                    if (root_type->kind == TypeKind::Float) { const auto word = static_cast<unsigned>(bits); float value; std::memcpy(&value, &word, 4); root = convert_value(static_cast<double>(value), root_type); }
                    else { double value; std::memcpy(&value, &bits, 8); root = convert_value(value, root_type); }
                } else {
                    const auto size = detail::integer_bits(root_type);
                    bits &= (static_cast<unsigned long long>(1) << size) - 1;
                    root = !root_type->is_unsigned && (bits & (static_cast<unsigned long long>(1) << (size - 1))) ?
                        Value{static_cast<long long>(bits) - static_cast<long long>(static_cast<unsigned long long>(1) << size)} : Value{static_cast<long long>(bits)};
                }
                return;
            }
            root = std::move(converted); return;
        }
        auto object = get<Aggregate*>(root);
        if (!detail::aggregate(address.type)) {
            const auto size = detail::layout(address.type, symbols_).size;
            for (auto it = object->cells.begin(); it != object->cells.end();) {
                if (it->first < address.offset + size && it->first + 8 > address.offset) it = object->cells.erase(it); else ++it;
            }
            for (std::size_t i = 0; i < size; ++i) { object->bytes.erase(address.offset + i); object->unknown.erase(address.offset + i); }
            if (detail::numeric(address.type)) {
                unsigned long long bits;
                if (detail::integral(address.type)) bits = static_cast<unsigned long long>(get<long long>(converted));
                else if (size == 4) { const auto value = static_cast<float>(get<double>(converted)); unsigned word; std::memcpy(&word, &value, 4); bits = word; }
                else { const auto value = get<double>(converted); std::memcpy(&bits, &value, 8); }
                for (std::size_t i = 0; i < size; ++i) object->bytes[address.offset + i] = static_cast<unsigned char>(bits >> (8 * i));
            } else object->cells[address.offset] = std::move(converted);
            return;
        }
        const auto size = detail::layout(address.type, symbols_).size;
        auto source = get<Aggregate*>(converted);
        for (auto it = object->cells.begin(); it != object->cells.end();) {
            if (it->first >= address.offset && it->first - address.offset < size) it = object->cells.erase(it);
            else ++it;
        }
        for (auto it = object->bytes.begin(); it != object->bytes.end();) {
            if (it->first >= address.offset && it->first - address.offset < size) it = object->bytes.erase(it); else ++it;
        }
        for (auto it = object->unknown.begin(); it != object->unknown.end();) {
            if (*it >= address.offset && *it - address.offset < size) it = object->unknown.erase(it); else ++it;
        }
        if (address.offset == 0 && address.temporary.empty() && same_type(address.type, symbol(address.symbol).type)) object->zero = source->zero;
        else if (source->zero != object->zero) materialize_default(*source, address.type, 0);
        for (const auto& cell : source->cells) object->cells[address.offset + cell.first] = cell.second;
        for (const auto& byte : source->bytes) object->bytes[address.offset + byte.first] = byte.second;
        for (const auto offset : source->unknown) object->unknown.insert(address.offset + offset);
    }

    // 子聚合的默认状态与父对象不同时，显式保留每个数值叶子的状态。
    void materialize_default(Aggregate& object, const TypePtr& type, std::size_t offset, std::size_t depth = 0) {
        if (depth > 128) throw std::runtime_error("聚合对象过深");
        if (object.cells.size() + object.bytes.size() > 16 * 1024 * 1024) throw std::runtime_error("聚合对象拷贝超出存储限制");
        if (detail::numeric(type)) {
            const auto size = detail::layout(type, symbols_).size;
            if (object.zero) for (std::size_t i = 0; i < size; ++i) {
                if (!object.unknown.count(offset + i)) object.bytes.emplace(offset + i, 0);
            }
            else for (std::size_t i = 0; i < size; ++i) if (!object.bytes.count(offset + i)) object.unknown.insert(offset + i);
        } else if (type->kind == TypeKind::Address) {
            if (!object.cells.count(offset)) object.cells[offset] = object.zero ? convert_value(static_cast<long long>(0), type) : Value{};
        } else if (type->kind == TypeKind::Array) {
            const auto stride = detail::layout(type->base, symbols_).size;
            for (std::size_t i = 0; i < *type->array_length; ++i) materialize_default(object, type->base, offset + i * stride, depth + 1);
        } else {
            const auto& members = symbols_.records.at(type->record_id).members;
            for (const auto& member : members) materialize_default(object, member.type, offset + *member.offset, depth + 1);
        }
    }

    void write(const std::string& name, const Value& value, Frame& frame) {
        auto converted = convert_value(value, destination_type(name, frame));
        if (name.size() > 2 && name[1] == 's' && globals_.count(static_cast<SymbolId>(operand_id(name, 's'))))
            global_values_[name] = std::move(converted);
        else frame.values[name] = std::move(converted);
    }

    void write_address(const Address& address, const Value& value, const TypePtr& expected) {
        if (!address.type || !detail::same_unqualified(address.type, expected) || address.type->is_const) throw std::runtime_error("scanf 地址类型不匹配");
        store_address(address, value);
    }

    Value builtin(const SymbolEntry& entry, const std::vector<Value>& arguments) {
        if (arguments.empty()) throw std::runtime_error("输入输出缺少格式字符串");
        const bool scanning = entry.builtin == BuiltinKind::Scanf;
        const auto parts = detail::format_parts(string_value(arguments[0]), scanning);
        std::size_t conversions = 0;
        for (const auto& part : parts) if (part.conversion) ++conversions;
        if (arguments.size() != conversions + 1) throw std::runtime_error("格式串与实参数量不一致");
        std::size_t index = 1;
        std::size_t written = 0;
        for (const auto& part : parts) {
            if (!part.conversion) {
                if (scanning) {
                    for (const unsigned char character : part.text) {
                        if (std::isspace(character)) input_ >> std::ws;
                        else if (input_.get() != character) throw std::runtime_error("scanf 输入文字与格式串不匹配");
                    }
                } else { output_ << part.text; written += part.text.size(); }
                continue;
            }
            const auto& value = arguments[index++];
            if (scanning) {
                if (!holds<Address>(value)) throw std::runtime_error("scanf 实参必须是对象地址");
                TypeArena conversion_types;
                TypeArenaScope conversion_scope(conversion_types);
                Value input_value;
                const auto expected = detail::format_type(part, true);
                if (part.conversion == 's') {
                    auto address = get<Address>(value);
                    if (address.literal || !address.type || address.type->kind != TypeKind::Char || address.type->is_const)
                        throw std::runtime_error("scanf %s 需要可写字符数组");
                    const auto capacity = address.last - address.offset;
                    if (capacity < 1) throw std::runtime_error("scanf 字符数组没有容量");
                    input_ >> std::ws; std::string text;
                    const auto limit = capacity - 1;
                    while (input_.peek() != std::char_traits<char>::eof() && !std::isspace(static_cast<unsigned char>(input_.peek())) && text.size() < limit)
                        text.push_back(static_cast<char>(input_.get()));
                    if (text.empty()) throw std::runtime_error("scanf 无法读取字符串");
                    if (input_.peek() != std::char_traits<char>::eof() && !std::isspace(static_cast<unsigned char>(input_.peek())))
                        throw std::runtime_error("scanf 输入字符串超过数组容量");
                    for (const unsigned char byte : text) { store_address(address, static_cast<long long>(byte > 127 ? byte - 256 : byte)); ++address.offset; }
                    store_address(address, static_cast<long long>(0));
                    continue;
                } else if (part.conversion == 'd') {
                    long long integer = 0;
                    if (!(input_ >> integer)) throw std::runtime_error("scanf 无法读取整数");
                    input_value = convert_value(integer, expected);
                } else if (part.conversion == 'f') {
                    double floating = 0;
                    if (!(input_ >> floating)) throw std::runtime_error("scanf 无法读取浮点数");
                    input_value = convert_value(floating, expected);
                } else {
                    char character = 0;
                    if (!input_.get(character)) throw std::runtime_error("scanf 无法读取字符");
                    const auto byte = static_cast<unsigned char>(character);
                    input_value = static_cast<long long>(byte > 127 ? byte - 256 : byte);
                }
                write_address(get<Address>(value), input_value, expected);
            } else {
                std::ostringstream formatted;
                if (part.conversion == 's') {
                    formatted << string_value(value);
                } else if (part.conversion == 'f') {
                    if (!holds<double>(value)) throw std::runtime_error("%f 需要 float");
                    formatted << std::fixed << std::setprecision(6) << get<double>(value);
                } else {
                    if (!holds<long long>(value)) throw std::runtime_error("%d/%c 需要整数");
                    const auto integer = get<long long>(value);
                    if (part.conversion == 'c') formatted << static_cast<char>(static_cast<unsigned char>(integer));
                    else formatted << integer;
                }
                output_ << formatted.str();
                written += formatted.str().size();
            }
            if (!scanning && !output_) throw std::runtime_error("程序输出失败");
        }
        if (!scanning && !output_) throw std::runtime_error("程序输出失败");
        return detail::checked_integer(static_cast<long long>(scanning ? conversions : written));
    }

    // 沿字符数组地址读取到终止零，越界或未初始化时立即报错。
    std::string string_value(const Value& value) {
        if (const auto* text = get_if<std::string>(&value)) return text->substr(0, text->find('\0'));
        if (!holds<Address>(value)) throw std::runtime_error("需要字符数组地址");
        auto address = get<Address>(value); std::string text;
        if (!address.type || address.type->kind != TypeKind::Char) throw std::runtime_error("字符串地址类型错误");
        while (address.offset < address.last) {
            const auto byte = get<long long>(read_address(address));
            if (byte == 0) return text;
            text.push_back(static_cast<char>(static_cast<unsigned char>(byte))); ++address.offset;
        }
        throw std::runtime_error("字符串缺少终止零或读取越界");
    }

    void push(SymbolId function_id, const std::vector<Value>& arguments, const std::string& destination) {
        std::size_t depth = frames_.size();
        if (!frames_.empty() && frames_.front().code->function == invalid_id) --depth;
        if (options_.max_call_depth && depth >= options_.max_call_depth) throw std::runtime_error("函数调用深度超过上限");
        if (next_frame_ == 0) throw std::runtime_error("调用帧编号已用完");
        Frame frame;
        frame.id = next_frame_++;
        frame.code = &functions_.at(function_id);
        frame.destination = destination;
        const auto& parameters = program_.functions;
        for (const auto& ir_function : parameters) {
            if (ir_function.symbol_id != function_id) continue;
            if (ir_function.parameters.size() != arguments.size()) throw std::runtime_error("实参数量不一致");
            for (std::size_t i = 0; i < arguments.size(); ++i)
                frame.values[detail::symbol_name(ir_function.parameters[i])] = convert_value(arguments[i], symbol(ir_function.parameters[i]).type);
        }
        frames_.push_back(std::move(frame));
    }

    void execute() {
        for (const auto id : program_.globals)
            global_values_[detail::symbol_name(id)] = initial_value(symbol(id).type, true);
        Frame initial;
        initial.code = &initializer_;
        frames_.push_back(std::move(initial));
        while (!frames_.empty()) {
            auto& frame = frames_.back();
            if (frame.pc >= frame.code->quads->size()) {
                if (frame.code->function != invalid_id) throw std::runtime_error("非 void 函数执行到了没有 return 的末尾");
                frames_.pop_back();
                push(program_.entry_function, {}, "-");
                continue;
            }
            location_ = (*frame.code->locations)[frame.pc];
            if (options_.max_steps && result_.executed_steps >= options_.max_steps) throw std::runtime_error("执行步数超过上限");
            ++result_.executed_steps;
            const auto q = (*frame.code->quads)[frame.pc++];
            if (q.op == "label") continue;
            if (q.op == "local" || q.op == "zero") {
                const auto& entry = symbol(operand_id(q.result, 's'));
                auto value = initial_value(entry.type, q.op == "zero");
                if (globals_.count(entry.id)) global_values_[q.result] = std::move(value);
                else frame.values[q.result] = std::move(value);
                continue;
            }
            if (q.op == "jmp" || q.op == "jz" || q.op == "jnz") {
                const bool jump = q.op == "jmp" || (q.op == "jz" ? !truth(read(q.arg1, frame)) : truth(read(q.arg1, frame)));
                if (jump) frame.pc = frame.code->labels.at(q.result);
            } else if (q.op == "=" || q.op == "cvt" || q.op == "cvt_i2f") write(q.result, read(q.arg1, frame), frame);
            else if (q.op == "tempaddr") {
                const auto type = destination_type(q.arg1, frame);
                write(q.result, Address{frame.id, invalid_id, 0, type, 0, detail::layout(type, symbols_).size, {}, q.arg1}, frame);
            } else if (q.op == "addr") {
                const auto& entry = symbol(operand_id(q.arg1, 's'));
                write(q.result, Address{globals_.count(entry.id) ? 0 : frame.id, entry.id, 0, entry.type, 0, detail::layout(entry.type, symbols_).size, {}, {}}, frame);
            } else if (q.op == "decay" || q.op == "offsetaddr") {
                const auto value = read(q.arg1, frame);
                if (!holds<Address>(value)) throw std::runtime_error("数组形参寻址需要对象地址");
                auto selected = get<Address>(value);
                if (selected.symbol == invalid_id && !selected.literal && selected.temporary.empty()) throw std::runtime_error("空地址不能访问");
                if (q.op == "decay") {
                    root_value(selected);
                    selected.first = selected.offset;
                    selected.last = selected.offset + detail::layout(selected.type, symbols_).size;
                } else {
                    const auto index_value = read(q.arg2, frame);
                    if (!holds<long long>(index_value)) throw std::runtime_error("数组形参寻址需要整型下标");
                    const auto stride = detail::layout(selected.type, symbols_).size;
                    const auto delta = get<long long>(index_value) * static_cast<long long>(stride);
                    const auto offset = static_cast<long long>(selected.offset) + delta;
                    if (offset < static_cast<long long>(selected.first) || offset >= static_cast<long long>(selected.last))
                        throw std::runtime_error("数组形参寻址越界");
                    selected.offset = static_cast<std::size_t>(offset);
                }
                selected.type = destination_type(q.result, frame)->base;
                write(q.result, selected, frame);
            } else if (q.op == "indexaddr" || q.op == "memberaddr") {
                const auto base_value = read(q.arg1, frame);
                if (!holds<Address>(base_value)) throw std::runtime_error("寻址操作数不是对象地址");
                auto selected = get<Address>(base_value);
                root_value(selected);
                if (q.op == "indexaddr") {
                    const auto index_value = read(q.arg2, frame);
                    if (!holds<long long>(index_value) || !selected.type || selected.type->kind != TypeKind::Array)
                        throw std::runtime_error("数组寻址的下标或地址类型无效");
                    const auto index = get<long long>(index_value);
                    if (index < 0 || static_cast<std::size_t>(index) >= *selected.type->array_length)
                        throw std::runtime_error("数组下标越界");
                    selected.first = selected.offset;
                    selected.last = selected.offset + detail::layout(selected.type, symbols_).size;
                    selected.offset += static_cast<std::size_t>(index) * detail::layout(selected.type->base, symbols_).size;
                } else {
                    selected.offset += decimal(q.arg2); selected.first = selected.offset;
                    selected.last = selected.offset + detail::layout(destination_type(q.result, frame)->base, symbols_).size;
                }
                selected.type = destination_type(q.result, frame)->base;
                root_value(selected);
                write(q.result, selected, frame);
            } else if (q.op == "load") {
                const auto address_value = read(q.arg1, frame);
                if (!holds<Address>(address_value)) throw std::runtime_error("load 需要对象地址");
                write(q.result, read_address(get<Address>(address_value)), frame);
            } else if (q.op == "store") {
                const auto address_value = read(q.result, frame);
                if (!holds<Address>(address_value)) throw std::runtime_error("store 需要对象地址");
                store_address(get<Address>(address_value), read(q.arg1, frame));
            } else if (q.op == "arg") frame.arguments.push_back(read(q.arg1, frame));
            else if (q.op == "call") {
                const auto called = function(q.arg1).id;
                const auto& callee = symbol(called);
                auto arguments = std::move(frame.arguments);
                frame.arguments.clear();
                if (arguments.size() != decimal(q.arg2)) throw std::runtime_error("参数队列与 call 不一致");
                if (callee.builtin != BuiltinKind::None) {
                    const auto returned = builtin(callee, arguments);
                    if (q.result != "-") write(q.result, returned, frames_.back());
                } else push(callee.id, arguments, q.result);
            } else if (q.op == "ret") {
                const auto returned = q.arg1 == "-" ? Value{} :
                    convert_value(read(q.arg1, frame), symbol(frame.code->function).type->base);
                const auto destination = frame.destination;
                frames_.pop_back();
                if (frames_.empty()) {
                    result_.exit_code = static_cast<int>(get<long long>(returned));
                } else if (destination != "-") write(destination, returned, frames_.back());
            } else if (q.op == "neg" || q.op == "not" || q.op == "bnot") {
                const auto value = read(q.arg1, frame);
                write(q.result, q.op == "not" ? Value{static_cast<long long>(!truth(value))} :
                    q.op == "bnot" ? Value{~get<long long>(value)} :
                    holds<long long>(value) ? Value{-get<long long>(value)} : Value{-number(value)}, frame);
            } else {
                const auto left_value = read(q.arg1, frame);
                const auto right_value = read(q.arg2, frame);
                const double left = number(left_value), right = number(right_value);
                Value value;
                if (q.op == "<") value = static_cast<long long>(left < right);
                else if (q.op == "<=") value = static_cast<long long>(left <= right);
                else if (q.op == ">") value = static_cast<long long>(left > right);
                else if (q.op == ">=") value = static_cast<long long>(left >= right);
                else if (q.op == "==") value = static_cast<long long>(left == right);
                else if (q.op == "!=") value = static_cast<long long>(left != right);
                else if (holds<long long>(left_value) && holds<long long>(right_value)) {
                    const auto a = get<long long>(left_value), b = get<long long>(right_value);
                    const auto target = destination_type(q.result, frame);
                    if (target->is_unsigned && q.op == "*") {
                        const auto product = static_cast<unsigned long long>(a) * static_cast<unsigned long long>(b);
                        write(q.result, static_cast<long long>(product & ((static_cast<unsigned long long>(1) << detail::integer_bits(target)) - 1)), frame);
                        continue;
                    }
                    if ((q.op == "/" || q.op == "%") && b == 0) throw std::runtime_error("整数除零");
                    if ((q.op == "/" || q.op == "%") && a == std::numeric_limits<int>::min() && b == -1)
                        throw std::runtime_error("整数除法溢出");
                    if (q.op == "+") value = a + b;
                    else if (q.op == "-") value = a - b;
                    else if (q.op == "*") value = a * b;
                    else if (q.op == "/") value = a / b;
                    else if (q.op == "%") value = a % b;
                    else if (q.op == "&") value = a & b;
                    else if (q.op == "|") value = a | b;
                    else if (q.op == "^") value = a ^ b;
                    else if (q.op == "<<" || q.op == ">>") {
                        if (b < 0 || b >= detail::integer_bits(target) || (q.op == "<<" && a < 0)) throw std::runtime_error("移位数量或左移操作数无效");
                        const auto divisor = static_cast<long long>(1) << b;
                        if (target->is_unsigned && q.op == "<<") value = static_cast<long long>((static_cast<unsigned long long>(a) << b) & 0xffffffffu);
                        else value = q.op == "<<" ? a * divisor : (a >= 0 ? a / divisor : -((-a + divisor - 1) / divisor));
                    } else throw std::runtime_error("整型运算符无效");
                } else {
                    if (q.op == "+") value = left + right;
                    else if (q.op == "-") value = left - right;
                    else if (q.op == "*") value = left * right;
                    else if (q.op == "/") {
                        if (right == 0) throw std::runtime_error("浮点除零");
                        value = left / right;
                    } else throw std::runtime_error("浮点运算符无效");
                }
                write(q.result, value, frame);
            }
        }
    }

public:
    Machine(const IRProgram& program, const SymbolTableData& symbols, std::istream& input,
            std::ostream& output, const RunOptions& options)
        : program_(program), symbols_(symbols), input_(input), output_(output), options_(options) {}

    RunResult run() {
        try {
            validate(); // 执行任何程序输出前，先检查整份 IR。
            location_ = {};
            execute();
        } catch (const std::exception& exception) {
            result_.exit_code.reset();
            diagnostics_.report(Level::Error, location_, exception.what(), "RUN_FAILURE");
        }
        result_.diagnostics = diagnostics_.take_diagnostics();
        return std::move(result_);
    }
};

}

RunResult run(const IRProgram& program, const SymbolTableData& symbols,
              std::istream& input, std::ostream& output, const RunOptions& options) {
    TypeArena types;
    TypeArenaScope type_scope(types);
    return Machine(program, symbols, input, output, options).run();
}

}
