#include "minic/ir.hpp"
#include "minic/constant_pool.hpp"
#include "minic/diagnostic.hpp"
#include "minic/semantic.hpp"
#include "internal.hpp"

#include <utility>
#include <algorithm>

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
    SourceRange current_range_; // 生成失败时的源码位置。

    const ASTNode& child(const ASTNode& node, std::size_t index) const {
        return *node.children[index];
    }

    const SymbolEntry& binding(const ASTNode& node) const {
        return symbols_.symbols[node.symbol_id];
    }

    void emit(const std::string& op, const std::string& first, const std::string& second,
              const std::string& result, const SourceRange& range) {
        quads_->push_back({op, first, second, result});
        locations_->push_back(range);
    }

    std::string temporary(TypePtr type) {
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
    std::string address(const ASTNode& node) {
        const auto result = temporary(detail::address_type(node.type));
        if (node.kind == NodeType::Identifier) {
            emit("addr", detail::symbol_name(node.symbol_id), "-", result, node.range);
        } else if (node.kind == NodeType::ArrayAccess) {
            const bool array_parameter = child(node, 0).type->kind == TypeKind::Address;
            const auto base = array_parameter ? snapshot(expression(child(node, 0)), detail::value_type(child(node, 0)), node.range) : address(child(node, 0));
            const auto index = expression(child(node, 1));
            emit(array_parameter ? "offsetaddr" : "indexaddr", base, index, result, node.range);
        } else if (node.kind == NodeType::MemberAccess) {
            const auto& member = symbols_.records[node.record_id].members[*node.member_index];
            const auto base = address(child(node, 0));
            emit("memberaddr", base, std::to_string(*member.offset), result, node.range);
        } else if (detail::aggregate(node.type) && node.category == ValueCategory::RValue) {
            const auto value = snapshot(expression_value(node), node.type, node.range);
            emit("tempaddr", value, "-", result, node.range);
        } else throw std::runtime_error("此表达式不能求对象地址");
        return result;
    }

    void initialize(const std::string& destination, const TypePtr& target, const ASTNode& init) {
        if (target->kind == TypeKind::Array && init.kind == NodeType::StringLiteral) {
            const auto& text = constant_get<std::string>(init.value);
            for (std::size_t i = 0; i < text.size(); ++i) {
                const auto selected = temporary(detail::address_type(target->base));
                emit("indexaddr", destination, integer(static_cast<long long>(i)), selected, init.range);
                const auto byte = static_cast<unsigned char>(text[i]);
                const auto character = byte <= 127 ? static_cast<long long>(byte) : static_cast<long long>(byte) - 256;
                const auto byte_value = constant_operand(constants_.intern(detail::type(TypeKind::Char), character, std::to_string(character), init.range));
                emit("store", byte_value, "-", selected, init.range);
            }
        } else {
            emit("store", expression(init), "-", destination, init.range);
        }
    }
    // 先生成原表达式，再按节点上的目标类型生成转换；数组取首元素地址。
    std::string expression(const ASTNode& node) {
        if (node.converted_type && node.type->kind == TypeKind::Array &&
            node.kind != NodeType::StringLiteral) {
            current_range_ = node.range;
            const auto result = temporary(node.converted_type);
            emit("decay", address(node), "-", result, node.range);
            return result;
        }
        if (options_.constant_folding && node.converted_type) {
            if (const auto value = detail::folded_value(node))
                return constant_operand(constants_.intern(node.converted_type, *value, "<折叠常量>", node.range));
        }
        const auto source = expression_value(node);
        if (!node.converted_type) return source;
        const auto result = temporary(node.converted_type);
        const bool i2f = node.type->kind == TypeKind::Int && node.converted_type->kind == TypeKind::Float;
        emit(i2f ? "cvt_i2f" : "cvt", source, "-", result, node.range);
        return result;
    }
    // 按原类型生成表达式值；不修改 AST，也不在这里应用本节点的转换目标。
    std::string expression_value(const ASTNode& node) {
        current_range_ = node.range;
        std::string result;
        // 仅折叠无副作用且不会溢出/除零的数值常量表达式。
        if (options_.constant_folding && (node.kind == NodeType::BinaryOp || node.kind == NodeType::UnaryOp)) {
            if (const auto value = detail::folded_value(node, 0, false)) {
                return constant_operand(constants_.intern(node.type, *value, "<折叠常量>", node.range));
            }
        }
        switch (node.kind) {
        case NodeType::ArrayAccess: case NodeType::MemberAccess:
            result = temporary(node.type);
            emit("load", address(node), "-", result, node.range);
            break;
        case NodeType::Identifier:
            result = detail::symbol_name(node.symbol_id);
            break;
        case NodeType::IntLiteral: case NodeType::FloatLiteral:
        case NodeType::CharLiteral: case NodeType::StringLiteral:
            result = constant_operand(constants_.intern(node.type, node.value, node.name, node.range));
            break;
        case NodeType::BinaryOp: {
            const auto left = expression(child(node, 0));
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
                const auto saved = snapshot(left, detail::value_type(child(node, 0)), node.range);
                const auto right = expression(child(node, 1));
                emit(node.name, saved, right, result, node.range);
            }
            break;
        }
        case NodeType::Assign: {
            const auto& target = child(node, 0);
            const auto destination = address(target);
            const auto source = expression(child(node, 1));
            emit("store", source, "-", destination, node.range);
            result = temporary(node.type);
            emit("load", destination, "-", result, node.range);
            break;
        }
        case NodeType::UnaryOp: {
            const auto& operand = child(node, 0);
            if (node.name == "&") { result = address(operand); break; }
            const auto source = expression(operand);
            if (node.name == "+") { result = source; break; }
            result = temporary(node.type);
            emit(node.name == "-" ? "neg" : "not", source, "-", result, node.range);
            break;
        }
        case NodeType::Call: {
            const auto& callee = child(node, 0);
            const auto target = detail::symbol_name(binding(callee).id);
            std::vector<std::string> arguments;
            for (std::size_t i = 1; i < node.children.size(); ++i) {
                const auto value = expression(child(node, i));
                // 实参逐个快照，嵌套调用在外层 arg 之前完成。
                arguments.push_back(snapshot(value, detail::value_type(child(node, i)), child(node, i).range));
            }
            for (const auto& value : arguments) emit("arg", value, "-", "-", node.range);
            result = node.type->kind == TypeKind::Void ? "-" : temporary(node.type);
            emit("call", target, std::to_string(arguments.size()), result, node.range);
            break;
        }
        default: throw std::runtime_error("此表达式不能生成 M1 四元式");
        }
        return result;
    }

    void statement(const ASTNode& node) {
        current_range_ = node.range;
        switch (node.kind) {
        case NodeType::Block:
            for (const auto& value : node.children) statement(*value);
            break;
        case NodeType::VarDecl: {
            const auto& entry = binding(node);
            const auto destination = detail::symbol_name(entry.id);
            if (entry.scope != 0) emit("local", "-", "-", destination, node.range);
            if (!node.children.empty()) {
                const auto selected = temporary(detail::address_type(node.type));
                emit("addr", destination, "-", selected, node.range);
                if (detail::aggregate(node.type) && child(node, 0).kind == NodeType::StringLiteral)
                    emit("zero", "-", "-", destination, node.range);
                initialize(selected, node.type, child(node, 0));
            }
            break;
        }
        case NodeType::StructDef: break;
        case NodeType::ExprStmt:
            expression(child(node, 0)); break;
        case NodeType::Return:
            emit("ret", node.children.empty() ? "-" : expression(child(node, 0)), "-", "-", node.range);
            break;
        case NodeType::If: {
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
            if (node.kind == NodeType::Break) emit("jmp", "-", "-", loops_.back().first, node.range);
            else emit("jmp", "-", "-", loops_.back().second, node.range);
            break;
        case NodeType::Empty: break;
        default: throw std::runtime_error("此语句不能生成 M1 四元式");
        }
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
            for (const auto& child_node : root.children) {
                const auto& node = *child_node;
                current_range_ = node.range;
                if (node.kind == NodeType::VarDecl) {
                    if (std::find(program_.globals.begin(), program_.globals.end(), node.symbol_id) == program_.globals.end()) program_.globals.push_back(node.symbol_id);
                    statement(node);
                } else if (node.kind == NodeType::StructDef) {
                    continue; // 成员布局已由语义分析保存，不生成运行指令。
                } else if (node.kind == NodeType::FunctionDef) {
                    const auto& entry = binding(node);
                    IRFunction function;
                    function.symbol_id = node.symbol_id;
                    for (std::size_t i = 0; i < entry.type->params.size(); ++i) {
                        const auto& parameter = child(node, i);
                        function.parameters.push_back(parameter.symbol_id);
                    }
                    quads_ = &function.quads;
                    locations_ = &function.locations;
                    temporaries_ = &function.temporaries;
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
