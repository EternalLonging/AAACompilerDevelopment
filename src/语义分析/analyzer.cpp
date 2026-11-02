#include "minic/semantic.hpp"
#include "minic/symbol_table.hpp"
#include "internal.hpp"

#include <algorithm>
#include <utility>
// 只分析当前解析器成功生成的新 AST；节点结构和树高由解析器保证。
namespace minic {
namespace {

class Analyzer {
    NodeArena* arena_ = nullptr; // 保存新增转换节点的池；总控传入 ParseResult.arena。
public:
    explicit Analyzer(NodeArena* arena = nullptr) : arena_(arena) {}
private:
    DiagnosticEngine diagnostics_{Phase::Semantic}; // 本阶段的报错列表。
    SymbolTable table_{diagnostics_}; // 名字和作用域信息。
    TypePtr return_type_ = nullptr; // 当前函数的返回类型。
    std::size_t loop_depth_ = 0; // 当前所在的循环层数。
    // 记录节点位置和错误码，把节点标成 Error；上层据此减少无意义的连锁报错。
    void error(ASTNode& node, const std::string& message, const std::string& code = "SEM_INVALID") {
        diagnostics_.report(Level::Error, node.range, message, code);
        node.type = detail::type(TypeKind::Error);
        node.category = ValueCategory::None;
    }

    // 判断表达式是否已有有效类型；返回 false 时不再继续依赖它做类型推导。
    bool valid_expression(const ASTNode& node) const {
        return node.type && node.type->kind != TypeKind::Error;
    }

    // 解析声明类型；结构体按标签查记录，数组继续解析元素类型。
    TypePtr resolve(const TypePtr& source, ASTNode& node) {
        if (source->kind == TypeKind::Struct) {
            const auto id = table_.lookup_tag(source->name);
            if (!id) {
                error(node, "结构体标签尚未声明：" + source->name, "SEM_TAG");
                return detail::type(TypeKind::Error);
            }
            auto result = make_type_info(*source);
            result->record_id = *id;
            return result;
        }
        if (source->kind == TypeKind::Array) {
            auto result = make_type_info(*source);
            result->base = resolve(source->base, node);
            return result;
        }
        return source;
    }
    // 判断数值、数组参数地址或同一结构体类型能否赋值；返回是否兼容。
    bool assignable(const TypePtr& target, const ASTNode& source) const {
        return can_assign(target, source.type) ||
               detail::address_compatible(target, source.type) ||
               (target->kind == TypeKind::Struct && same_type(target, source.type));
    }

