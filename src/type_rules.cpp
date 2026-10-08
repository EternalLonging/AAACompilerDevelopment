#include "minic/semantic.hpp"

#include <utility>

namespace minic {
namespace {

using TypePair = std::pair<const TypeInfo*, const TypeInfo*>;

// 比较类型的每一层，循环引用视为无效类型。
bool compare(const TypePtr& left, const TypePtr& right, std::vector<TypePair>& path) {
    if (!left || !right || left->kind != right->kind ||
        left->is_unsigned != right->is_unsigned ||
        left->is_const != right->is_const || left->is_volatile != right->is_volatile)
        return false;
    for (const auto& pair : path)
        if (pair.first == left.get() && pair.second == right.get()) return false;
    path.emplace_back(left.get(), right.get());
    bool equal = false;
    switch (left->kind) {
    case TypeKind::Unknown: case TypeKind::Error: case TypeKind::Named:
        break;
    case TypeKind::Pointer:
        equal = compare(left->base, right->base, path);
        break;
    case TypeKind::Array:
        equal = left->array_length == right->array_length &&
                compare(left->base, right->base, path);
        break;
    case TypeKind::Function:
        equal = left->variadic == right->variadic &&
                left->has_prototype == right->has_prototype &&
                left->params.size() == right->params.size() &&
                compare(left->base, right->base, path);
        for (std::size_t i = 0; equal && i < left->params.size(); ++i)
            equal = compare(left->params[i], right->params[i], path);
        break;
    case TypeKind::Struct: case TypeKind::Union: case TypeKind::Enum:
        equal = left->record_id != invalid_id && left->record_id == right->record_id;
        break;
    default:
        equal = true;
        break;
    }
    path.pop_back();
    return equal;
}

// M1 只处理有符号 char、int 和 float 的数值转换。
bool m1_number(const TypePtr& type) {
    return type && !type->is_unsigned &&
           (type->kind == TypeKind::Char || type->kind == TypeKind::Int ||
            type->kind == TypeKind::Float);
}

TypePtr basic_type(TypeKind kind) {
    auto type = std::make_shared<TypeInfo>();
    type->kind = kind;
    return type;
}

}

bool same_type(const TypePtr& left, const TypePtr& right) {
    std::vector<TypePair> path;
    return compare(left, right, path);
}

TypePtr arithmetic_result(const TypePtr& left, const TypePtr& right) {
    if (!m1_number(left) || !m1_number(right)) return basic_type(TypeKind::Error);
    return basic_type(left->kind == TypeKind::Float || right->kind == TypeKind::Float
                          ? TypeKind::Float : TypeKind::Int);
}

bool can_assign(const TypePtr& target, const TypePtr& source) {
    if (!m1_number(target) || !m1_number(source)) return false;
    if (target->kind == source->kind || target->kind == TypeKind::Float) return true;
    return target->kind == TypeKind::Int && source->kind == TypeKind::Char;
}

}
