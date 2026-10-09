#pragma once
#include "minic/interface.hpp"
#include "minic/semantic.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace minic::detail {

// 建立一个不带限定符的基本类型。
inline TypePtr type(TypeKind kind) {
    auto result = std::make_shared<TypeInfo>();
    result->kind = kind;
    return result;
}

inline bool numeric(const TypePtr& value) {
    return value && !value->is_unsigned &&
           (value->kind == TypeKind::Char || value->kind == TypeKind::Int ||
            value->kind == TypeKind::Float);
}

inline std::string symbol_name(SymbolId id) { return "%s" + std::to_string(id); }

inline TypePtr pointer(TypePtr base) {
    auto result = std::make_shared<TypeInfo>();
    result->kind = TypeKind::Pointer;
    result->base = std::move(base);
    return result;
}

struct Layout {
    std::size_t size; // 对象占用的字节数。
    std::size_t alignment; // 对象的字节对齐要求。
};

inline constexpr std::size_t max_object_size = 16 * 1024 * 1024;

inline std::size_t align_up(std::size_t size, std::size_t alignment) {
    if (!alignment || size > max_object_size || alignment > max_object_size - size)
        throw std::runtime_error("对象大小或对齐超出教学解释器限制");
    return (size + alignment - 1) / alignment * alignment;
}

// 布局固定使用项目目标模型，不依赖宿主 sizeof。
inline Layout layout(const TypePtr& value, const SymbolTableData& symbols, std::size_t depth = 0) {
    if (!value || depth > 128) throw std::runtime_error("对象类型为空或嵌套过深");
    if (numeric(value)) return value->kind == TypeKind::Char ? Layout{1, 1} : Layout{4, 4};
    if (value->kind == TypeKind::Pointer && value->base) return {8, 8};
    if (value->kind == TypeKind::Array) {
        if (!value->array_length || !*value->array_length) throw std::runtime_error("数组长度必须已确定且大于零");
        const auto element = layout(value->base, symbols, depth + 1);
        if (*value->array_length > max_object_size / element.size) throw std::runtime_error("数组超出 16 MiB 对象限制");
        return {element.size * *value->array_length, element.alignment};
    }
    if ((value->kind == TypeKind::Struct || value->kind == TypeKind::Union) && value->record_id < symbols.records.size()) {
        const auto& record = symbols.records[value->record_id];
        if (record.kind != (value->kind == TypeKind::Struct ? RecordKind::Struct : RecordKind::Union) ||
            !record.is_complete || !record.size || !record.alignment ||
            !*record.size || *record.size > max_object_size || !*record.alignment ||
            (*record.alignment & (*record.alignment - 1)) || *record.size % *record.alignment)
            throw std::runtime_error("结构体尚未完整定义或布局无效");
        return {*record.size, *record.alignment};
    }
    throw std::runtime_error("对象类型尚未支持布局");
}

inline bool aggregate(const TypePtr& value) {
    return value && (value->kind == TypeKind::Array || value->kind == TypeKind::Struct || value->kind == TypeKind::Union);
}

inline bool pointer_assign(const TypePtr& target, const TypePtr& source) {
    if (!target || !source || target->kind != TypeKind::Pointer || source->kind != TypeKind::Pointer ||
        !target->base || !source->base) return false;
    if (source->base->is_const && !target->base->is_const) return false;
    if (source->base->is_volatile && !target->base->is_volatile) return false;
    auto left = std::make_shared<TypeInfo>(*target->base), right = std::make_shared<TypeInfo>(*source->base);
    left->is_const = right->is_const = false;
    left->is_volatile = right->is_volatile = false;
    return same_type(left, right);
}