    // 数组传参和字符串输入输出使用首元素地址；源码不提供指针类型。
    void decay(ASTNode*& node) {
        if (node->type->kind != TypeKind::Array) return;
        convert(node, detail::address_type(node->type->base));
    }
    // 登记结构体标签，按成员顺序计算偏移、总大小和对齐，再补齐记录表。
    void record_definition(ASTNode& node) {
        const auto id = table_.declare_record(node.name, RecordKind::Struct, node.range);
        if (!id) return;
        node.record_id = *id;
        auto type = make_type_info();
        type->kind = TypeKind::Struct;
        type->name = node.name;
        type->record_id = *id;
        node.type = type;
        auto definition = *table_.record(*id);
        std::size_t size = 0, alignment = 1;
        bool valid = true;
        for (auto& member : node.children) {
            member->scope_id = table_.current_scope();
            member->type = resolve(member->declared_type, *member);
            try {
                const auto layout = detail::layout(member->type, table_.data());
                size = detail::align_up(size, layout.alignment);
                if (layout.size > detail::max_object_size - size) throw std::runtime_error("结构体过大");
                definition.members.push_back({member->name, member->type, size, member->range});
                size = size + layout.size;
                alignment = std::max(alignment, layout.alignment);
            } catch (const std::runtime_error& exception) { error(*member, exception.what(), "SEM_LAYOUT"); valid = false; }
        }
        if (valid) {
            try {
                definition.size = detail::align_up(size, alignment);
                definition.alignment = alignment;
                if (!table_.complete_record(*id, std::move(definition))) node.type = detail::type(TypeKind::Error);
            } catch (const std::runtime_error& exception) { error(node, exception.what(), "SEM_LAYOUT"); }
        }
    }
    // 检查初始化值和目标类型；必要时插入转换，全局初值另做常量表达式检查。
    void initialize(ASTNode*& init, const TypePtr& target, bool global) {
        expression(*init);
        if (!valid_expression(*init)) return;

        if (target->kind == TypeKind::Array &&
            target->base->kind == TypeKind::Char &&
            init->kind == NodeType::StringLiteral) {
            if (constant_get<std::string>(init->value).size() > *target->array_length)
                error(*init, "字符串超过 char 数组长度", "SEM_INIT_COUNT");
            return;
        }

        if (global && !constant_initializer(*init)) {
            error(*init, "全局初始化必须是常量表达式", "SEM_GLOBAL_INIT");
            return;
        }

        if (target->kind == TypeKind::Struct && assignable(target, *init)) return;

        if (!assignable(target, *init)) {
            error(*init, "初始化类型不兼容", "SEM_INIT_TYPE");
            return;
        }

        convert(init, target);
    }
    // 全局初值只允许字面量及由它们组成的运算、隐式转换；返回是否满足。
    bool constant_initializer(const ASTNode& node) const {
        switch (node.kind) {
        case NodeType::IntLiteral: case NodeType::FloatLiteral:
        case NodeType::CharLiteral: case NodeType::StringLiteral:
            return true;
        case NodeType::BinaryOp: case NodeType::UnaryOp: case NodeType::ImplicitCast:
            return std::all_of(node.children.begin(), node.children.end(),
                [this](const ASTNode* child) { return constant_initializer(*child); });
        default:
            return false;
        }
    }

