#pragma once
#include "minic/interface.hpp"

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
