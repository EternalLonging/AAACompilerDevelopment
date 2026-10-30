#include "minic/semantic.hpp"
#include "internal.hpp"

#include <utility>

namespace minic {
namespace {

using TypePair = std::pair<const TypeInfo*, const TypeInfo*>;

// 比较类型的每一层，循环引用视为无效类型。
bool compare(const TypePtr& left, const TypePtr& right, std::vector<TypePair>& path) {
    if (path.size() > 128) return false;
    if (!left || !right || left->kind != right->kind || left->is_const != right->is_const)
        return false;
    for (const auto& pair : path)
        if (pair.first == left && pair.second == right) return false;
    path.emplace_back(left, right);
    bool equal = false;
    switch (left->kind) {
    case TypeKind::Unknown: case TypeKind::Error:
        break;
    case TypeKind::Address:
        equal = compare(left->base, right->base, path);
        break;
    case TypeKind::Array:
        equal = left->array_length == right->array_length &&
                compare(left->base, right->base, path);
        break;
    case TypeKind::Function:
        equal = left->variadic == right->variadic &&
                left->params.size() == right->params.size() &&
                compare(left->base, right->base, path);
        for (std::size_t i = 0; equal && i < left->params.size(); ++i) {
            const auto adjusted = [](TypePtr parameter) {
                TypeInfo result;
                if (parameter->kind == TypeKind::Array) {
                    result.kind = TypeKind::Address;
                    result.base = parameter->base;
                } else result = *parameter;
                result.is_const = false;
                return result;
            };
            if (!left->params[i] || !right->params[i]) { equal = false; break; }
            const auto left_parameter = adjusted(left->params[i]);
            const auto right_parameter = adjusted(right->params[i]);
            equal = compare(&left_parameter, &right_parameter, path);
        }
        break;
    case TypeKind::Struct:
        equal = left->record_id != invalid_id && left->record_id == right->record_id;
        break;
    default:
        equal = true;
        break;
    }
    path.pop_back();
    return equal;
}

TypePtr basic_type(TypeKind kind) {
    auto type = make_type_info();
    type->kind = kind;
    return type;
}

}

bool same_type(const TypePtr& left, const TypePtr& right) {
    std::vector<TypePair> path;
    return compare(left, right, path);
}

TypePtr arithmetic_result(const TypePtr& left, const TypePtr& right) {
    if (!detail::numeric(left) || !detail::numeric(right)) return basic_type(TypeKind::Error);
    if (left->kind == TypeKind::Float || right->kind == TypeKind::Float) return basic_type(TypeKind::Float);
    return basic_type(TypeKind::Int);
}

bool can_assign(const TypePtr& target, const TypePtr& source) {
    if (!detail::numeric(target) || !detail::numeric(source)) return false;
    if (target->kind == source->kind || target->kind == TypeKind::Float) return true;
    return target->kind == TypeKind::Int && source->kind == TypeKind::Char;
}

}
