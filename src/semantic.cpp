#include "minic/semantic.hpp"
#include "minic/symbol_table.hpp"
#include "internal.hpp"

#include <regex>
#include <algorithm>
#include <utility>
#include <unordered_set>

namespace minic {
namespace {

class Analyzer {
    DiagnosticEngine diagnostics_{Phase::Semantic}; // 本阶段的报错列表。
    SymbolTable table_{diagnostics_}; // 名字和作用域信息。
    TypePtr return_type_; // 当前函数的返回类型。
    std::size_t loop_depth_ = 0; // 当前所在的循环层数。
    std::size_t depth_ = 0; // 防止过深的语法树耗尽调用栈。
    struct SwitchContext {
        std::unordered_set<std::int64_t> values; // 已使用的 case 常量。
        bool has_default = false; // 是否已有 default。
    };
    std::vector<SwitchContext> switches_; // 当前嵌套的 switch。
    std::unordered_map<std::string, SourceRange> labels_; // 当前函数的标签名。
    std::vector<std::pair<std::string, SourceRange>> gotos_; // 当前函数待检查的 goto。
    std::vector<std::pair<SymbolId, SourceRange>> calls_; // 调用过的普通函数。
    std::vector<std::pair<SymbolId, SourceRange>> object_uses_; // 使用过的外部对象，结束时检查是否有本文件定义。

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

    bool modifiable(const TypePtr& type, std::size_t depth = 0) const {
        if (!type || type->is_const || depth > 128) return false;
        if (type->kind == TypeKind::Array) return modifiable(type->base, depth + 1);
        if ((type->kind == TypeKind::Struct || type->kind == TypeKind::Union)) {
            const auto* record = table_.record(type->record_id);
            if (!record || !record->is_complete) return false;
            for (const auto& member : record->members) if (!modifiable(member.type, depth + 1)) return false;
        }
        return true;
    }

    TypePtr resolve(const TypePtr& source, ASTNode& node, std::size_t depth = 0) {
        if (!source || depth > 128) { error(node, "声明类型为空或嵌套过深", "SEM_DECL_TYPE"); return detail::type(TypeKind::Error); }
        auto result = std::make_shared<TypeInfo>(*source);
        if (source->kind == TypeKind::Named) {
            const auto id = table_.lookup(source->name);
            const auto* alias = id ? table_.symbol(*id) : nullptr;
            if (!alias || alias->kind != SymbolKind::Typedef) {
                error(node, "类型别名尚未声明：" + source->name, "SEM_TYPEDEF"); return detail::type(TypeKind::Error);
            }
            result = std::make_shared<TypeInfo>(*alias->type);
            result->is_const = result->is_const || source->is_const;
            result->is_volatile = result->is_volatile || source->is_volatile;
        } else if (source->kind == TypeKind::Enum) {
            const auto id = table_.lookup_tag(source->name);
            const auto* record = id ? table_.record(*id) : nullptr;
            if (!record || record->kind != RecordKind::Enum || !record->is_complete) {
                error(node, "枚举类型尚未定义", "SEM_TAG"); return detail::type(TypeKind::Error);
            }
            result->kind = TypeKind::Int; result->record_id = invalid_id; result->name.clear();
        } else if ((source->kind == TypeKind::Struct || source->kind == TypeKind::Union)) {
            const auto id = table_.lookup_tag(source->name);
            const auto record_id = source->record_id == invalid_id && id ? *id : source->record_id;
            const auto* record = table_.record(record_id);
            if (!record || record->kind != (source->kind == TypeKind::Struct ? RecordKind::Struct : RecordKind::Union)) {
                error(node, "结构体标签尚未声明：" + source->name, "SEM_TAG"); return detail::type(TypeKind::Error);
            }
            result->record_id = record_id;
            result->name = record->tag;
        } else if (source->kind == TypeKind::Array || source->kind == TypeKind::Pointer) {
            result->base = resolve(source->base, node, depth + 1);
        } else if (source->kind == TypeKind::Function) {
            result->base = resolve(source->base, node, depth + 1);
            for (auto& parameter : result->params) {
                parameter = resolve(parameter, node, depth + 1);
                if (parameter->kind == TypeKind::Array) parameter = detail::pointer(parameter->base);
                else if (parameter->kind == TypeKind::Function) parameter = detail::pointer(parameter);
                parameter = detail::unqualified(parameter);
            }
        } else if (!detail::numeric(source) && source->kind != TypeKind::Void) {
            error(node, "声明类型尚未支持", "SEM_DECL_TYPE"); return detail::type(TypeKind::Error);
        }
        return result;
    }

    bool assignable(const TypePtr& target, const ASTNode& source) const {
        if (can_assign(target, source.type) || detail::pointer_assign(target, source.type)) return true;
        if (target && target->kind == TypeKind::Pointer && target->base && target->base->kind == TypeKind::Char && source.kind == NodeType::StringLiteral) return true;
        if (target && target->kind == TypeKind::Pointer && detail::integer_constant(source) == 0) return true;
        return target && source.type && (target->kind == TypeKind::Struct || target->kind == TypeKind::Union) &&
            same_type(detail::unqualified(target), detail::unqualified(source.type));
    }