// 读取纯整型常量表达式；不执行调用、赋值或变量读取。
inline std::optional<std::int64_t> integer_constant(const ASTNode& node, std::size_t depth = 0) {
    if (depth > 128 || !numeric(node.type) || node.type->kind == TypeKind::Float) return std::nullopt;
    if (node.kind == NodeType::IntLiteral || node.kind == NodeType::CharLiteral) {
        if (const auto* value = std::get_if<std::int64_t>(&node.value)) return *value;
        return std::nullopt;
    }
    if (node.children.empty() || !node.children[0]) return std::nullopt;
    const auto left = integer_constant(*node.children[0], depth + 1);
    if (!left) return std::nullopt;
    std::int64_t value;
    if (node.kind == NodeType::ImplicitCast || node.kind == NodeType::Cast) value = *left;
    else if (node.kind == NodeType::UnaryOp) {
        if (node.name == "+") value = *left;
        else if (node.name == "-") value = -*left;
        else if (node.name == "!") value = !*left;
        else if (node.name == "~") value = ~*left;
        else return std::nullopt;
    } else if (node.kind == NodeType::BinaryOp && node.children.size() == 2 && node.children[1]) {
        if (node.name == "&&" && !*left) return 0;
        if (node.name == "||" && *left) return 1;
        const auto right = integer_constant(*node.children[1], depth + 1);
        if (!right) return std::nullopt;
        if (node.name == "+") value = *left + *right;
        else if (node.name == "-") value = *left - *right;
        else if (node.name == "*") value = *left * *right;
        else if (node.name == "&") value = *left & *right;
        else if (node.name == "|") value = *left | *right;
        else if (node.name == "^") value = *left ^ *right;
        else if (node.name == "<<" || node.name == ">>") {
            if (*right < 0 || *right >= 32 || (node.name == "<<" && *left < 0)) return std::nullopt;
            const auto divisor = std::int64_t{1} << *right;
            value = node.name == "<<" ? *left * divisor : (*left >= 0 ? *left / divisor : -((-*left + divisor - 1) / divisor));
        }
        else if (node.name == "/" || node.name == "%") {
            if (!*right || (*left == std::numeric_limits<std::int32_t>::min() && *right == -1)) return std::nullopt;
            value = node.name == "/" ? *left / *right : *left % *right;
        } else if (node.name == "<") value = *left < *right;
        else if (node.name == "<=") value = *left <= *right;
        else if (node.name == ">") value = *left > *right;
        else if (node.name == ">=") value = *left >= *right;
        else if (node.name == "==") value = *left == *right;
        else if (node.name == "!=") value = *left != *right;
        else if (node.name == "&&") value = *left && *right;
        else if (node.name == "||") value = *left || *right;
        else return std::nullopt;
    } else return std::nullopt;
    const auto low = node.type->kind == TypeKind::Char ? -128 : std::numeric_limits<std::int32_t>::min();
    const auto high = node.type->kind == TypeKind::Char ? 127 : std::numeric_limits<std::int32_t>::max();
    if (value < low || value > high) return std::nullopt;
    return value;
}

// 解释器采用 32 位 int、8 位有符号 char 和 32 位 float。
inline std::int64_t checked_integer(std::int64_t value, TypeKind kind = TypeKind::Int) {
    const auto low = kind == TypeKind::Char ? -128 : std::numeric_limits<std::int32_t>::min();
    const auto high = kind == TypeKind::Char ? 127 : std::numeric_limits<std::int32_t>::max();
    if (value < low || value > high) throw std::runtime_error("整数结果超出类型范围");
    return value;
}

inline double checked_float(double value) {
    const float rounded = static_cast<float>(value);
    if (!std::isfinite(rounded)) throw std::runtime_error("浮点结果超出有限数范围");
    return rounded;
}

// 安全折叠数值常量，保留每一步 float 舍入；失败则保留原表达式。
inline std::optional<ConstantValue> folded_value(const ASTNode& node, std::size_t depth = 0) {
    if (depth > 128 || !numeric(node.type)) return std::nullopt;
    if (const auto integer = integer_constant(node, depth)) return ConstantValue{*integer};
    if (node.kind == NodeType::FloatLiteral) {
        if (const auto* value = std::get_if<double>(&node.value)) return ConstantValue{*value};
        return std::nullopt;
    }
    if (node.children.empty() || !node.children[0]) return std::nullopt;
    const auto left_value = folded_value(*node.children[0], depth + 1);
    if (!left_value) return std::nullopt;
    const auto number = [](const ConstantValue& value) {
        if (const auto* integer = std::get_if<std::int64_t>(&value)) return static_cast<double>(*integer);
        return std::get<double>(value);
    };
    const auto left = number(*left_value);
    double result;
    if (node.kind == NodeType::Cast || node.kind == NodeType::ImplicitCast) result = left;
    else if (node.kind == NodeType::UnaryOp) {
        if (node.name == "+") result = left;
        else if (node.name == "-") result = -left;
        else if (node.name == "!") result = !left;
        else return std::nullopt;
    } else if (node.kind == NodeType::BinaryOp && node.children.size() == 2 && node.children[1]) {
        if (node.name == "&&" && left == 0) return ConstantValue{std::int64_t{0}};
        if (node.name == "||" && left != 0) return ConstantValue{std::int64_t{1}};
        const auto right_value = folded_value(*node.children[1], depth + 1);
        if (!right_value) return std::nullopt;
        const auto right = number(*right_value);
        if (node.name == "+") result = left + right;
        else if (node.name == "-") result = left - right;
        else if (node.name == "*") result = left * right;
        else if (node.name == "/") { if (right == 0) return std::nullopt; result = left / right; }
        else if (node.name == "<") result = left < right;
        else if (node.name == "<=") result = left <= right;
        else if (node.name == ">") result = left > right;
        else if (node.name == ">=") result = left >= right;
        else if (node.name == "==") result = left == right;
        else if (node.name == "!=") result = left != right;
        else if (node.name == "&&") result = left != 0 && right != 0;
        else if (node.name == "||") result = left != 0 || right != 0;
        else return std::nullopt;
        // 整型运算失败时不能经 double 换一套规则后成功折叠。
        if (node.kind == NodeType::BinaryOp && std::holds_alternative<std::int64_t>(*left_value) &&
            std::holds_alternative<std::int64_t>(*right_value)) return std::nullopt;
    } else return std::nullopt;
    try {
        if (node.type->kind == TypeKind::Float) return ConstantValue{checked_float(result)};
        const auto truncated = std::trunc(result);
        if (!std::isfinite(truncated) || truncated < std::numeric_limits<std::int32_t>::min() ||
            truncated > std::numeric_limits<std::int32_t>::max()) return std::nullopt;
        return ConstantValue{checked_integer(static_cast<std::int64_t>(truncated), node.type->kind)};
    } catch (const std::runtime_error&) { return std::nullopt; }
}

