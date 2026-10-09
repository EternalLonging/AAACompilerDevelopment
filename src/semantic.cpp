#include "minic/semantic.hpp"
#include "minic/symbol_table.hpp"
#include "internal.hpp"

#include <regex>
#include <utility>

namespace minic {
namespace {

class Analyzer {
    DiagnosticEngine diagnostics_{Phase::Semantic}; // 本阶段的报错列表。
    SymbolTable table_{diagnostics_}; // 名字和作用域信息。
    TypePtr return_type_; // 当前函数的返回类型。
    std::size_t loop_depth_ = 0; // 当前所在的循环层数。
    std::size_t depth_ = 0; // 防止过深的语法树耗尽调用栈。
    std::vector<std::pair<SymbolId, SourceRange>> calls_; // 调用过的普通函数。

    void error(ASTNode& node, const std::string& message, const std::string& code = "SEM_INVALID") {
        diagnostics_.report(Level::Error, node.range, message, code);
        node.type = detail::type(TypeKind::Error);
        node.category = ValueCategory::None;
    }

    bool shape(ASTNode& node, std::size_t low, std::size_t high) {
        if (node.children.size() < low || node.children.size() > high) {
            error(node, "语法树节点的孩子数量不符合接口约定", "SEM_AST_SHAPE");
            return false;
        }
        for (const auto& child : node.children) {
            if (!child) { error(node, "语法树含有空孩子", "SEM_AST_SHAPE"); return false; }
        }
        return true;
    }

    bool valid_expression(const ASTNode& node) const {
        return node.type && node.type->kind != TypeKind::Error;
    }

    bool constant_initializer(const ASTNode& node) const {
        if (node.kind == NodeType::IntLiteral || node.kind == NodeType::FloatLiteral || node.kind == NodeType::CharLiteral)
            return true;
        if (node.kind != NodeType::BinaryOp && node.kind != NodeType::Cast && node.kind != NodeType::ImplicitCast &&
            !(node.kind == NodeType::UnaryOp && (node.name == "+" || node.name == "-" || node.name == "!"))) return false;
        for (const auto& child : node.children) if (!child || !constant_initializer(*child)) return false;
        return true;
    }

    // 在需要提升的表达式外包一层隐式转换。
    void convert(std::unique_ptr<ASTNode>& node, const TypePtr& target) {
        if (same_type(node->type, target)) return;
        auto cast = std::make_unique<ASTNode>();
        cast->kind = NodeType::ImplicitCast;
        cast->range = node->range;
        cast->scope_id = node->scope_id;
        cast->declared_type = target;
        cast->type = target;
        cast->category = ValueCategory::RValue;
        cast->children.push_back(std::move(node));
        node = std::move(cast);
    }

    void require_condition(ASTNode& node) {
        expression(node);
        if (valid_expression(node) && !detail::numeric(node.type))
            error(node, "条件必须是 int、float 或 char", "SEM_CONDITION");
    }

    void literal(ASTNode& node) {
        if (!shape(node, 0, 0)) return;
        try {
            if (node.kind == NodeType::StringLiteral || node.kind == NodeType::CharLiteral) {
                const auto text = detail::decode_string(node.name,
                    node.kind == NodeType::StringLiteral ? '"' : '\'');
                if (node.kind == NodeType::StringLiteral) {
                    auto type = std::make_shared<TypeInfo>();
                    type->kind = TypeKind::Array;
                    type->base = detail::type(TypeKind::Char);
                    type->array_length = text.size() + 1;
                    node.type = type;
                    node.value = text;
                } else {
                    if (text.size() != 1) throw std::runtime_error("M1 字符常量必须恰好包含一个字节");
                    node.value = detail::checked_integer(static_cast<unsigned char>(text[0]), TypeKind::Char);
                    node.type = detail::type(TypeKind::Char);
                }
            } else if (node.kind == NodeType::IntLiteral) {
                static const std::regex pattern(R"((0[xX][0-9a-fA-F]+|0[0-7]*|[1-9][0-9]*))");
                if (!std::regex_match(node.name, pattern)) throw std::runtime_error("M1 整数常量不支持该写法或后缀");
                const auto value = std::stoll(node.name, nullptr, 0);
                node.value = detail::checked_integer(value);
                node.type = detail::type(TypeKind::Int);
            } else {
                static const std::regex pattern(R"(((\d+\.\d*|\.\d+)([eE][+-]?\d+)?|\d+[eE][+-]?\d+)[fF]?)");
                if (!std::regex_match(node.name, pattern)) throw std::runtime_error("M1 浮点常量写法不受支持");
                node.value = detail::checked_float(std::stod(node.name));
                node.type = detail::type(TypeKind::Float);
            }
            node.category = ValueCategory::RValue;
        } catch (const std::exception& exception) {
            error(node, exception.what(), "SEM_LITERAL");
        }
    }

