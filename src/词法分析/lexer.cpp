// ============================================================================
// lexer.cpp —— 词法分析器的实现。
// ----------------------------------------------------------------------------
// 大致思路：用一个"确定性有限自动机(DFA)"来扫描源码。
//   - DFA 的表格（状态转移表、接受态表、规则表、关键字表）由脚本生成，
//     已经放在 lexer_dfa.hpp 里；不要手改，改词法规则请改 词法规则.json 后重新生成。
//   - 本文件只负责"驱动"这些表：一个字节一个字节往前走，走到最长匹配为止。
// ============================================================================
// 本模块的接口声明：lex 函数、LexResult 类型
#include "minic/lexer.hpp"
// 脚本生成的 DFA 表格（数据部分，不要手改）
#include "lexer_dfa.hpp"

namespace minic {

// ============================================================================
// lex —— 词法分析唯一的入口函数。
//   输入：source 源码全文；filename 文件名（只用于报错定位，默认 "<input>"）。
//   输出：LexResult{ tokens 单词表, diagnostics 报错表 }。
//   约定：tokens 末尾一定追加一个 END_OF_FILE，表示"输入到此结束"。
//   特点：遇到非法字符不会崩，而是记一条带行列号的中文报错，然后继续往下扫。
// ============================================================================
LexResult lex(const std::string& source, const std::string& filename) {
    // 最终要返回的结果：单词表 + 报错表
    LexResult result;
    // 当前扫到哪了（行号、列号、字节偏移），从文件开头开始
    SourceLocation location;
    // 上一个字节是不是回车 \r，用于正确处理 Windows 的 \r\n 换行
    bool previous_cr = false;
    // ------------------------------------------------------------------------
    // 内部小工具 advance(end)：把"当前位置"推进到第 end 个字节，
    // 推进过程中顺便数出换了多少行、走到了第几列。
    // 为什么需要它：DFA 匹配结束时只知道"字节位置"，但报错要给"行号列号"。
    // ------------------------------------------------------------------------
    const auto advance = [&](std::size_t end) {
        while (location.offset < end) {
            const unsigned char byte = static_cast<unsigned char>(source[location.offset++]);
            // 回车：换一行，列号归 1
            if (byte == '\r') { ++location.line; location.col = 1; }
            // 换行：\r\n 只算一行，避免 Windows 换行被数成两行
            else if (byte == '\n') { if (!previous_cr) ++location.line; location.col = 1; }
            // 普通字符：列号 +1
            else ++location.col;
            // 记下当前字节是不是回车，供下一个字节判断
            previous_cr = byte == '\r';
        }
    };
    // ------------------------------------------------------------------------
    // 主循环：只要源码还没扫完，就切出下一个单词。
    // ------------------------------------------------------------------------
    while (location.offset < source.size()) {
        // 本单词的起点（行、列、字节偏移）
        const auto begin = location;
        // state：DFA 当前状态；rule_id：命中的规则编号
        int state = 0, rule_id = -1;
        // cursor：正在探查的字节；end：目前最远的一个"可接受"结束位置
        std::size_t cursor = begin.offset, end = cursor;
        // 最长匹配：记住最后接受态，失败时退回其结束位置。
        while (cursor < source.size()) {
            const auto byte = static_cast<unsigned char>(source[cursor]);
            // 查转移表：当前状态 + 字节类别 → 下一个状态
            const int next = lexer_detail::transitions[state][lexer_detail::byte_class[byte]];
            // 走不通就停下
            if (next < 0) break;
            state = next;
            ++cursor;
            // 是接受态：先记下规则编号和结束位置；后面若能走更远会被覆盖
            if (lexer_detail::accepting[state] >= 0) {
                rule_id = lexer_detail::accepting[state];
                end = cursor;
            }
        }
        // INVALID_BYTE 覆盖全部字节，因此总能推进至少一个字节。
        // 取出最终命中的规则（产出 Token / 跳过 / 报错 三种之一）
        const auto& rule = lexer_detail::rules[rule_id];
        // 把行列号推进到本单词结束的位置
        advance(end);
        // 本单词在源码中的位置范围（文件名 + 起始位置 + 结束位置）
        const SourceRange range{filename, begin, location};
        // 分支一：正常产出 Token
        if (rule.action == lexer_detail::Action::Token) {
            // 先按规则给出的 Token 类型
            auto type = rule.token;
            // 截出这个词在源码里的原文
            const auto text = source.substr(begin.offset, end - begin.offset);
            // 写法上和标识符一样的还可能是关键字（例如 int、float），
            // 所以这里查一遍关键字表，把类型改成关键字。
            if (type == TokenType::ID) {
                for (const auto& keyword : lexer_detail::keywords)
                    if (text == keyword.spelling) { type = keyword.token; break; }
            }
            // 收进单词表：类型、原文、起始行列、位置范围
            result.tokens.push_back({type, text, begin.line, begin.col, range});
        // 分支二：命中报错规则（例如出现了非法字符）
        } else if (rule.action == lexer_detail::Action::Error) {
            result.diagnostics.push_back({Phase::Lexer, Level::Error, begin.line, begin.col,
                                          rule.message, rule.code, range, {}});
            if (result.diagnostics.size() == 20) {
                // 报错达到上限（20 条）就停止本阶段，避免错误刷屏
                result.diagnostics.back().message += "；错误过多，停止本阶段";
                break;
            }
        }
        // Action::Skip 的规则（例如空格、制表符）不产出 Token、也不报错，直接进入下一轮。
    }
    // 末尾固定补一个 END_OF_FILE，告诉后续阶段"输入到这里就结束了"。
    result.tokens.push_back({TokenType::END_OF_FILE, "", location.line, location.col,
                             {filename, location, location}});
    return result;
}

}