    // 在表达式外包一层隐式转换，并改写调用者持有的节点指针；新节点由 arena_ 管理。
    void convert(ASTNode*& node, const TypePtr& target) {
        if (same_type(node->type, target)) return;
        auto cast = arena_->make();
        cast->kind = NodeType::ImplicitCast;
        cast->range = node->range;
        cast->scope_id = node->scope_id;
        cast->declared_type = target;
        cast->type = target;
        cast->category = ValueCategory::RValue;
        cast->children.push_back(node);
        node = cast;
    }
    // 检查 if/循环的条件表达式；仅接受数值条件，零和非零的分支行为留给 IR。
    void require_condition(ASTNode& node) {
        expression(node);
        if (valid_expression(node) && !detail::numeric(node.type))
            error(node, "条件必须是数值", "SEM_CONDITION");
    }
    // 把字面量拼写解码成值，检查范围并填写类型；字符串类型含末尾零字节。
    void literal(ASTNode& node) {
        try {
            if (node.kind == NodeType::StringLiteral || node.kind == NodeType::CharLiteral) {
                const auto text = detail::decode_string(node.name,
                    node.kind == NodeType::StringLiteral ? '"' : '\'');
                if (node.kind == NodeType::StringLiteral) {
                    auto type = make_type_info();
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
                const auto value = std::stoull(node.name, nullptr, 0);
                if (value > static_cast<unsigned long long>(std::numeric_limits<int>::max()))
                    throw std::runtime_error("整数常量超出 32 位目标范围");
                node.type = detail::type(TypeKind::Int);
                node.value = static_cast<long long>(value);
            } else {
                const auto value = std::stod(node.name);
                if (!std::isfinite(value)) throw std::runtime_error("浮点常量必须有限");
                node.value = detail::checked_float(value);
                node.type = detail::type(TypeKind::Float);
            }
            node.category = ValueCategory::RValue;
        } catch (const std::exception& exception) {
            error(node, exception.what(), "SEM_LITERAL");
        }
    }
    // 从当前作用域向外查名字，把符号编号、类型和左值/函数类别写回 AST。
    void identifier(ASTNode& node) {
        const auto id = table_.lookup(node.name);
        if (!id) { error(node, "名字尚未声明：" + node.name, "SEM_UNDECLARED"); return; }
        const auto* entry = table_.symbol(*id);
        node.symbol_id = *id;
        node.type = entry->type;
        node.category = entry->kind == SymbolKind::Function ? ValueCategory::Function : ValueCategory::LValue;
    }
    // 检查两侧表达式和运算符，插入必要转换并确定结果类型；比较和逻辑结果为 int。
    void binary(ASTNode& node) {
        expression(*node.children[0]);
        expression(*node.children[1]);
        auto& left = node.children[0];
        auto& right = node.children[1];
        if (!valid_expression(*left) || !valid_expression(*right)) { node.type = detail::type(TypeKind::Error); return; }
        const auto common = arithmetic_result(left->type, right->type);
        const bool compare = node.name == "<" || node.name == "<=" || node.name == ">" ||
                             node.name == ">=" || node.name == "==" || node.name == "!=";
        const bool logic = node.name == "&&" || node.name == "||";
        if (common->kind == TypeKind::Error ||
            (node.name == "%" && detail::floating(common))) {
            error(node, "运算符或操作数类型不受支持", "SEM_OPERANDS"); return;
        }
        if (!logic) { convert(left, common); convert(right, common); }
        node.type = compare || logic ? detail::type(TypeKind::Int) : common;
        node.category = ValueCategory::RValue;
    }
    // 检查左边能否被赋值以及右边类型是否允许，再给右边插入隐式转换。
    void assignment(ASTNode& node) {
        expression(*node.children[0]);
        expression(*node.children[1]);
        auto& left = node.children[0];
        auto& right = node.children[1];
        if (!valid_expression(*left) || !valid_expression(*right)) { node.type = detail::type(TypeKind::Error); return; }
        if (left->category == ValueCategory::LValue && left->type->kind == TypeKind::Struct &&
            assignable(left->type, *right)) {
            node.type = left->type; node.category = ValueCategory::RValue; return;
        }
        if (left->category != ValueCategory::LValue || !detail::numeric(left->type)) {
            error(node, "赋值目标必须是可修改的数值变量", "SEM_LVALUE"); return;
        }
        if (!can_assign(left->type, right->type)) { error(node, "赋值需要不允许的隐式类型转换", "SEM_ASSIGN_TYPE"); return; }
        convert(right, left->type);
        node.type = left->type;
        node.category = ValueCategory::RValue;
    }
    // 检查 +、-、! 和 scanf 专用的 &，填写结果类型。
    void unary(ASTNode& node, bool address_allowed = false) {
        auto& child = node.children[0];
        expression(*child);
        if (!valid_expression(*child)) { node.type = detail::type(TypeKind::Error); return; }
        if (node.name == "&") {
            if (!address_allowed) { error(node, "& 只允许用在 scanf 的输入参数中", "SEM_ADDRESS"); return; }
            if (child->category != ValueCategory::LValue || !detail::numeric(child->type)) {
                error(node, "scanf 取地址需要可修改的数值对象", "SEM_ADDRESS"); return;
            }
            node.type = detail::address_type(child->type);
        } else {
            if (!detail::numeric(child->type)) { error(node, "一元运算需要数值类型", "SEM_OPERANDS"); return; }
            const auto promoted = arithmetic_result(child->type, child->type);
            convert(child, promoted);
            node.type = node.name == "!" ? detail::type(TypeKind::Int) : promoted;
        }
        node.category = ValueCategory::RValue;
    }
    // 检查被调函数、参数数量和类型；printf/scanf 还要按格式串逐项检查参数。
    void call(ASTNode& node) {
        expression(*node.children[0], true);
        const auto& callee = *node.children[0];
        if (!valid_expression(callee)) { node.type = detail::type(TypeKind::Error); return; }
        if (callee.kind != NodeType::Identifier || callee.category != ValueCategory::Function) { error(node, "调用需要函数名字", "SEM_CALL"); return; }
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
                const auto parts = detail::format_parts(constant_get<std::string>(node.children[1]->value), scanning);
                std::size_t index = 2;
                for (const auto& part : parts) {
                    if (!part.conversion) continue;
                    if (index >= node.children.size()) throw std::runtime_error("格式转换符多于实参数量");
                    auto& argument = node.children[index++];
                    if (!valid_expression(*argument)) continue;
                    const auto expected = detail::format_type(part, scanning);
                    if (part.conversion == 's') decay(argument);
                    if (scanning) {
                        if (argument->type->kind != TypeKind::Address ||
                            !same_type(argument->type->base, expected))
                            throw std::runtime_error("scanf 实参地址类型与格式转换符不匹配");
                    } else if (part.conversion == 's') {
                        if (argument->type->kind != TypeKind::Address || argument->type->base->kind != TypeKind::Char)
                            throw std::runtime_error("%s 需要字符数组或字符串");
                    } else {
                        if (!detail::numeric(argument->type) ||
                            (part.conversion == 'f' ? !detail::floating(argument->type) : !detail::integral(argument->type)))
                            throw std::runtime_error("printf 实参类型与格式转换符不匹配");
                        if (part.conversion != 'f') convert(argument, part.conversion == 'c' ? detail::type(TypeKind::Int) : expected);
                    }
                }
                if (index != node.children.size()) throw std::runtime_error("实参数量多于格式转换符数量");
                convert(node.children[1], entry.type->params[0]);
            } catch (const std::exception& exception) { error(node, exception.what(), "SEM_FORMAT"); return; }
        } else {
            if (node.children.size() - 1 != entry.type->params.size()) {
                error(node, "函数实参数量与签名不一致", "SEM_ARGUMENT_COUNT"); return;
            }
            for (std::size_t i = 1; i < node.children.size(); ++i) {
                if (!valid_expression(*node.children[i])) continue;
                decay(node.children[i]);
                const auto& target = entry.type->params[i - 1];
                if (!assignable(target, *node.children[i])) error(node, "函数实参类型不兼容", "SEM_ARGUMENT_TYPE");
                else convert(node.children[i], target);
            }
        }
        if (node.type && node.type->kind == TypeKind::Error) return;
        node.type = entry.type->base;
        node.category = ValueCategory::RValue;
    }
    // 按节点种类分派表达式检查，写回类型和符号信息；允许函数名的场合由参数指定。
    void expression(ASTNode& node, bool callee_allowed = false) {
        if (diagnostics_.should_stop()) return;
        node.scope_id = table_.current_scope();
        switch (node.kind) {
        case NodeType::Identifier: identifier(node); break;
        case NodeType::IntLiteral: case NodeType::FloatLiteral:
        case NodeType::CharLiteral: case NodeType::StringLiteral: literal(node); break;
        case NodeType::BinaryOp: binary(node); break;
        case NodeType::Assign: assignment(node); break;
        case NodeType::UnaryOp: unary(node); break;
        case NodeType::Call: call(node); break;
        case NodeType::ArrayAccess: {
            expression(*node.children[0]); expression(*node.children[1]);
            const auto& array = *node.children[0];
            const auto& index = *node.children[1];
            if (!valid_expression(array) || !valid_expression(index)) { node.type = detail::type(TypeKind::Error); break; }
            if ((array.type->kind != TypeKind::Array && array.type->kind != TypeKind::Address) || !detail::numeric(index.type) || detail::floating(index.type)) {
                error(node, "下标访问需要数组和整型下标", "SEM_INDEX"); break;
            }
            try { detail::layout(array.type->base, table_.data()); }
            catch (const std::runtime_error& exception) { error(node, exception.what(), "SEM_INDEX"); break; }
            node.type = array.type->base;
            node.category = array.type->kind == TypeKind::Address || array.category == ValueCategory::LValue ? ValueCategory::LValue : ValueCategory::RValue;
            convert(node.children[1], detail::type(TypeKind::Int));
            break;
        }
        case NodeType::MemberAccess: {
            expression(*node.children[0]);
            const auto& object = *node.children[0];
            if (!valid_expression(object)) { node.type = detail::type(TypeKind::Error); break; }
            if (object.type->kind != TypeKind::Struct) { error(node, "成员访问需要结构体对象", "SEM_MEMBER"); break; }
            const auto index = table_.find_member(object.type->record_id, node.name);
            if (!index) { error(node, "结构体中没有该成员：" + node.name, "SEM_MEMBER"); break; }
            node.record_id = object.type->record_id;
            node.member_index = *index;
            node.type = table_.record(node.record_id)->members[*index].type;
            node.category = object.category;
            break;
        }
        default: error(node, "此表达式节点尚未支持，或语法树已被分析过", "SEM_UNSUPPORTED"); break;
        }
        if (node.category == ValueCategory::Function && !callee_allowed)
            error(node, "函数名字只能用于直接调用", "SEM_FUNCTION_VALUE");
    }
    // 解析变量类型、登记符号并检查初值；无长度的字符数组可从字符串推导长度。
    void variable(ASTNode& node, bool global) {
        auto resolved = resolve(node.declared_type, node);
        if (resolved->kind == TypeKind::Array && !resolved->array_length && !node.children.empty()) {
            auto inferred = make_type_info(*resolved);
            if (node.children[0]->kind == NodeType::StringLiteral && inferred->base->kind == TypeKind::Char) {
                try { inferred->array_length = detail::decode_string(node.children[0]->name, '"').size() + 1; }
                catch (const std::runtime_error&) {}
            }
            resolved = inferred;
        }
        try { detail::layout(resolved, table_.data()); }
        catch (const std::runtime_error& exception) { error(node, exception.what(), "SEM_DECL_TYPE"); return; }
        SymbolEntry entry;
        entry.name = node.name;
        entry.type = resolved;
        entry.kind = resolved->kind == TypeKind::Array ? SymbolKind::Array : SymbolKind::Variable;
        entry.range = node.range;
        entry.is_defined = !node.children.empty();
        const auto id = table_.declare_object(std::move(entry));
        if (!id) { node.type = detail::type(TypeKind::Error); return; }
        node.symbol_id = *id;
        node.type = table_.symbol(*id)->type;
        if (!node.children.empty()) {
            initialize(node.children[0], node.type, global);
        }
    }