    void identifier(ASTNode& node) {
        if (!shape(node, 0, 0)) return;
        const auto id = table_.lookup(node.name);
        if (!id) { error(node, "名字尚未声明：" + node.name, "SEM_UNDECLARED"); return; }
        const auto* entry = table_.symbol(*id);
        node.symbol_id = *id;
        node.type = entry->type;
        node.category = entry->kind == SymbolKind::Function ? ValueCategory::Function : ValueCategory::LValue;
    }

    void binary(ASTNode& node) {
        if (!shape(node, 2, 2)) return;
        expression(*node.children[0]);
        expression(*node.children[1]);
        auto& left = node.children[0];
        auto& right = node.children[1];
        if (!valid_expression(*left) || !valid_expression(*right)) { node.type = detail::type(TypeKind::Error); return; }
        const auto common = arithmetic_result(left->type, right->type);
        const bool compare = node.name == "<" || node.name == "<=" || node.name == ">" ||
                             node.name == ">=" || node.name == "==" || node.name == "!=";
        const bool logic = node.name == "&&" || node.name == "||";
        const bool arithmetic = node.name == "+" || node.name == "-" || node.name == "*" ||
                                node.name == "/" || node.name == "%";
        if (common->kind == TypeKind::Error || (!compare && !logic && !arithmetic) ||
            (node.name == "%" && common->kind == TypeKind::Float)) {
            error(node, "运算符或操作数类型不受支持", "SEM_OPERANDS"); return;
        }
        if (!logic) { convert(left, common); convert(right, common); }
        node.type = compare || logic ? detail::type(TypeKind::Int) : common;
        node.category = ValueCategory::RValue;
    }

    void assignment(ASTNode& node) {
        if (!shape(node, 2, 2)) return;
        expression(*node.children[0]);
        expression(*node.children[1]);
        auto& left = node.children[0];
        auto& right = node.children[1];
        if (!valid_expression(*left) || !valid_expression(*right)) { node.type = detail::type(TypeKind::Error); return; }
        if (left->category != ValueCategory::LValue || left->type->is_const || !detail::numeric(left->type)) {
            error(node, "赋值目标必须是可修改的数值变量", "SEM_LVALUE"); return;
        }
        const bool compound = node.name == "+=" || node.name == "-=" || node.name == "*=" || node.name == "/=";
        if (node.name != "=" && !compound) { error(node, "不支持的赋值运算符"); return; }
        auto source = compound ? arithmetic_result(left->type, right->type) : right->type;
        if (!can_assign(left->type, source)) { error(node, "赋值需要不允许的隐式类型转换", "SEM_ASSIGN_TYPE"); return; }
        convert(right, left->type);
        node.type = detail::type(left->type->kind);
        node.category = ValueCategory::RValue;
    }

