#pragma once
#include "minic/interface.hpp"
#include "minic/semantic.hpp"

#include <cmath>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>

namespace minic::detail {

// 建立一个不带限定符的基本类型。
inline TypePtr type(TypeKind kind) {
    auto result = make_type_info();
    result->kind = kind;
    return result;
}

inline bool floating(const TypePtr& value) {
    return value && !value->is_unsigned && (value->kind == TypeKind::Float || value->kind == TypeKind::Double || value->kind == TypeKind::LongDouble);
}
inline bool integral(const TypePtr& value) {
    return value && (value->kind == TypeKind::Char || value->kind == TypeKind::Short || value->kind == TypeKind::Int || value->kind == TypeKind::Long);
}
inline bool numeric(const TypePtr& value) { return integral(value) || floating(value); }
inline int integer_bits(const TypePtr& value) {
    return value->kind == TypeKind::Char ? 8 : value->kind == TypeKind::Short ? 16 : 32;
}
inline TypePtr unqualified(const TypePtr& value) {
    auto result = make_type_info(*value); result->is_const = false; return result;
}

// 忽略最外层的 const 比较类型；局部副本用完即释放。
inline bool same_unqualified(TypePtr left, TypePtr right) {
    if (!left || !right) return false;
    auto a = *left, b = *right;
    a.is_const = b.is_const = false;
    return same_type(&a, &b);
}

inline std::string symbol_name(SymbolId id) { return "%s" + std::to_string(id); }

inline TypePtr address_type(TypePtr base) {
    auto result = make_type_info();
    result->kind = TypeKind::Address;
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
    if (integral(value)) { const auto size = static_cast<std::size_t>(integer_bits(value) / 8); return {size, size}; }
    if (floating(value)) return value->kind == TypeKind::Float ? Layout{4, 4} : Layout{8, 8};
    if (value->kind == TypeKind::Address && value->base) return {8, 8};
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

inline bool address_compatible(const TypePtr& target, const TypePtr& source) {
    if (!target || !source || target->kind != TypeKind::Address || source->kind != TypeKind::Address ||
        !target->base || !source->base) return false;
    if (source->base->is_const && !target->base->is_const) return false;
    auto left = *target->base, right = *source->base;
    left.is_const = right.is_const = false;
    return same_type(&left, &right);
}

// 读取纯整型常量表达式；不执行调用、赋值或变量读取。
inline std::optional<std::int64_t> integer_constant(const ASTNode& node, std::size_t depth = 0) {
    if (depth > 128 || !integral(node.type)) return std::nullopt;
    if (node.kind == NodeType::IntLiteral || node.kind == NodeType::CharLiteral) {
        if (const auto* value = std::get_if<std::int64_t>(&node.value)) return *value;
        if (const auto* value = std::get_if<std::uint64_t>(&node.value); value && *value <= std::numeric_limits<std::uint32_t>::max()) return static_cast<std::int64_t>(*value);
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
        else if (node.name == "*") {
            if (node.type->is_unsigned) value = static_cast<std::int64_t>((static_cast<std::uint64_t>(*left) * static_cast<std::uint64_t>(*right)) & ((std::uint64_t{1} << integer_bits(node.type)) - 1));
            else value = *left * *right;
        }
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
    const auto bits = integer_bits(node.type);
    if (node.type->is_unsigned) return static_cast<std::int64_t>(static_cast<std::uint64_t>(value) & ((std::uint64_t{1} << bits) - 1));
    const auto low = -(std::int64_t{1} << (bits - 1));
    const auto high = (std::int64_t{1} << (bits - 1)) - 1;
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
    // 新的整数宽度及 unsigned 保留运行求值，避免套用旧的 32 位折叠规则。
    if (depth > 128 || !numeric(node.type) || node.type->is_unsigned || node.type->kind == TypeKind::Long || node.type->kind == TypeKind::Short) return std::nullopt;
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
        if (floating(node.type)) return std::isfinite(result) ? std::optional<ConstantValue>{node.type->kind == TypeKind::Float ? ConstantValue{checked_float(result)} : ConstantValue{result}} : std::nullopt;
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
inline std::string decode_literal_segment(const std::string& spelling, char quote) {
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

// 相邻字符串逐段解码后拼接，避免前一段十六进制转义吞入后一段数字。
inline std::string decode_string(const std::string& spelling, char quote) {
    if (quote != '"') return decode_literal_segment(spelling, quote);
    std::string result;
    std::size_t pos = 0;
    while (pos < spelling.size()) {
        if (spelling[pos] != '"') throw std::runtime_error("字符串需要引号，宽字符串尚不支持");
        const auto begin = pos++;
        bool closed = false;
        while (pos < spelling.size()) {
            const char c = spelling[pos++];
            if (c == '\\' && pos < spelling.size()) ++pos;
            else if (c == '"') { closed = true; break; }
        }
        if (!closed) throw std::runtime_error("字符串缺少结束引号");
        result += decode_literal_segment(spelling.substr(begin, pos - begin), quote);
        while (pos < spelling.size() && std::isspace(static_cast<unsigned char>(spelling[pos]))) ++pos;
    }
    if (spelling.empty()) throw std::runtime_error("字符串缺少引号");
    return result;
}

struct FormatPart {
    std::string text; // 直接输出或匹配的文字。
    char conversion = 0; // d、f、c、s；0 表示纯文字。
    std::size_t width = 0; // 最大输入或输出字符数，0 表示未指定。
    char length = 0; // h、l、L 长度修饰符，0 表示默认类型。
};

// M1 格式串支持 %d、%f、%c、printf 的 %s 及 %%。
inline std::vector<FormatPart> format_parts(const std::string& format, bool scanning) {
    std::vector<FormatPart> result;
    std::string text;
    for (std::size_t i = 0; i < format.size() && format[i] != 0; ++i) {
        if (format[i] != '%') { text += format[i]; continue; }
        if (++i >= format.size() || format[i] == 0) throw std::runtime_error("格式串末尾缺少转换符");
        if (format[i] == '%') { text += '%'; continue; }
        std::size_t width = 0;
        const bool has_width = format[i] >= '0' && format[i] <= '9';
        while (i < format.size() && format[i] >= '0' && format[i] <= '9') {
            width = width * 10 + static_cast<std::size_t>(format[i++] - '0');
            if (width > max_object_size) throw std::runtime_error("格式宽度超出限制");
        }
        char length = 0;
        if (has_width && !width) throw std::runtime_error("格式宽度必须大于零");
        if (i < format.size() && (format[i] == 'h' || format[i] == 'l' || format[i] == 'L')) length = format[i++];
        if (i >= format.size() || (format[i] != 'd' && format[i] != 'u' && format[i] != 'x' && format[i] != 'o' &&
            format[i] != 'f' && format[i] != 'c' && format[i] != 's'))
            throw std::runtime_error("不支持该格式转换符");
        if ((format[i] == 's' || format[i] == 'c') && length) throw std::runtime_error("暂不支持宽字符格式");
        if (format[i] != 'f' && length == 'L') throw std::runtime_error("L 仅能修饰浮点格式");
        if (format[i] == 'f' && length == 'h') throw std::runtime_error("h 不能修饰浮点格式");
        if (!scanning && width) throw std::runtime_error("printf 宽度格式尚未支持");
        if (scanning && width && format[i] != 's') throw std::runtime_error("当前输入宽度只支持 %s");
        if (!text.empty()) { result.push_back({text, 0}); text.clear(); }
        result.push_back({{}, format[i], width, length});
    }
    if (!text.empty()) result.push_back({text, 0});
    return result;
}

inline TypePtr format_type(const FormatPart& part, bool scanning) {
    auto result = make_type_info();
    if (part.conversion == 'f') result->kind = part.length == 'L' ? TypeKind::LongDouble : part.length == 'l' ? TypeKind::Double : TypeKind::Float;
    else if (part.conversion == 'c' || part.conversion == 's') result->kind = TypeKind::Char;
    else result->kind = part.length == 'l' ? TypeKind::Long : part.length == 'h' && scanning ? TypeKind::Short : TypeKind::Int;
    result->is_unsigned = part.conversion == 'u' || part.conversion == 'x' || part.conversion == 'o';
    return result;
}

}
