#include "minic/lexer.hpp"
#include "lexer_dfa.hpp"

namespace minic {

LexResult lex(const std::string& source, const std::string& filename) {
    LexResult result;
    SourceLocation location;
    bool previous_cr = false;
    const auto advance = [&](std::size_t end) {
        while (location.offset < end) {
            const unsigned char byte = static_cast<unsigned char>(source[location.offset++]);
            if (byte == '\r') { ++location.line; location.col = 1; }
            else if (byte == '\n') { if (!previous_cr) ++location.line; location.col = 1; }
            else ++location.col;
            previous_cr = byte == '\r';
        }
    };
    while (location.offset < source.size()) {
        const auto begin = location;
        int state = 0, rule_id = -1;
        std::size_t cursor = begin.offset, end = cursor;
        // 最长匹配：记住最后接受态，失败时退回其结束位置。
        while (cursor < source.size()) {
            const auto byte = static_cast<unsigned char>(source[cursor]);
            const int next = lexer_detail::transitions[state][lexer_detail::byte_class[byte]];
            if (next < 0) break;
            state = next;
            ++cursor;
            if (lexer_detail::accepting[state] >= 0) {
                rule_id = lexer_detail::accepting[state];
                end = cursor;
            }
        }
        // INVALID_BYTE 覆盖全部字节，因此总能推进至少一个字节。
        const auto& rule = lexer_detail::rules[rule_id];
        advance(end);
        const SourceRange range{filename, begin, location};
        if (rule.action == lexer_detail::Action::Token) {
            auto type = rule.token;
            const auto text = source.substr(begin.offset, end - begin.offset);
            if (type == TokenType::ID) {
                for (const auto& keyword : lexer_detail::keywords)
                    if (text == keyword.spelling) { type = keyword.token; break; }
            }
            result.tokens.push_back({type, text, begin.line, begin.col, range});
        } else if (rule.action == lexer_detail::Action::Error) {
            result.diagnostics.push_back({Phase::Lexer, Level::Error, begin.line, begin.col,
                                          rule.message, rule.code, range, {}});
            if (result.diagnostics.size() == 20) {
                result.diagnostics.back().message += "；错误过多，停止本阶段";
                break;
            }
        }
    }
    result.tokens.push_back({TokenType::END_OF_FILE, "", location.line, location.col,
                             {filename, location, location}});
    return result;
}

}