    void unary(ASTNode& node, bool address_allowed = false) {
        if (!shape(node, 1, 1)) return;
        expression(*node.children[0]);
        auto& child = node.children[0];
        if (!valid_expression(*child)) { node.type = detail::type(TypeKind::Error); return; }
        if (node.name == "&") {
            if (!address_allowed || child->kind != NodeType::Identifier ||
                child->category != ValueCategory::LValue || !detail::numeric(child->type) || child->type->is_const) {
                error(node, "M1 取地址仅允许用于 scanf 的可修改变量实参", "SEM_ADDRESS"); return;
            }
            auto pointer = std::make_shared<TypeInfo>();
            pointer->kind = TypeKind::Pointer;
            pointer->base = child->type;
            node.type = pointer;
        } else {
            if (!detail::numeric(child->type)) { error(node, "一元运算需要数值类型"); return; }
            const bool update = node.name == "pre++" || node.name == "post++" ||
                                node.name == "pre--" || node.name == "post--";
            if (update) {
                if (child->category != ValueCategory::LValue || child->type->is_const) {
                    error(node, "自增或自减需要可修改变量", "SEM_LVALUE"); return;
                }
                node.type = detail::type(child->type->kind);
            } else if (node.name == "+" || node.name == "-" || node.name == "!") {
                const auto promoted = arithmetic_result(child->type, child->type);
                convert(child, promoted);
                node.type = node.name == "!" ? detail::type(TypeKind::Int) : promoted;
            } else { error(node, "M1 不支持该一元运算符"); return; }
        }
        node.category = ValueCategory::RValue;
    }

    void call(ASTNode& node) {
        if (!shape(node, 1, invalid_id)) return;
        expression(*node.children[0]);
        const auto& callee = *node.children[0];
        if (!valid_expression(callee)) { node.type = detail::type(TypeKind::Error); return; }
        if (callee.kind != NodeType::Identifier || callee.category != ValueCategory::Function ||
            callee.type->kind != TypeKind::Function) { error(node, "M1 只支持直接调用已声明函数", "SEM_CALL"); return; }
        const auto entry = *table_.symbol(callee.symbol_id);
        node.symbol_id = callee.symbol_id;
        const bool builtin = entry.builtin != BuiltinKind::None;
        const bool scanning = entry.builtin == BuiltinKind::Scanf;
        for (std::size_t i = 1; i < node.children.size(); ++i) {
            auto& child = *node.children[i];
            child.scope_id = table_.current_scope();
            if (scanning && i > 1 && child.kind == NodeType::UnaryOp && child.name == "&") unary(child, true);
            else expression(child);
        }
        if (builtin) {
            if (node.children.size() < 2 || node.children[1]->kind != NodeType::StringLiteral ||
                !valid_expression(*node.children[1])) { error(node, "M1 输入输出需要字面量格式串", "SEM_FORMAT"); return; }
            try {
                const auto parts = detail::format_parts(std::get<std::string>(node.children[1]->value), scanning);
                std::size_t index = 2;
                for (const auto& part : parts) {
                    if (!part.conversion) continue;
                    if (index >= node.children.size()) throw std::runtime_error("格式转换符多于实参数量");
                    auto& argument = node.children[index++];
                    if (!valid_expression(*argument)) continue;
                    const auto expected = part.conversion == 'f' ? TypeKind::Float :
                                          part.conversion == 'c' ? TypeKind::Char : TypeKind::Int;
                    if (scanning) {
                        if (argument->kind != NodeType::UnaryOp || argument->name != "&" ||
                            argument->type->kind != TypeKind::Pointer || argument->type->base->kind != expected)
                            throw std::runtime_error("scanf 实参地址类型与格式转换符不匹配");
                    } else if (part.conversion == 's') {
                        if (argument->kind != NodeType::StringLiteral) throw std::runtime_error("M1 的 %s 只支持窄字符串字面量");
                        auto pointer = std::make_shared<TypeInfo>();
                        pointer->kind = TypeKind::Pointer;
                        pointer->base = detail::type(TypeKind::Char);
                        convert(argument, pointer);
                    } else {
                        if (!detail::numeric(argument->type) ||
                            (part.conversion == 'f' ? argument->type->kind != TypeKind::Float : argument->type->kind == TypeKind::Float))
                            throw std::runtime_error("printf 实参类型与格式转换符不匹配");
                        if (part.conversion != 'f') convert(argument, detail::type(TypeKind::Int));
                    }
                }
                if (index != node.children.size()) throw std::runtime_error("实参数量多于格式转换符数量");
                convert(node.children[1], entry.type->params[0]);
            } catch (const std::exception& exception) { error(node, exception.what(), "SEM_FORMAT"); return; }
        } else {
            calls_.push_back({entry.id, node.range});
            if (node.children.size() - 1 != entry.type->params.size()) {
                error(node, "函数实参数量与原型不一致", "SEM_ARGUMENT_COUNT"); return;
            }
            for (std::size_t i = 1; i < node.children.size(); ++i) {
                if (!valid_expression(*node.children[i])) continue;
                const auto& target = entry.type->params[i - 1];
                if (!can_assign(target, node.children[i]->type)) error(node, "函数实参类型不兼容", "SEM_ARGUMENT_TYPE");
                else convert(node.children[i], target);
            }
        }
        if (node.type && node.type->kind == TypeKind::Error) return;
        node.type = entry.type->base;
        node.category = ValueCategory::RValue;
    }

