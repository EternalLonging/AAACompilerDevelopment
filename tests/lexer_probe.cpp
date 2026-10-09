#include "minic/lexer.hpp"
#include <iostream>
#include <sstream>

// 测试用字节协议，允许对照正则检查零字节及全部 256 种输入。
static std::string hex(const std::string& text) {
    static const char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned char byte : text) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}
static void range(std::ostream& out, const minic::SourceRange& r) {
    out << r.begin.line << ',' << r.begin.col << ',' << r.begin.offset << ','
        << r.end.line << ',' << r.end.col << ',' << r.end.offset;
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::string source;
        for (std::size_t i = 0; i < line.size(); i += 2)
            source += static_cast<char>(std::stoul(line.substr(i, 2), nullptr, 16));
        const auto result = minic::lex(source, "probe.c");
        std::ostringstream out;
        for (const auto& t : result.tokens) {
            out << 'T' << static_cast<int>(t.type) << ','; range(out, t.range);
            out << ',' << hex(t.lexeme) << '|';
        }
        for (const auto& d : result.diagnostics) {
            out << 'E' << d.code << ','; range(out, d.range); out << '|';
        }
        std::cout << out.str() << '\n';
    }
}
