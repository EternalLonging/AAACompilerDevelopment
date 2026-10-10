#include "minic/semantic.hpp"
#include "internal.hpp"

#include <utility>

namespace minic {
namespace {

using TypePair = std::pair<const TypeInfo*, const TypeInfo*>;

// 比较类型的每一层，循环引用视为无效类型。
bool compare(const TypePtr& left, const TypePtr& right, std::vector<TypePair>& path) {
    if (path.size() > 128) return false;
    if (!left || !right || left->kind != right->kind ||
        left->is_unsigned != right->is_unsigned ||
        left->is_const != right->is_const || left->is_volatile != right->is_volatile)
        return false;
    for (const auto& pair : path)
        if (pair.first == left && pair.second == right) return false;
    path.emplace_back(left, right);
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
        for (std::size_t i = 0; equal && i < left->params.size(); ++i) {
            const auto adjusted = [](TypePtr parameter) {
                TypeInfo result;
                if (parameter->kind == TypeKind::Array || parameter->kind == TypeKind::Function) {
                    result.kind = TypeKind::Pointer;
                    result.base = parameter->kind == TypeKind::Array ? parameter->base : parameter;
                } else result = *parameter;
                result.is_const = result.is_volatile = false;
                return result;
            };
            if (!left->params[i] || !right->params[i]) { equal = false; break; }
            const auto left_parameter = adjusted(left->params[i]);
            const auto right_parameter = adjusted(right->params[i]);
            equal = compare(&left_parameter, &right_parameter, path);
        }
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

// 判断教学目标支持的数值类型。
bool m1_number(const TypePtr& type) { return detail::numeric(type); }

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
    if (!m1_number(left) || !m1_number(right)) return basic_type(TypeKind::Error);
    if (left->kind == TypeKind::LongDouble || right->kind == TypeKind::LongDouble) return basic_type(TypeKind::LongDouble);
    if (left->kind == TypeKind::Double || right->kind == TypeKind::Double) return basic_type(TypeKind::Double);
    if (left->kind == TypeKind::Float || right->kind == TypeKind::Float) return basic_type(TypeKind::Float);
    auto result = make_type_info();
    result->kind = left->kind == TypeKind::Long || right->kind == TypeKind::Long ? TypeKind::Long : TypeKind::Int;
    result->is_unsigned = (left->is_unsigned && detail::integer_bits(left) == 32) || (right->is_unsigned && detail::integer_bits(right) == 32);
    return result;
}

bool can_assign(const TypePtr& target, const TypePtr& source) {
    if (!m1_number(target) || !m1_number(source)) return false;
    if (target->is_unsigned || source->is_unsigned || target->kind == TypeKind::Short || source->kind == TypeKind::Short ||
        target->kind == TypeKind::Long || source->kind == TypeKind::Long || target->kind == TypeKind::Double || source->kind == TypeKind::Double ||
        target->kind == TypeKind::LongDouble || source->kind == TypeKind::LongDouble) return true;
    if (target->kind == source->kind || target->kind == TypeKind::Float) return true;
    return target->kind == TypeKind::Int && source->kind == TypeKind::Char;
}

}