    // 普通数组在需要值时转成首元素指针；sizeof 和取地址保留数组类型。
    void decay(std::unique_ptr<ASTNode>& node) {
        if (node->type && node->type->kind == TypeKind::Function) { convert(node, detail::pointer(node->type)); return; }
        if (!node->type || node->type->kind != TypeKind::Array) return;
        if (node->kind == NodeType::Identifier && table_.symbol(node->symbol_id)->storage == StorageClass::Register) {
            error(*node, "register 数组不能转成地址", "SEM_ADDRESS"); return;
        }
        auto element = std::make_shared<TypeInfo>(*node->type->base);
        element->is_const = element->is_const || node->type->is_const;
        element->is_volatile = element->is_volatile || node->type->is_volatile;
        convert(node, detail::pointer(element));
    }

    void typedef_declaration(ASTNode& node) {
        if (!shape(node, 0, 0)) return;
        node.type = resolve(node.declared_type, node);
        if (!valid_expression(node)) return;
        SymbolEntry entry; entry.name = node.name; entry.kind = SymbolKind::Typedef;
        entry.type = node.type; entry.range = node.range; entry.storage = StorageClass::Typedef;
        const auto id = table_.insert(std::move(entry));
        if (id) node.symbol_id = *id;
    }

    void enum_definition(ASTNode& node) {
        if (!shape(node, 1, invalid_id)) return;
        const auto id = table_.declare_record(node.name, RecordKind::Enum, node.range);
        if (!id) return;
        if (table_.record(*id)->is_complete) { error(node, "枚举已经定义", "SEM_ENUM"); return; }
        node.record_id = *id;
        auto definition = *table_.record(*id);
        definition.size = 4; definition.alignment = 4;
        std::int64_t next = 0;
        bool valid = true;
        for (auto& child : node.children) {
            child->scope_id = table_.current_scope();
            if (child->kind != NodeType::EnumMember || !shape(*child, 0, 1)) { error(*child, "枚举定义需要 EnumMember", "SEM_ENUM"); valid = false; continue; }
            if (!child->children.empty()) {
                expression(*child->children[0]);
                const auto value = detail::integer_constant(*child->children[0]);
                if (!value) { error(*child, "枚举值必须是整型常量表达式", "SEM_ENUM"); valid = false; continue; }
                next = *value;
            }
            if (next < std::numeric_limits<std::int32_t>::min() || next > std::numeric_limits<std::int32_t>::max()) { error(*child, "枚举自动编号溢出", "SEM_ENUM"); valid = false; continue; }
            child->type = detail::type(TypeKind::Int); child->value = next;
            SymbolEntry entry; entry.name = child->name; entry.kind = SymbolKind::EnumConstant;
            entry.type = child->type; entry.range = child->range; entry.is_defined = true;
            const auto symbol = table_.insert(std::move(entry));
            if (!symbol) { valid = false; continue; }
            child->symbol_id = *symbol;
            definition.enumerators.push_back({child->name, next, *symbol, child->range});
            ++next;
            // 提前保存已登记枚举常量，后续枚举表达式可引用前面的项。
            enum_values_[*symbol] = std::get<std::int64_t>(child->value);
        }
        if (valid) table_.complete_record(*id, std::move(definition));
    }

    std::unordered_map<SymbolId, std::int64_t> enum_values_; // 枚举常量的数值。