    void expression(ASTNode& node) {
        if (diagnostics_.should_stop()) return;
        node.scope_id = table_.current_scope();
        if (++depth_ > 512) {
            diagnostics_.report(Level::Fatal, node.range, "语法树嵌套过深", "SEM_DEPTH");
            --depth_; return;
        }
        switch (node.kind) {
        case NodeType::Identifier: identifier(node); break;
        case NodeType::IntLiteral: case NodeType::FloatLiteral:
        case NodeType::CharLiteral: case NodeType::StringLiteral: literal(node); break;
        case NodeType::BinaryOp: binary(node); break;
        case NodeType::Assign: assignment(node); break;
        case NodeType::UnaryOp: unary(node); break;
        case NodeType::Call: call(node); break;
        case NodeType::Cast:
            if (shape(node, 1, 1)) {
                expression(*node.children[0]);
                if (detail::numeric(node.declared_type) && detail::numeric(node.children[0]->type)) {
                    node.type = detail::type(node.declared_type->kind);
                    node.category = ValueCategory::RValue;
                } else error(node, "M1 显式转换仅支持基本数值类型");
            }
            break;
        default: error(node, "此表达式节点尚未支持，或语法树已被分析过", "SEM_UNSUPPORTED"); break;
        }
        --depth_;
    }

    void variable(ASTNode& node, bool global) {
        if (!shape(node, 0, 1)) return;
        if (!detail::numeric(node.declared_type) ||
            (node.storage != StorageClass::None && !( !global &&
             (node.storage == StorageClass::Auto || node.storage == StorageClass::Register)))) {
            error(node, "M1 变量仅支持基本数值类型和自动存储", "SEM_DECL_TYPE"); return;
        }
        SymbolEntry entry;
        entry.name = node.name;
        entry.type = std::make_shared<TypeInfo>(*node.declared_type);
        entry.range = node.range;
        entry.storage = node.storage;
        entry.is_defined = true;
        const auto id = table_.insert(std::move(entry));
        if (!id) { node.type = detail::type(TypeKind::Error); return; }
        node.symbol_id = *id;
        node.type = table_.symbol(*id)->type;
        if (!node.children.empty()) {
            expression(*node.children[0]);
            if (global && !constant_initializer(*node.children[0]))
                error(node, "M1 全局初始化必须是数值常量表达式", "SEM_GLOBAL_INIT");
            if (valid_expression(*node.children[0])) {
                if (node.type->kind == TypeKind::Error) return;
                if (!can_assign(node.type, node.children[0]->type)) error(node, "初始化类型不兼容", "SEM_INIT_TYPE");
                else convert(node.children[0], detail::type(node.type->kind));
            }
        } else if (node.type->is_const) error(node, "const 变量必须初始化", "SEM_CONST_INIT");
    }

