#include "minic/ir.hpp"
#include "minic/constant_pool.hpp"
#include "minic/diagnostic.hpp"
#include "minic/semantic.hpp"
#include "internal.hpp"

#include <utility>

namespace minic {
namespace {

class Generator {
    const SymbolTableData& symbols_; // 本次语义分析的符号信息。
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

    std::string integer(std::int64_t value) {
        return constant_operand(constants_.intern(detail::type(TypeKind::Int), value, std::to_string(value)));
    }

    std::string snapshot(const std::string& source, const TypePtr& type, const SourceRange& range) {
        const auto value = temporary(type);
        emit("=", source, "-", value, range);
        return value;
    }

    std::string expression(const ASTNode& node) {
        current_range_ = node.range;
        if (++depth_ > 512) throw std::runtime_error("语法树嵌套过深");
        if (!node.type || node.type->kind == TypeKind::Unknown || node.type->kind == TypeKind::Error ||
            node.scope_id >= symbols_.scopes.size()) throw std::runtime_error("表达式尚未通过语义检查");
        std::string result;
        switch (node.kind) {
        case NodeType::Identifier:
            count(node, 0, 0);
            binding(node);
            result = detail::symbol_name(node.symbol_id);
            break;
        case NodeType::IntLiteral: case NodeType::FloatLiteral:
        case NodeType::CharLiteral: case NodeType::StringLiteral:
            count(node, 0, 0);
            result = constant_operand(constants_.intern(node.type, node.value, node.name, node.range));
            break;
        case NodeType::ImplicitCast: case NodeType::Cast: {
            count(node, 1, 1);
            const auto source = expression(child(node, 0));
            result = temporary(node.type);
            const bool i2f = node.type->kind == TypeKind::Float && child(node, 0).type->kind == TypeKind::Int;
            emit(i2f ? "cvt_i2f" : "cvt", source, "-", result, node.range);
            break;
        }
        case NodeType::BinaryOp: {
            count(node, 2, 2);
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
                const auto saved = snapshot(left, child(node, 0).type, node.range);
                const auto right = expression(child(node, 1));
                emit(node.name, saved, right, result, node.range);
            }
            break;
        }
        case NodeType::Assign: {
            count(node, 2, 2);
            const auto& target = child(node, 0);
            if (target.kind != NodeType::Identifier || target.category != ValueCategory::LValue)
                throw std::runtime_error("M1 赋值目标必须绑定普通变量");
            binding(target);
            const auto destination = detail::symbol_name(target.symbol_id);
            std::string original;
            if (node.name != "=") original = snapshot(destination, target.type, node.range);
            const auto source = expression(child(node, 1));
            if (node.name == "=") emit("=", source, "-", destination, node.range);
            else {
                if (node.name != "+=" && node.name != "-=" && node.name != "*=" && node.name != "/=")
                    throw std::runtime_error("不支持的复合赋值");
                const auto value = temporary(node.type);
                emit(node.name.substr(0, 1), original, source, value, node.range);
                emit("=", value, "-", destination, node.range);
            }
            result = snapshot(destination, node.type, node.range);
            break;
        }
        case NodeType::UnaryOp: {
            count(node, 1, 1);
            const auto& operand = child(node, 0);
            const auto source = expression(operand);
            if (node.name == "+") { result = source; break; }
            result = temporary(node.type);
            if (node.name == "&") emit("addr", source, "-", result, node.range);
            else if (node.name == "-" || node.name == "!")
                emit(node.name == "-" ? "neg" : "not", source, "-", result, node.range);
            else if (node.name == "pre++" || node.name == "post++" || node.name == "pre--" || node.name == "post--") {
                if (operand.kind != NodeType::Identifier) throw std::runtime_error("自增自减目标必须是变量");
                if (node.name.compare(0, 4, "post") == 0) emit("=", source, "-", result, node.range);
                const auto updated = temporary(node.type);
                emit(node.name.find("++") != std::string::npos ? "+" : "-", source, integer(1), updated, node.range);
                emit("=", updated, "-", source, node.range);
                if (node.name.compare(0, 3, "pre") == 0) emit("=", source, "-", result, node.range);
            } else throw std::runtime_error("不支持的一元运算");
            break;
        }
        case NodeType::Call: {
            count(node, 1, invalid_id);
            const auto& callee = child(node, 0);
            if (callee.kind != NodeType::Identifier || binding(callee).kind != SymbolKind::Function)
                throw std::runtime_error("M1 调用目标必须是函数符号");
            std::vector<std::string> arguments;
            for (std::size_t i = 1; i < node.children.size(); ++i) {
                const auto value = expression(child(node, i));
                // 实参逐个快照，嵌套调用在外层 arg 之前完成。
                arguments.push_back(snapshot(value, child(node, i).type, child(node, i).range));
            }
            for (const auto& value : arguments) emit("arg", value, "-", "-", node.range);
            result = node.type->kind == TypeKind::Void ? "-" : temporary(node.type);
            emit("call", detail::symbol_name(callee.symbol_id), std::to_string(arguments.size()), result, node.range);
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
            if (!node.children.empty()) emit("=", expression(child(node, 0)), "-", destination, node.range);
            break;
        }
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
            emit("jmp", "-", "-", node.kind == NodeType::Break ? loops_.back().first : loops_.back().second, node.range);
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
    explicit Generator(const SymbolTableData& symbols) : symbols_(symbols) {}

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
                    program_.globals.push_back(node.symbol_id);
                    statement(node);
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
    return Generator(symbols).generate(program);
}

}