inline int hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

// 将窄字符或字符串中的转义还原为字节。
inline std::string decode_string(const std::string& spelling, char quote) {
    if (spelling.size() < 2 || spelling.front() != quote || spelling.back() != quote)
        throw std::runtime_error("字面量缺少正确引号，或使用了尚不支持的宽字符前缀");
    std::string result;
    for (std::size_t i = 1; i + 1 < spelling.size(); ++i) {
        unsigned char value = static_cast<unsigned char>(spelling[i]);
        if (value == '\n' || value == '\r' || value == 0 || value == quote)
            throw std::runtime_error("字面量含有未转义的引号、换行或零字节");
        if (value == '\\') {
            if (++i + 1 >= spelling.size()) throw std::runtime_error("转义不完整");
            switch (spelling[i]) {
            case 'a': value = '\a'; break;
            case 'b': value = '\b'; break;
            case 'f': value = '\f'; break;
            case 'n': value = '\n'; break;
            case 'r': value = '\r'; break;
            case 't': value = '\t'; break;
            case 'v': value = '\v'; break;
            case '\\': case '\'': case '"': case '?': value = static_cast<unsigned char>(spelling[i]); break;
            default: {
                unsigned int number = 0;
                if (spelling[i] == 'x') {
                    std::size_t count = 0;
                    while (i + 2 < spelling.size() && hex_digit(spelling[i + 1]) >= 0) {
                        number = number * 16 + static_cast<unsigned int>(hex_digit(spelling[++i]));
                        if (number > 255) throw std::runtime_error("十六进制转义超出字节范围");
                        ++count;
                    }
                    if (count == 0) throw std::runtime_error("十六进制转义缺少数字");
                } else if (spelling[i] >= '0' && spelling[i] <= '7') {
                    number = static_cast<unsigned int>(spelling[i] - '0');
                    for (int count = 1; count < 3 && i + 2 < spelling.size() &&
                         spelling[i + 1] >= '0' && spelling[i + 1] <= '7'; ++count)
                        number = number * 8 + static_cast<unsigned int>(spelling[++i] - '0');
                    if (number > 255) throw std::runtime_error("八进制转义超出字节范围");
                } else throw std::runtime_error("不支持的转义字符");
                value = static_cast<unsigned char>(number);
                break;
            }
            }
        }
        result.push_back(static_cast<char>(value));
    }
    return result;
}

struct FormatPart {
    std::string text; // 直接输出或匹配的文字。
    char conversion = 0; // d、f、c、s；0 表示纯文字。
};

// M1 格式串支持 %d、%f、%c、printf 的 %s 及 %%。
inline std::vector<FormatPart> format_parts(const std::string& format, bool scanning) {
    std::vector<FormatPart> result;
    std::string text;
    for (std::size_t i = 0; i < format.size() && format[i] != 0; ++i) {
        if (format[i] != '%') { text += format[i]; continue; }
        if (++i >= format.size() || format[i] == 0) throw std::runtime_error("格式串末尾缺少转换符");
        if (format[i] == '%') { text += '%'; continue; }
        if (format[i] != 'd' && format[i] != 'f' && format[i] != 'c' &&
            !(format[i] == 's' && !scanning))
            throw std::runtime_error("M1 格式串仅支持 %d、%f、%c、printf 的 %s 和 %%");
        if (!text.empty()) { result.push_back({text, 0}); text.clear(); }
        result.push_back({{}, format[i]});
    }
    if (!text.empty()) result.push_back({text, 0});
    return result;
}

}