    // 返回 true 表示所有路径都已经返回。
    bool statement(ASTNode& node, bool function_body = false) {
        if (diagnostics_.should_stop()) return false;
        node.scope_id = table_.current_scope();
        if (++depth_ > 512) {
            diagnostics_.report(Level::Fatal, node.range, "语法树嵌套过深", "SEM_DEPTH");
            --depth_; return false;
        }
        bool returned = false;
        switch (node.kind) {
        case NodeType::Block:
            if (shape(node, 0, invalid_id)) {
                if (!function_body) node.scope_id = table_.enter_scope(ScopeKind::Block, node.range);
                for (auto& child : node.children) {
                    if (diagnostics_.should_stop()) break;
                    returned = statement(*child) || returned;
                }
                if (!function_body) table_.exit_scope();
            }
            break;
        case NodeType::VarDecl: variable(node, false); break;
        case NodeType::ExprStmt:
            if (shape(node, 1, 1)) expression(*node.children[0]);
            break;
        case NodeType::Return:
            if (shape(node, 0, 1)) {
                if (node.children.empty()) {
                    if (return_type_->kind != TypeKind::Void) error(node, "非 void 函数必须返回一个值", "SEM_RETURN");
                } else {
                    expression(*node.children[0]);
                    if (return_type_->kind == TypeKind::Void) error(node, "void 函数不能返回一个值", "SEM_RETURN");
                    else if (valid_expression(*node.children[0])) {
                        if (!can_assign(return_type_, node.children[0]->type)) error(node, "返回值类型不兼容", "SEM_RETURN");
                        else convert(node.children[0], return_type_);
                    }
                }
                returned = true;
            }
            break;
        case NodeType::If:
            if (shape(node, 2, 3)) {
                require_condition(*node.children[0]);
                const bool yes = statement(*node.children[1]);
                const bool no = node.children.size() == 3 && statement(*node.children[2]);
                returned = yes && no;
            }
            break;
        case NodeType::While: case NodeType::For:
            if (shape(node, node.kind == NodeType::While ? 2 : 4, node.kind == NodeType::While ? 2 : 4)) {
                if (node.kind == NodeType::For) {
                    node.scope_id = table_.enter_scope(ScopeKind::Block, node.range);
                    auto& init = *node.children[0];
                    if (init.kind == NodeType::ExprStmt || init.kind == NodeType::VarDecl || init.kind == NodeType::Empty) statement(init);
                    else expression(init);
                    if (node.children[1]->kind != NodeType::Empty) require_condition(*node.children[1]);
                    else shape(*node.children[1], 0, 0);
                    auto& update = *node.children[2];
                    if (update.kind == NodeType::ExprStmt || update.kind == NodeType::Empty) statement(update);
                    else expression(update);
                } else require_condition(*node.children[0]);
                ++loop_depth_;
                statement(*node.children.back());
                --loop_depth_;
                if (node.kind == NodeType::For) table_.exit_scope();
            }
            break;
        case NodeType::Break: case NodeType::Continue:
            if (shape(node, 0, 0) && loop_depth_ == 0) error(node, "break/continue 只能用于循环", "SEM_LOOP");
            break;
        case NodeType::Empty: shape(node, 0, 0); break;
        default: error(node, "此语句节点尚未支持", "SEM_UNSUPPORTED"); break;
        }
        --depth_;
        return returned;
    }