    // 检查语句和内部作用域；返回 true 表示已确认所有路径返回，用来检查非 void 函数。
    bool statement(ASTNode& node, bool function_body = false) {
        if (diagnostics_.should_stop()) return false;
        node.scope_id = table_.current_scope();
        bool returned = false;
        switch (node.kind) {
        case NodeType::Block:
            if (!function_body) node.scope_id = table_.enter_scope(ScopeKind::Block, node.range);
            for (auto& child : node.children) {
                if (diagnostics_.should_stop()) break;
                returned = statement(*child) || returned;
            }
            if (!function_body) table_.exit_scope();
            break;
        case NodeType::VarDecl: variable(node, false); break;
        case NodeType::StructDef: record_definition(node); break;
        case NodeType::ExprStmt:
            expression(*node.children[0]);
            break;
        case NodeType::Return:
            if (node.children.empty()) {
                if (return_type_->kind != TypeKind::Void) error(node, "非 void 函数必须返回一个值", "SEM_RETURN");
            } else {
                expression(*node.children[0]);
                if (return_type_->kind == TypeKind::Void) error(node, "void 函数不能返回一个值", "SEM_RETURN");
                else if (valid_expression(*node.children[0])) {
                    if (!assignable(return_type_, *node.children[0])) error(node, "返回值类型不兼容", "SEM_RETURN");
                    else convert(node.children[0], return_type_);
                }
            }
            returned = true;
            break;
        case NodeType::If: {
            require_condition(*node.children[0]);
            const bool yes = statement(*node.children[1]);
            const bool no = node.children.size() == 3 && statement(*node.children[2]);
            returned = yes && no;
            break;
        }
        case NodeType::While: case NodeType::For:
            if (node.kind == NodeType::For) {
                node.scope_id = table_.enter_scope(ScopeKind::Block, node.range);
                auto& init = *node.children[0];
                init.scope_id = node.scope_id;
                if (init.kind != NodeType::Empty) expression(init);
                if (node.children[1]->kind != NodeType::Empty) require_condition(*node.children[1]);
                auto& update = *node.children[2];
                update.scope_id = node.scope_id;
                if (update.kind != NodeType::Empty) expression(update);
            } else require_condition(*node.children[0]);
            ++loop_depth_;
            statement(*node.children.back());
            --loop_depth_;
            if (node.kind == NodeType::For) table_.exit_scope();
            break;
        case NodeType::Break: case NodeType::Continue:
            if (loop_depth_ == 0)
                error(node, "break 和 continue 只能用在循环中", "SEM_LOOP");
            break;
        case NodeType::Empty: break;
        default: error(node, "此语句节点尚未支持", "SEM_UNSUPPORTED"); break;
        }
        return returned;
    }
    // 从形参节点建立函数类型，先登记函数以支持递归，再检查函数体和返回路径。
    void function(ASTNode& node) {
        auto type = make_type_info();
        type->kind = TypeKind::Function;
        type->base = node.declared_type->base;
        for (std::size_t i = 0; i + 1 < node.children.size(); ++i) {
            auto& parameter = *node.children[i];
            auto resolved = resolve(parameter.declared_type, parameter);
            if (resolved->kind == TypeKind::Error) return;
            if (resolved->kind == TypeKind::Array)
                resolved = detail::address_type(resolved->base);
            parameter.type = resolved;
            type->params.push_back(resolved);
        }
        SymbolEntry entry;
        entry.name = node.name;
        entry.kind = SymbolKind::Function;
        entry.type = type;
        entry.range = node.range;
        entry.is_defined = true;
        const auto id = table_.insert(std::move(entry));
        if (!id) { node.type = detail::type(TypeKind::Error); return; }
        node.symbol_id = *id;
        node.type = type;
        node.scope_id = table_.enter_scope(ScopeKind::Function, node.range);
        return_type_ = type->base;
        for (std::size_t i = 0; i < type->params.size(); ++i) {
            auto& parameter = *node.children[i];
            parameter.scope_id = node.scope_id;
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
        return_type_ = nullptr;
    }

public:
    // 遍历解析器给出的顶层声明，返回符号表和诊断；类型等标注直接写回 AST。
    SemanticResult analyze(Program& program) {
        static NodeArena fallback_arena;
        if (!arena_) arena_ = &fallback_arena;
        register_builtins(table_);
        program.scope_id = 0;
        for (auto& child : program.children) {
            if (diagnostics_.should_stop()) break;
            child->scope_id = 0;
            switch (child->kind) {
            case NodeType::VarDecl: variable(*child, true); break;
            case NodeType::StructDef: record_definition(*child); break;
            case NodeType::FunctionDef: function(*child); break;
            default: break;
            }
        }
        SemanticResult result;
        result.symbols = std::move(table_).release();
        result.diagnostics = diagnostics_.take_diagnostics();
        return result;
    }
};

}
// 为本次调用新建检查器；修改 AST 标注，返回符号表和诊断。
SemanticResult analyze(Program& program, NodeArena* arena) {
    return Analyzer(arena).analyze(program);
}

}