    void record_definition(ASTNode& node) {
        if (!shape(node, 0, invalid_id)) return;
        const bool union_type = node.kind == NodeType::UnionDef;
        const auto id = table_.declare_record(node.name, union_type ? RecordKind::Union : RecordKind::Struct, node.range);
        if (!id) return;
        node.record_id = *id;
        auto type = std::make_shared<TypeInfo>();
        type->kind = union_type ? TypeKind::Union : TypeKind::Struct;
        type->name = node.name;
        type->record_id = *id;
        node.type = type;
        if (node.children.empty()) return; // 允许标签前向声明。
        auto definition = *table_.record(*id);
        std::size_t size = 0, alignment = 1;
        bool valid = true;
        for (auto& member : node.children) {
            member->scope_id = table_.current_scope();
            if (member->kind != NodeType::MemberDecl || !shape(*member, 0, 0)) {
                error(*member, "结构体定义只能包含 MemberDecl", "SEM_MEMBER"); valid = false; continue;
            }
            member->type = resolve(member->declared_type, *member);
            try {
                const auto layout = detail::layout(member->type, table_.data());
                if (!union_type) size = detail::align_up(size, layout.alignment);
                if (!union_type && layout.size > detail::max_object_size - size) throw std::runtime_error("结构体过大");
                definition.members.push_back({member->name, member->type, union_type ? 0 : size, member->range});
                size = union_type ? std::max(size, layout.size) : size + layout.size;
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

    void initialize(std::unique_ptr<ASTNode>& init, const TypePtr& target, bool global) {
        init->scope_id = table_.current_scope();
        if (init->kind == NodeType::InitList) {
            if (!shape(*init, 0, invalid_id)) return;
            init->type = target;
            std::vector<TypePtr> elements;
            if (target->kind == TypeKind::Array) {
                if (init->children.size() > *target->array_length) { error(*init, "数组初始化项过多", "SEM_INIT_COUNT"); return; }
                for (std::size_t i = 0; i < init->children.size(); ++i) elements.push_back(target->base);
            } else if ((target->kind == TypeKind::Struct || target->kind == TypeKind::Union)) {
                const auto* record = table_.record(target->record_id);
                if (init->children.size() > (target->kind == TypeKind::Union ? 1 : record->members.size())) { error(*init, "结构体初始化项过多", "SEM_INIT_COUNT"); return; }
                for (std::size_t i = 0; i < init->children.size(); ++i) elements.push_back(record->members[i].type);
            } else {
                if (init->children.size() != 1) { error(*init, "数值初始化列表必须恰好一项", "SEM_INIT_COUNT"); return; }
                elements.push_back(target);
            }
            for (std::size_t i = 0; i < elements.size(); ++i) initialize(init->children[i], elements[i], global);
            return;
        }
        expression(*init);
        if (!valid_expression(*init)) return;
        if (target->kind == TypeKind::Pointer) decay(init);
        if (target->kind == TypeKind::Array && target->base->kind == TypeKind::Char && init->kind == NodeType::StringLiteral) {
            if (std::get<std::string>(init->value).size() > *target->array_length)
                error(*init, "字符串超过 char 数组长度", "SEM_INIT_COUNT");
            return;
        }
        if (global && !constant_initializer(*init)) { error(*init, "全局初始化必须是常量表达式", "SEM_GLOBAL_INIT"); return; }
        if ((target->kind == TypeKind::Struct || target->kind == TypeKind::Union) && assignable(target, *init)) return;
        if (!assignable(target, *init)) { error(*init, "初始化类型不兼容", "SEM_INIT_TYPE"); return; }
        convert(init, detail::unqualified(target));
    }

    bool constant_initializer(const ASTNode& node) const {
        if (node.kind == NodeType::IntLiteral || node.kind == NodeType::FloatLiteral || node.kind == NodeType::CharLiteral || node.kind == NodeType::StringLiteral)
            return true;
        if (node.kind == NodeType::Identifier && node.category == ValueCategory::Function) return true;
        if (node.kind == NodeType::UnaryOp && node.name == "&" && node.children.size() == 1)
            return static_address(*node.children[0]);
        if (node.kind == NodeType::ImplicitCast && node.type && node.type->kind == TypeKind::Pointer &&
            node.children.size() == 1 && node.children[0]->type->kind == TypeKind::Array)
            return static_address(*node.children[0]);
        if (node.kind != NodeType::BinaryOp && node.kind != NodeType::Cast && node.kind != NodeType::ImplicitCast &&
            !(node.kind == NodeType::UnaryOp && (node.name == "+" || node.name == "-" || node.name == "!" || node.name == "~"))) return false;
        for (const auto& child : node.children) if (!child || !constant_initializer(*child)) return false;
        return true;
    }

    // 全局初始化只接受全局对象及其固定下标、固定成员的地址。
    bool static_address(const ASTNode& node) const {
        if (node.kind == NodeType::StringLiteral) return true;
        if (node.kind == NodeType::Identifier) {
            const auto* entry = table_.symbol(node.symbol_id);
            return entry && (entry->scope == 0 || entry->storage == StorageClass::Static);
        }
        if (node.kind == NodeType::MemberAccess && node.children.size() == 1)
            return static_address(*node.children[0]);
        if (node.kind == NodeType::ArrayAccess && node.children.size() == 2)
            return static_address(*node.children[0]) && detail::integer_constant(*node.children[1]).has_value();
        return false;
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
        if (node.type && node.type->kind == TypeKind::Array && node.kind != NodeType::StringLiteral) {
            auto owned = std::make_unique<ASTNode>(std::move(node)); decay(owned); node = std::move(*owned);
        }
        if (valid_expression(node) && !detail::numeric(node.type) && node.type->kind != TypeKind::Pointer)
            error(node, "条件必须是数值或指针", "SEM_CONDITION");
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
                static const std::regex pattern(R"((0[xX][0-9a-fA-F]+|0[0-7]*|[1-9][0-9]*)([uU][lL]?|[lL][uU]?)?)");
                std::smatch match;
                if (!std::regex_match(node.name, match, pattern)) throw std::runtime_error("整数常量写法或后缀无效");
                const auto value = std::stoull(match[1].str(), nullptr, 0);
                const auto suffix = match[2].str();
                auto type = std::make_shared<TypeInfo>(); type->kind = suffix.find_first_of("lL") == std::string::npos ? TypeKind::Int : TypeKind::Long;
                type->is_unsigned = suffix.find_first_of("uU") != std::string::npos;
                const bool nondecimal = match[1].str().size() > 1 && match[1].str()[0] == '0';
                if (value > std::numeric_limits<std::int32_t>::max() && nondecimal) type->is_unsigned = true;
                if (value > (type->is_unsigned ? std::numeric_limits<std::uint32_t>::max() : static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())))
                    throw std::runtime_error("整数常量超出 32 位目标范围");
                node.type = type;
                node.value = type->is_unsigned ? ConstantValue{static_cast<std::uint64_t>(value)} : ConstantValue{static_cast<std::int64_t>(value)};
            } else {
                static const std::regex pattern(R"(((\d+\.\d*|\.\d+)([eE][+-]?\d+)?|\d+[eE][+-]?\d+)[fFlL]?)");
                if (!std::regex_match(node.name, pattern)) throw std::runtime_error("M1 浮点常量写法不受支持");
                const auto suffix = node.name.back();
                // 保留项目的无后缀 float 约定；L 后缀采用 64 位 long double 目标。
                const auto kind = suffix == 'l' || suffix == 'L' ? TypeKind::LongDouble : TypeKind::Float;
                const auto value = std::stod(node.name);
                if (!std::isfinite(value)) throw std::runtime_error("浮点常量必须有限");
                node.value = kind == TypeKind::Float ? detail::checked_float(value) : value;
                node.type = detail::type(kind);
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
        if (entry->kind == SymbolKind::Typedef) { error(node, "类型别名不能作为表达式使用", "SEM_TYPEDEF"); return; }
        if (entry->kind == SymbolKind::EnumConstant) {
            node.kind = NodeType::IntLiteral; node.name = std::to_string(enum_values_.at(*id));
            node.value = enum_values_.at(*id); node.type = detail::type(TypeKind::Int);
            node.category = ValueCategory::RValue; return;
        }
        node.symbol_id = *id;
        if (entry->kind == SymbolKind::Function && entry->builtin == BuiltinKind::None) calls_.push_back({*id, node.range});
        if (entry->storage == StorageClass::Extern && entry->kind != SymbolKind::Function) object_uses_.push_back({*id, node.range});
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
        decay(left); decay(right);
        if (node.name == ",") { node.type = right->type; node.category = ValueCategory::RValue; return; }
        if (left->type->kind == TypeKind::Pointer || right->type->kind == TypeKind::Pointer) {
            if (node.name == "+" && right->type->kind == TypeKind::Pointer && detail::numeric(left->type) && !detail::floating(left->type)) {
                try { detail::layout(right->type->base, table_.data()); }
                catch (const std::runtime_error& exception) { error(node, exception.what(), "SEM_POINTER"); return; }
                convert(left, detail::type(TypeKind::Int)); node.type = right->type; node.category = ValueCategory::RValue; return;
            }
            if ((node.name == "+" || node.name == "-") && left->type->kind == TypeKind::Pointer &&
                detail::numeric(right->type) && !detail::floating(right->type)) {
                try { detail::layout(left->type->base, table_.data()); }
                catch (const std::runtime_error& exception) { error(node, exception.what(), "SEM_POINTER"); return; }
                convert(right, detail::type(TypeKind::Int));
                node.type = left->type; node.category = ValueCategory::RValue; return;
            }
            const bool compatible = detail::pointer_assign(left->type, right->type) || detail::pointer_assign(right->type, left->type);
            if (compatible && (node.name == "-" || node.name == "<" || node.name == "<=" || node.name == ">" || node.name == ">=")) {
                try { detail::layout(left->type->base, table_.data()); }
                catch (const std::runtime_error& exception) { error(node, exception.what(), "SEM_POINTER"); return; }
                node.type = detail::type(TypeKind::Int); node.category = ValueCategory::RValue; return;
            }
            const bool equality = node.name == "==" || node.name == "!=";
            const bool logic = node.name == "&&" || node.name == "||";
            if (logic && (detail::numeric(left->type) || left->type->kind == TypeKind::Pointer) &&
                (detail::numeric(right->type) || right->type->kind == TypeKind::Pointer)) {
                node.type = detail::type(TypeKind::Int); node.category = ValueCategory::RValue; return;
            }
            if (equality) {
                if (left->type->kind == TypeKind::Pointer && detail::integer_constant(*right) == 0) convert(right, left->type);
                if (right->type->kind == TypeKind::Pointer && detail::integer_constant(*left) == 0) convert(left, right->type);
                if (detail::pointer_assign(left->type, right->type) || detail::pointer_assign(right->type, left->type)) {
                    node.type = detail::type(TypeKind::Int); node.category = ValueCategory::RValue; return;
                }
            }
            error(node, "指针运算需要兼容指针或整型偏移", "SEM_POINTER"); return;
        }
        const auto common = arithmetic_result(left->type, right->type);
        const bool compare = node.name == "<" || node.name == "<=" || node.name == ">" ||
                             node.name == ">=" || node.name == "==" || node.name == "!=";
        const bool logic = node.name == "&&" || node.name == "||";
        const bool arithmetic = node.name == "+" || node.name == "-" || node.name == "*" ||
                                node.name == "/" || node.name == "%";
        const bool bitwise = node.name == "&" || node.name == "|" || node.name == "^" || node.name == "<<" || node.name == ">>";
        if (common->kind == TypeKind::Error || (!compare && !logic && !arithmetic && !bitwise) ||
            ((node.name == "%" || bitwise) && detail::floating(common))) {
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
        decay(right);
        if (left->category == ValueCategory::LValue && (left->type->kind == TypeKind::Struct || left->type->kind == TypeKind::Union) &&
            modifiable(left->type) && node.name == "=" && assignable(left->type, *right)) {
            node.type = left->type; node.category = ValueCategory::RValue; return;
        }
        if (left->category == ValueCategory::LValue && left->type->kind == TypeKind::Pointer &&
            !left->type->is_const && node.name == "=" && assignable(left->type, *right)) {
            convert(right, left->type); node.type = left->type; node.category = ValueCategory::RValue; return;
        }
        if (left->category == ValueCategory::LValue && left->type->kind == TypeKind::Pointer && !left->type->is_const &&
            (node.name == "+=" || node.name == "-=") && detail::numeric(right->type) && !detail::floating(right->type)) {
            try { detail::layout(left->type->base, table_.data()); }
            catch (const std::runtime_error& exception) { error(node, exception.what(), "SEM_POINTER"); return; }
            convert(right, detail::type(TypeKind::Int)); node.type = left->type; node.category = ValueCategory::RValue; return;
        }
        if (left->category != ValueCategory::LValue || left->type->is_const || !detail::numeric(left->type)) {
            error(node, "赋值目标必须是可修改的数值变量", "SEM_LVALUE"); return;
        }
        const bool integer_compound = node.name == "%=" || node.name == "&=" || node.name == "|=" || node.name == "^=" || node.name == "<<=" || node.name == ">>=";
        const bool compound = node.name == "+=" || node.name == "-=" || node.name == "*=" || node.name == "/=" || integer_compound;
        if (node.name != "=" && !compound) { error(node, "不支持的赋值运算符"); return; }
        auto source = compound ? arithmetic_result(left->type, right->type) : right->type;
        if (integer_compound && detail::floating(source)) { error(node, "位运算和取余需要整型操作数", "SEM_OPERANDS"); return; }
        if (!can_assign(left->type, source)) { error(node, "赋值需要不允许的隐式类型转换", "SEM_ASSIGN_TYPE"); return; }
        convert(right, left->type);
        node.type = detail::unqualified(left->type);
        node.category = ValueCategory::RValue;
    }

    void unary(ASTNode& node, bool address_allowed = false) {
        if (!shape(node, 1, 1)) return;
        expression(*node.children[0]);
        auto& child = node.children[0];
        if (!valid_expression(*child)) { node.type = detail::type(TypeKind::Error); return; }
        if (node.name != "&") decay(child);
        if (node.name == "&") {
            if (child->kind == NodeType::Identifier && table_.symbol(child->symbol_id)->storage == StorageClass::Register) {
                error(node, "register 对象不能取地址", "SEM_ADDRESS"); return;
            }
            if ((child->category != ValueCategory::LValue && child->category != ValueCategory::Function) || child->type->kind == TypeKind::Void ||
                (address_allowed && (!detail::numeric(child->type) || child->type->is_const))) {
                error(node, "取地址需要对象左值；scanf 还需要可修改的数值对象", "SEM_ADDRESS"); return;
            }
            auto pointer = std::make_shared<TypeInfo>();
            pointer->kind = TypeKind::Pointer;
            pointer->base = child->type;
            node.type = pointer;
        } else if (node.name == "*") {
            if (child->type->kind != TypeKind::Pointer || !child->type->base || child->type->base->kind == TypeKind::Void) {
                error(node, "解引用需要完整对象指针", "SEM_POINTER"); return;
            }
            if (child->type->base->kind == TypeKind::Function) { node.type = child->type->base; node.category = ValueCategory::Function; return; }
            try { detail::layout(child->type->base, table_.data()); }
            catch (const std::runtime_error& exception) { error(node, exception.what(), "SEM_POINTER"); return; }
            node.type = child->type->base; node.category = ValueCategory::LValue; return;
        } else if (node.name == "!" && child->type->kind == TypeKind::Pointer) {
            node.type = detail::type(TypeKind::Int);
        } else if (child->type->kind == TypeKind::Pointer && (node.name == "pre++" || node.name == "post++" || node.name == "pre--" || node.name == "post--")) {
            if (child->category != ValueCategory::LValue || child->type->is_const) { error(node, "指针自增需要可修改左值", "SEM_LVALUE"); return; }
            try { detail::layout(child->type->base, table_.data()); }
            catch (const std::runtime_error& exception) { error(node, exception.what(), "SEM_POINTER"); return; }
            node.type = child->type;
        } else {
            if (!detail::numeric(child->type)) { error(node, "一元运算需要数值类型"); return; }
            const bool update = node.name == "pre++" || node.name == "post++" ||
                                node.name == "pre--" || node.name == "post--";
            if (update) {
                if (child->category != ValueCategory::LValue || child->type->is_const) {
                    error(node, "自增或自减需要可修改变量", "SEM_LVALUE"); return;
                }
                node.type = detail::unqualified(child->type);
            } else if (node.name == "+" || node.name == "-" || node.name == "!" || node.name == "~") {
                if (node.name == "~" && detail::floating(child->type)) { error(node, "按位取反需要整型", "SEM_OPERANDS"); return; }
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
        const bool direct = callee.kind == NodeType::Identifier && callee.category == ValueCategory::Function;
        SymbolEntry entry;
        if (direct) { entry = *table_.symbol(callee.symbol_id); node.symbol_id = callee.symbol_id; }
        else {
            entry.type = callee.type->kind == TypeKind::Function ? callee.type :
                callee.type->kind == TypeKind::Pointer ? callee.type->base : TypePtr{};
            if (!entry.type || entry.type->kind != TypeKind::Function || entry.type->variadic) { error(node, "调用需要有固定签名的函数指针", "SEM_CALL"); return; }
        }
        const bool builtin = entry.builtin != BuiltinKind::None;
        const bool scanning = entry.builtin == BuiltinKind::Scanf;
        for (std::size_t i = 1; i < node.children.size(); ++i) {
            auto& child = *node.children[i];
            child.scope_id = table_.current_scope();
            if (scanning && i > 1 && child.kind == NodeType::UnaryOp && child.name == "&") unary(child);
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
                    const auto expected = detail::format_type(part, scanning);
                    if (part.conversion == 's') decay(argument);
                    if (scanning) {
                        if (argument->type->kind != TypeKind::Pointer || !argument->type->base ||
                            !same_type(detail::unqualified(argument->type->base), expected) || argument->type->base->is_const)
                            throw std::runtime_error("scanf 实参地址类型与格式转换符不匹配");
                    } else if (part.conversion == 's') {
                        if (argument->type->kind != TypeKind::Pointer || argument->type->base->kind != TypeKind::Char)
                            throw std::runtime_error("%s 需要字符数组或字符指针");
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
                error(node, "函数实参数量与原型不一致", "SEM_ARGUMENT_COUNT"); return;
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
        case NodeType::ArrayAccess:
            if (shape(node, 2, 2)) {
                expression(*node.children[0]); expression(*node.children[1]);
                const auto& array = *node.children[0];
                const auto& index = *node.children[1];
                if (!valid_expression(array) || !valid_expression(index)) { node.type = detail::type(TypeKind::Error); break; }
                if ((array.type->kind != TypeKind::Array && array.type->kind != TypeKind::Pointer) || !detail::numeric(index.type) || detail::floating(index.type)) {
                    error(node, "下标访问需要数组和整型下标", "SEM_INDEX"); break;
                }
                try { detail::layout(array.type->base, table_.data()); }
                catch (const std::runtime_error& exception) { error(node, exception.what(), "SEM_INDEX"); break; }
                auto element = std::make_shared<TypeInfo>(*array.type->base);
                element->is_const = element->is_const || (array.type->kind == TypeKind::Array && array.type->is_const);
                element->is_volatile = element->is_volatile || (array.type->kind == TypeKind::Array && array.type->is_volatile);
                node.type = element;
                node.category = array.type->kind == TypeKind::Pointer || array.category == ValueCategory::LValue ? ValueCategory::LValue : ValueCategory::RValue;
                convert(node.children[1], detail::type(TypeKind::Int));
            }
            break;
        case NodeType::MemberAccess:
            if (shape(node, 1, 1)) {
                expression(*node.children[0]);
                if (valid_expression(*node.children[0]) && node.children[0]->type->kind == TypeKind::Pointer) {
                    auto dereference = std::make_unique<ASTNode>();
                    dereference->kind = NodeType::UnaryOp; dereference->name = "*";
                    dereference->range = node.children[0]->range; dereference->scope_id = node.scope_id;
                    dereference->type = node.children[0]->type->base; dereference->category = ValueCategory::LValue;
                    dereference->children.push_back(std::move(node.children[0])); node.children[0] = std::move(dereference);
                }
                const auto& object = *node.children[0];
                if (!valid_expression(object)) { node.type = detail::type(TypeKind::Error); break; }
                if ((object.type->kind != TypeKind::Struct && object.type->kind != TypeKind::Union)) { error(node, "成员访问需要结构体对象", "SEM_MEMBER"); break; }
                const auto index = table_.find_member(object.type->record_id, node.name);
                if (!index) { error(node, "结构体中没有该成员：" + node.name, "SEM_MEMBER"); break; }
                node.record_id = object.type->record_id;
                node.member_index = *index;
                auto member = std::make_shared<TypeInfo>(*table_.record(node.record_id)->members[*index].type);
                member->is_const = member->is_const || object.type->is_const;
                member->is_volatile = member->is_volatile || object.type->is_volatile;
                node.type = member;
                node.category = object.category;
            }
            break;
        case NodeType::Sizeof:
            if (shape(node, node.declared_type ? 0 : 1, node.declared_type ? 0 : 1)) {
                auto measured = node.declared_type ? resolve(node.declared_type, node) : TypePtr{};
                if (!node.declared_type) { expression(*node.children[0]); measured = node.children[0]->type; }
                try {
                    const auto size = detail::layout(measured, table_.data()).size;
                    node.children.clear(); node.declared_type.reset();
                    node.kind = NodeType::IntLiteral; node.name = std::to_string(size);
                    node.value = static_cast<std::int64_t>(size); node.type = detail::type(TypeKind::Int);
                    node.category = ValueCategory::RValue;
                } catch (const std::runtime_error& exception) { error(node, exception.what(), "SEM_SIZEOF"); }
            }
            break;
        case NodeType::Conditional:
            if (shape(node, 3, 3)) {
                require_condition(*node.children[0]);
                expression(*node.children[1]); expression(*node.children[2]);
                decay(node.children[1]); decay(node.children[2]);
                auto common = arithmetic_result(node.children[1]->type, node.children[2]->type);
                if (common->kind == TypeKind::Error) {
                    const auto& yes = *node.children[1]; const auto& no = *node.children[2];
                    if (yes.type && yes.type->kind == TypeKind::Pointer && assignable(yes.type, no)) common = yes.type;
                    else if (no.type && no.type->kind == TypeKind::Pointer && assignable(no.type, yes)) common = no.type;
                }
                if (common->kind == TypeKind::Error) error(node, "条件表达式的两个结果类型不兼容", "SEM_CONDITIONAL");
                else {
                    convert(node.children[1], common); convert(node.children[2], common);
                    node.type = common; node.category = ValueCategory::RValue;
                }
            }
            break;
        case NodeType::Cast:
            if (shape(node, 1, 1)) {
                expression(*node.children[0]);
                decay(node.children[0]);
                const auto target = resolve(node.declared_type, node);
                const auto source = node.children[0]->type;
                const bool character_pointer_cast = target->kind == TypeKind::Pointer && source && source->kind == TypeKind::Pointer &&
                    target->base && source->base && target->base->kind != TypeKind::Function && source->base->kind != TypeKind::Function &&
                    (target->base->kind == TypeKind::Char || source->base->kind == TypeKind::Char) &&
                    (!source->base->is_const || target->base->is_const) && (!source->base->is_volatile || target->base->is_volatile);
                if ((detail::numeric(target) && detail::numeric(node.children[0]->type)) ||
                    character_pointer_cast || (target->kind == TypeKind::Pointer && assignable(target, *node.children[0]))) {
                    node.type = target;
                    node.category = ValueCategory::RValue;
                } else error(node, "显式转换只支持数值或兼容对象指针");
            }
            break;
        default: error(node, "此表达式节点尚未支持，或语法树已被分析过", "SEM_UNSUPPORTED"); break;
        }
        --depth_;
    }

    void variable(ASTNode& node, bool global) {
        if (!shape(node, 0, 1)) return;
        auto resolved = resolve(node.declared_type, node);
        if (resolved->kind == TypeKind::Array && !resolved->array_length && !node.children.empty()) {
            auto inferred = std::make_shared<TypeInfo>(*resolved);
            if (node.children[0]->kind == NodeType::InitList) inferred->array_length = node.children[0]->children.size();
            else if (node.children[0]->kind == NodeType::StringLiteral && inferred->base->kind == TypeKind::Char) {
                try { inferred->array_length = detail::decode_string(node.children[0]->name, '"').size() + 1; }
                catch (const std::runtime_error&) {}
            }
            resolved = inferred;
        }
        try { detail::layout(resolved, table_.data()); }
        catch (const std::runtime_error& exception) { error(node, exception.what(), "SEM_DECL_TYPE"); return; }
        if (node.storage == StorageClass::Extern && !global && !node.children.empty()) { error(node, "块内 extern 不能初始化", "SEM_DECL_TYPE"); return; }
        if (node.storage != StorageClass::None && node.storage != StorageClass::Static && node.storage != StorageClass::Extern && !( !global &&
             (node.storage == StorageClass::Auto || node.storage == StorageClass::Register))) {
            error(node, "变量存储类别尚未支持", "SEM_DECL_TYPE"); return;
        }
        SymbolEntry entry;
        entry.name = node.name;
        entry.type = resolved;
        entry.kind = resolved->kind == TypeKind::Array ? SymbolKind::Array : SymbolKind::Variable;
        entry.range = node.range;
        entry.storage = node.storage;
        entry.is_defined = !node.children.empty();
        const auto id = table_.declare_object(std::move(entry));
        if (!id) { node.type = detail::type(TypeKind::Error); return; }
        node.symbol_id = *id;
        node.type = table_.symbol(*id)->type;
        if (!node.children.empty()) {
            initialize(node.children[0], node.type, global || node.storage == StorageClass::Static);
        } else if (node.type->is_const && node.storage != StorageClass::Extern) error(node, "const 变量必须初始化", "SEM_CONST_INIT");
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
        case NodeType::StructDef: case NodeType::UnionDef: record_definition(node); break;
        case NodeType::TypedefDecl: typedef_declaration(node); break;
        case NodeType::EnumDef: enum_definition(node); break;
        case NodeType::Label:
            if (shape(node, 1, 1)) {
                if (node.name.empty() || !labels_.emplace(node.name, node.range).second)
                    error(node, "函数内标签为空或重复", "SEM_LABEL");
                returned = statement(*node.children[0]);
            }
            break;
        case NodeType::Goto:
            if (shape(node, 0, 0)) gotos_.push_back({node.name, node.range});
            break;
        case NodeType::ExprStmt:
            if (shape(node, 1, 1)) expression(*node.children[0]);
            break;
        case NodeType::Return:
            if (shape(node, 0, 1)) {
                if (node.children.empty()) {
                    if (return_type_->kind != TypeKind::Void) error(node, "非 void 函数必须返回一个值", "SEM_RETURN");
                } else {
                    expression(*node.children[0]);
                    decay(node.children[0]);
                    if (return_type_->kind == TypeKind::Void) error(node, "void 函数不能返回一个值", "SEM_RETURN");
                    else if (valid_expression(*node.children[0])) {
                        if (!assignable(return_type_, *node.children[0])) error(node, "返回值类型不兼容", "SEM_RETURN");
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
        case NodeType::DoWhile:
            if (shape(node, 2, 2)) {
                ++loop_depth_; statement(*node.children[0]); --loop_depth_;
                require_condition(*node.children[1]);
            }
            break;
        case NodeType::Switch:
            if (shape(node, 2, 2)) {
                expression(*node.children[0]);
                if (valid_expression(*node.children[0]) && (!detail::numeric(node.children[0]->type) ||
                    detail::floating(node.children[0]->type))) error(node, "switch 需要整型控制表达式", "SEM_SWITCH");
                if (node.children[1]->kind != NodeType::Block) error(node, "switch 的第二个孩子必须是 Block", "SEM_SWITCH");
                switches_.push_back({}); statement(*node.children[1]); switches_.pop_back();
            }
            break;
        case NodeType::Case: case NodeType::Default:
            if (shape(node, node.kind == NodeType::Case ? 2 : 1, node.kind == NodeType::Case ? 2 : 1)) {
                if (switches_.empty()) { error(node, "case/default 只能位于 switch 中", "SEM_SWITCH"); break; }
                if (node.kind == NodeType::Default) {
                    if (switches_.back().has_default) error(node, "switch 中出现重复 default", "SEM_SWITCH_DUPLICATE");
                    switches_.back().has_default = true;
                } else {
                    expression(*node.children[0]);
                    const auto value = detail::integer_constant(*node.children[0]);
                    if (!value) error(node, "case 必须是有效的整型常量表达式", "SEM_CASE");
                    else if (!switches_.back().values.insert(*value).second) error(node, "switch 中出现重复 case 值", "SEM_SWITCH_DUPLICATE");
                    else node.value = *value;
                }
                statement(*node.children.back());
            }
            break;
        case NodeType::Break: case NodeType::Continue:
            if (shape(node, 0, 0) && loop_depth_ == 0 &&
                (node.kind == NodeType::Continue || switches_.empty()))
                error(node, "break 需要循环或 switch，continue 需要循环", "SEM_LOOP");
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
        if (declared && declared->kind == TypeKind::Function) {
            auto normalized = std::make_shared<TypeInfo>(*declared);
            normalized->base = resolve(declared->base, node);
            for (auto& parameter : normalized->params) {
                parameter = resolve(parameter, node);
                if (parameter->kind == TypeKind::Array) parameter = detail::pointer(parameter->base);
                else if (parameter->kind == TypeKind::Function) parameter = detail::pointer(parameter);
                parameter = detail::unqualified(parameter);
            }
            declared = normalized;
        }
        if (!declared || declared->kind != TypeKind::Function ||
            !(detail::numeric(declared->base) || (declared->base && (declared->base->kind == TypeKind::Void || declared->base->kind == TypeKind::Pointer ||
              declared->base->kind == TypeKind::Struct || declared->base->kind == TypeKind::Union))) ||
            declared->variadic || (node.storage != StorageClass::None && node.storage != StorageClass::Static && node.storage != StorageClass::Extern) ||
            (!declared->has_prototype && !declared->params.empty()) ||
            node.children.size() != declared->params.size() + (definition ? 1 : 0)) {
            error(node, "M1 函数类型或形参列表无效", "SEM_FUNCTION"); return;
        }
        for (std::size_t i = 0; i < declared->params.size(); ++i) {
            auto& parameter = *node.children[i];
            auto parameter_type = resolve(parameter.declared_type, parameter);
            if (parameter_type->kind == TypeKind::Array) parameter_type = detail::pointer(parameter_type->base);
            else if (parameter_type->kind == TypeKind::Function) parameter_type = detail::pointer(parameter_type);
            if (parameter.kind != NodeType::ParamDecl || !parameter.children.empty() ||
                !(detail::numeric(declared->params[i]) || declared->params[i]->kind == TypeKind::Pointer ||
                  declared->params[i]->kind == TypeKind::Struct || declared->params[i]->kind == TypeKind::Union) ||
                !same_type(detail::unqualified(parameter_type), declared->params[i]) ||
                parameter.storage != StorageClass::None || (definition && parameter.name.empty())) {
                error(node, "函数形参节点与签名不一致", "SEM_PARAMETER"); return;
            }
            parameter.type = parameter_type;
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
        entry.storage = node.storage;
        entry.is_defined = definition;
        const auto id = table_.insert(std::move(entry));
        if (!id) { node.type = detail::type(TypeKind::Error); return; }
        node.symbol_id = *id;
        node.type = normalized;
        if (!definition) return;
        node.scope_id = table_.enter_scope(ScopeKind::Function, node.range);
        return_type_ = normalized->base;
        labels_.clear(); gotos_.clear();
        for (std::size_t i = 0; i < declared->params.size(); ++i) {
            auto& parameter = *node.children[i];
            parameter.scope_id = node.scope_id;
            // 签名忽略形参的顶层限定符，函数体内的形参仍保留 const/volatile。
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
        for (const auto& jump : gotos_) if (!labels_.count(jump.first))
            diagnostics_.report(Level::Error, jump.second, "goto 的目标标签不存在：" + jump.first, "SEM_GOTO");
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
                else if (child->kind == NodeType::StructDef || child->kind == NodeType::UnionDef) record_definition(*child);
                else if (child->kind == NodeType::TypedefDecl) typedef_declaration(*child);
                else if (child->kind == NodeType::EnumDef) enum_definition(*child);
                else if (child->kind == NodeType::FunctionDecl || child->kind == NodeType::FunctionDef) function(*child);
                else error(*child, "M1 顶层仅支持变量和函数声明", "SEM_UNSUPPORTED");
            }
            for (const auto& call : calls_) {
                if (!table_.symbol(call.first)->is_defined)
                    diagnostics_.report(Level::Error, call.second, "被调用函数没有定义：" + table_.symbol(call.first)->name, "SEM_UNDEFINED_FUNCTION");
            }
            for (const auto& use : object_uses_) {
                const auto* entry = table_.symbol(use.first);
                if (entry->storage == StorageClass::Extern && !entry->is_defined)
                    diagnostics_.report(Level::Error, use.second, "外部对象没有本文件定义：" + entry->name, "SEM_UNDEFINED_OBJECT");
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