    void function(ASTNode& node) {
        if (!shape(node, 0, invalid_id)) return;
        const bool definition = node.kind == NodeType::FunctionDef;
        auto declared = node.declared_type;
        if (!declared || declared->kind != TypeKind::Function ||
            !(detail::numeric(declared->base) || (declared->base && declared->base->kind == TypeKind::Void)) ||
            declared->variadic || node.storage != StorageClass::None ||
            (!declared->has_prototype && !declared->params.empty()) ||
            node.children.size() != declared->params.size() + (definition ? 1 : 0)) {
            error(node, "M1 函数类型或形参列表无效", "SEM_FUNCTION"); return;
        }
        for (std::size_t i = 0; i < declared->params.size(); ++i) {
            const auto& parameter = *node.children[i];
            if (parameter.kind != NodeType::ParamDecl || !parameter.children.empty() ||
                !detail::numeric(declared->params[i]) || !same_type(parameter.declared_type, declared->params[i]) ||
                parameter.storage != StorageClass::None || (definition && parameter.name.empty())) {
                error(node, "函数形参节点与签名不一致", "SEM_PARAMETER"); return;
            }
        }
        if (definition && node.children.back()->kind != NodeType::Block) { error(node, "函数定义最后一个孩子必须是 Block"); return; }
        // M1 将空参数 f() 按无参函数处理，完整 C 的未指定参数规则留待扩展。
        auto normalized = std::make_shared<TypeInfo>(*declared);
        normalized->has_prototype = true;
        SymbolEntry entry;
        entry.name = node.name;
        entry.kind = SymbolKind::Function;
        entry.type = normalized;
        entry.range = node.range;
        entry.is_defined = definition;
        const auto id = table_.insert(std::move(entry));
        if (!id) { node.type = detail::type(TypeKind::Error); return; }
        node.symbol_id = *id;
        node.type = normalized;
        if (!definition) return;
        node.scope_id = table_.enter_scope(ScopeKind::Function, node.range);
        return_type_ = normalized->base;
        for (std::size_t i = 0; i < declared->params.size(); ++i) {
            auto& parameter = *node.children[i];
            parameter.scope_id = node.scope_id;
            parameter.type = normalized->params[i];
            SymbolEntry symbol;
            symbol.name = parameter.name;
            symbol.kind = SymbolKind::Parameter;
            symbol.type = parameter.type;
            symbol.range = parameter.range;
            symbol.is_defined = true;
            const auto parameter_id = table_.insert(std::move(symbol));
            if (parameter_id) parameter.symbol_id = *parameter_id;
            else parameter.type = detail::type(TypeKind::Error);
        }
        const bool returned = statement(*node.children.back(), true);
        if (!returned && return_type_->kind != TypeKind::Void)
            error(node, "非 void 函数必须保证所有路径返回；请在末尾添加 return", "SEM_MISSING_RETURN");
        table_.exit_scope();
        return_type_.reset();
    }

public:
    SemanticResult analyze(Program& program) {
        if (program.kind != NodeType::Program) {
            diagnostics_.report(Level::Fatal, program.range, "语义分析需要 Program 根节点", "SEM_ROOT");
        } else if (shape(program, 0, invalid_id)) {
            register_builtins(table_);
            program.scope_id = 0;
            for (auto& child : program.children) {
                if (diagnostics_.should_stop()) break;
                child->scope_id = 0;
                if (child->kind == NodeType::VarDecl) variable(*child, true);
                else if (child->kind == NodeType::FunctionDecl || child->kind == NodeType::FunctionDef) function(*child);
                else error(*child, "M1 顶层仅支持变量和函数声明", "SEM_UNSUPPORTED");
            }
            for (const auto& call : calls_) {
                if (!table_.symbol(call.first)->is_defined)
                    diagnostics_.report(Level::Error, call.second, "被调用函数没有定义：" + table_.symbol(call.first)->name, "SEM_UNDEFINED_FUNCTION");
            }
        }
        SemanticResult result;
        result.symbols = std::move(table_).release();
        result.diagnostics = diagnostics_.take_diagnostics();
        return result;
    }
};

}

SemanticResult analyze(Program& program) { return Analyzer{}.analyze(program); }

}
