#include "minic/parser.hpp"
#include "minic/detail/predictive_table.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace minic {
namespace {
using TT = TokenType;
using Node = ASTNode*;
using namespace predictive;
struct ParseFailure {};

// 查预测表识别语法；产生式匹配完成后，把孩子的结果组合成 AST。
class Parser {
    // 保存一次文法符号的匹配结果；下标连接孩子，避免递归调用。
    struct Match {
        int symbol; // 终结符或非终结符编号。
        std::size_t begin = 0, end = 0; // 匹配的单词区间，右端不包含。
        std::vector<std::size_t> children; // 产生式右部，保持文法顺序。
        Node ast = nullptr; // 此符号组合出的 AST 节点。
        TypePtr type = nullptr; // 类型说明符组合出的类型。
        std::vector<Node> declarations; // 一条声明中的结构体、变量或成员。
        bool unary_form = false; // 表达式是否具有一元式形状，供赋值左部检查。
        explicit Match(int value) : symbol(value) {}
    };
    struct Frame {
        std::size_t match; // 此栈项对应的匹配结果下标。
        bool reduce; // false 表示匹配符号，true 表示组合已匹配的孩子。
    };
    const std::vector<Token>& tokens; // 词法阶段给出的单词表，末尾为 EOF。
    std::size_t pos = 0; // 下一个输入单词的下标。
    unsigned nesting = 0; // 当前尚未闭合的括号层数。
    ParseResult result; // 返回的 AST 内存池、根节点和诊断。
    std::vector<Match> matches; // 匹配结果按下标保存，扩容不会改变下标。
    std::vector<Frame> stack; // 待匹配符号和待执行的归约动作。
    std::unordered_map<Node, unsigned> heights; // 已组合 AST 节点的高度。

    // 记录当前位置的错误，最多保留 20 条诊断。
    void report(const std::string& message, const std::string& code = "PARSE_EXPECTED",
                Level level = Level::Error, std::size_t at = std::numeric_limits<std::size_t>::max()) {
        const auto& token = tokens[at == std::numeric_limits<std::size_t>::max() ? pos : at];
        result.diagnostics.push_back({Phase::Parser, level, token.line, token.col,
                                      message, code, token.range, {}});
        if (result.diagnostics.size() == 20)
            result.diagnostics.back().message += "；错误过多，停止本阶段";
    }
    // 记录无法继续的 AST 约束错误，返回主循环处理。
    [[noreturn]] void fail(const std::string& message, const std::string& code,
                           Level level = Level::Error, std::size_t at = std::numeric_limits<std::size_t>::max()) {
        report(message, code, level, at);
        throw ParseFailure{};
    }
    // 判断是否已达到错误上限或出现致命错误。
    bool stopped() const {
        return result.diagnostics.size() >= 20 ||
            (!result.diagnostics.empty() && result.diagnostics.back().level == Level::Fatal);
    }
    // 返回文法孩子的匹配结果。
    Match& child(Match& m, std::size_t i) { return matches[m.children[i]]; }
    // 返回匹配结果的首个单词。
    const Token& first(const Match& m) const { return tokens[m.begin]; }
    // 创建 AST 节点并填写起始位置。
    Node node(NodeType kind, const Token& start, const std::string& name = "") {
        auto n = result.arena.make();
        n->kind = kind; n->name = name; n->range = start.range;
        heights[n] = 1;
        return n;
    }
    // 设置 AST 结束位置并检查树高；end 为单词区间右端。
    void finish(Node n, std::size_t end) {
        if (end) n->range.end = tokens[end - 1].range.end;
        unsigned height = 1;
        for (auto c : n->children) height = std::max(height, 1 + heights.at(c));
        if (height > 128) fail("语法树嵌套超过 128 层", "PARSE_DEPTH", Level::Fatal);
        heights[n] = height;
    }
    // 根据已匹配的类型单词建立基础类型。
    TypePtr specs(const Match& m) {
        auto t = make_type_info();
        switch (first(m).type) {
        case TT::KW_VOID: t->kind = TypeKind::Void; break;
        case TT::KW_CHAR: t->kind = TypeKind::Char; break;
        case TT::KW_INT: t->kind = TypeKind::Int; break;
        case TT::KW_FLOAT: t->kind = TypeKind::Float; break;
        default: t->kind = TypeKind::Struct; t->name = tokens[m.begin + 1].lexeme; break;
        }
        return t;
    }
    // 在基础类型外加一层已匹配的数组，并检查界限数值。
    TypePtr declarator(TypePtr base, Match& array) {
        if (array.begin == array.end) return base;
        auto t = make_type_info(); t->kind = TypeKind::Array; t->base = base;
        const auto& size = tokens[array.begin + 1];
        if (size.type != TT::RBRACKET) {
            std::size_t used = 0;
            unsigned long long value = 0;
            try { value = std::stoull(size.lexeme, &used, 0); }
            catch (const std::exception&) { fail("数组长度超出范围", "PARSE_ARRAY_BOUND", Level::Error, array.begin + 1); }
            if (used != size.lexeme.size()) fail("数组长度不能带 U 和 L 后缀", "PARSE_ARRAY_BOUND", Level::Error, array.begin + 1);
            if (!value || value > std::numeric_limits<std::size_t>::max())
                fail("数组长度必须为可表示的正整数", "PARSE_ARRAY_BOUND", Level::Error, array.begin + 1);
            t->array_length = static_cast<std::size_t>(value);
        }
        return t;
    }
    // 用类型、名字、可选数组和可选初值建立一个变量或成员。
    Node object(NodeType kind, const Token& start, TypePtr type, Match& id,
                Match& array, Match* init) {
        auto n = node(kind, start, first(id).lexeme);
        n->declared_type = declarator(type, array);
        std::size_t end = array.end;
        if (init && init->ast) { n->children.push_back(init->ast); end = init->end; }
        finish(n, end);
        return n;
    }
    // 展开已匹配的对象续表，按源码顺序构造同一声明中的变量。
    void objects(Match& m, TypePtr type, Match& id, Match& tail) {
        m.declarations.push_back(object(NodeType::VarDecl, first(m), type, id,
                                        child(tail, 0), &child(tail, 1)));
        auto rest = &child(tail, 2);
        while (!rest->children.empty()) {
            m.declarations.push_back(object(NodeType::VarDecl, first(m), type, child(*rest, 1),
                                            child(*rest, 2), &child(*rest, 3)));
            rest = &child(*rest, 4);
        }
        finish(m.declarations.back(), m.end);
    }
    // 组合一条已匹配的声明；此函数不读取或推进输入。
    void declaration(Match& m) {
        if (m.symbol == 成员声明) {
            auto type = child(m, 0).type;
            m.declarations.push_back(object(NodeType::MemberDecl, first(m), type,
                                            child(m, 1), child(m, 2), nullptr));
            auto rest = &child(m, 3);
            while (!rest->children.empty()) {
                m.declarations.push_back(object(NodeType::MemberDecl, first(m), type,
                                                child(*rest, 1), child(*rest, 2), nullptr));
                rest = &child(*rest, 3);
            }
            finish(m.declarations.back(), m.end);
            return;
        }
        auto type = specs(m);
        auto id = &child(m, 1);
        auto tail = &child(m, 2);
        if (first(m).type == TT::KW_STRUCT) {
            if (first(*tail).type == TT::LBRACE) {
                auto def = node(NodeType::StructDef, first(m), first(*id).lexeme);
                def->children = child(*tail, 1).declarations;
                auto rest = &child(*tail, 2);
                while (!rest->children.empty()) {
                    const auto& members = child(*rest, 0).declarations;
                    def->children.insert(def->children.end(), members.begin(), members.end());
                    rest = &child(*rest, 1);
                }
                finish(def, child(*tail, 3).end);
                m.declarations.push_back(def);
                auto& after = child(*tail, 4);
                if (after.children.size() == 1) { finish(def, m.end); return; }
                id = &child(after, 0); tail = &child(after, 1);
            } else {
                id = &child(*tail, 0); tail = &child(*tail, 1);
            }
        } else if (tail->symbol == 外部声明尾) {
            tail = &child(*tail, 0);
        }
        if (tail->symbol != 函数尾) { objects(m, type, *id, *tail); return; }
        auto fn = node(NodeType::FunctionDef, first(m), first(*id).lexeme);
        auto fn_type = make_type_info(); fn_type->kind = TypeKind::Function; fn_type->base = type;
        auto& params = child(*tail, 1);
        if (params.children.size() == 2) {
            fn->children.push_back(child(params, 0).ast);
            auto rest = &child(params, 1);
            while (!rest->children.empty()) {
                fn->children.push_back(child(*rest, 1).ast); rest = &child(*rest, 2);
            }
        }
        for (auto p : fn->children) fn_type->params.push_back(p->declared_type);
        fn->declared_type = fn_type;
        fn->children.push_back(child(*tail, 3).ast);
        finish(fn, m.end); m.declarations.push_back(fn);
    }
    // 将已匹配的局部声明和语句列表组合成块。
    void block(Match& m) {
        m.ast = node(NodeType::Block, first(m));
        auto rest = &child(m, 1);
        while (!rest->children.empty()) {
            const auto& declarations = child(*rest, 0).declarations;
            m.ast->children.insert(m.ast->children.end(), declarations.begin(), declarations.end());
            rest = &child(*rest, 1);
        }
        rest = &child(m, 2);
        while (!rest->children.empty()) {
            m.ast->children.push_back(child(*rest, 0).ast); rest = &child(*rest, 1);
        }
        finish(m.ast, m.end);
    }
    // 按已选语句产生式组合控制流节点，保持公共 AST 孩子顺序。
    void statement(Match& m) {
        const auto token = first(m).type;
        if (token == TT::LBRACE) { m.ast = child(m, 0).ast; return; }
        NodeType kind = NodeType::ExprStmt;
        switch (token) {
        case TT::SEMI: kind = NodeType::Empty; break;
        case TT::KW_IF: kind = NodeType::If; break;
        case TT::KW_WHILE: kind = NodeType::While; break;
        case TT::KW_FOR: kind = NodeType::For; break;
        case TT::KW_RETURN: kind = NodeType::Return; break;
        case TT::KW_BREAK: kind = NodeType::Break; break;
        case TT::KW_CONTINUE: kind = NodeType::Continue; break;
        default: break;
        }
        m.ast = node(kind, first(m));
        if (kind == NodeType::If || kind == NodeType::While) {
            m.ast->children = {child(m, 2).ast, child(m, 4).ast};
            if (kind == NodeType::If && child(m, 5).ast) m.ast->children.push_back(child(m, 5).ast);
        } else if (kind == NodeType::For) {
            for (auto i : {2, 4, 6}) {
                auto& expr = child(m, i);
                m.ast->children.push_back(expr.ast ? expr.ast : node(NodeType::Empty, first(expr)));
            }
            m.ast->children.push_back(child(m, 8).ast);
        } else if (kind == NodeType::Return) {
            if (child(m, 1).ast) m.ast->children.push_back(child(m, 1).ast);
        } else if (kind == NodeType::ExprStmt) m.ast->children.push_back(child(m, 0).ast);
        finish(m.ast, m.end);
    }
    // 按文法层级组合表达式：普通二元尾表左折叠，赋值右结合。
    void expression(Match& m) {
        auto& left = child(m, 0);
        m.ast = left.ast; m.unary_form = left.unary_form;
        auto rest = &child(m, 1);
        if (m.symbol == 表达式) {
            if (rest->children.empty()) return;
            if (!m.unary_form) fail("赋值左部必须符合一元表达式形状", "PARSE_ASSIGNMENT_TARGET", Level::Error, rest->begin);
            auto n = node(NodeType::Assign, first(*rest), "=");
            n->range.begin = m.ast->range.begin; n->range.file = m.ast->range.file;
            n->children = {m.ast, child(*rest, 1).ast};
            finish(n, m.end); m.ast = n; m.unary_form = false;
            return;
        }
        while (!rest->children.empty()) {
            auto& right = child(*rest, 1);
            auto n = node(NodeType::BinaryOp, first(*rest), first(*rest).lexeme);
            n->range.begin = m.ast->range.begin; n->range.file = m.ast->range.file;
            n->children = {m.ast, right.ast};
            finish(n, right.end); m.ast = n; m.unary_form = false;
            rest = &child(*rest, 2);
        }
    }
    // 组合前缀、一元主式和字面量；括号主式保留一元式形状。
    void unary(Match& m) {
        m.unary_form = true;
        if (m.symbol == 一元式) {
            if (m.children.size() == 1) m.ast = child(m, 0).ast;
            else {
                m.ast = node(NodeType::UnaryOp, first(m), first(m).lexeme);
                m.ast->children.push_back(child(m, 1).ast); finish(m.ast, m.end);
            }
            return;
        }
        if (m.symbol == 字符串) {
            m.ast = node(NodeType::StringLiteral, first(m), first(m).lexeme);
            for (auto i = m.begin + 1; i < m.end; ++i) m.ast->name += " " + tokens[i].lexeme;
        } else if (first(m).type == TT::LPAREN) {
            m.ast = child(m, 1).ast; m.ast->range.begin = first(m).range.begin;
        } else if (child(m, 0).symbol == 字符串) {
            m.ast = child(m, 0).ast;
        } else {
            NodeType kind = NodeType::Identifier;
            switch (first(m).type) {
            case TT::INT_LITERAL:
                if (first(m).lexeme.find_first_of("uUlL") != std::string::npos)
                    fail("整数常量不接受后缀", "PARSE_LITERAL", Level::Error, m.begin);
                kind = NodeType::IntLiteral; break;
            case TT::FLOAT_LITERAL:
                if (first(m).lexeme.find_last_of("lL") == first(m).lexeme.size() - 1)
                    fail("浮点常量不接受 L 后缀", "PARSE_LITERAL", Level::Error, m.begin);
                kind = NodeType::FloatLiteral; break;
            case TT::CHAR_LITERAL: kind = NodeType::CharLiteral; break;
            default: break;
            }
            m.ast = node(kind, first(m), first(m).lexeme);
        }
        finish(m.ast, m.end);
    }
    // 按源码顺序把已匹配的调用、下标和成员访问包在主式外面。
    void postfix(Match& m) {
        m.ast = child(m, 0).ast; m.unary_form = true;
        auto rest = &child(m, 1);
        while (!rest->children.empty()) {
            auto& suffix = child(*rest, 0);
            const auto op = first(suffix).type;
            Node n;
            if (op == TT::LPAREN) {
                n = node(NodeType::Call, first(suffix), m.ast->kind == NodeType::Identifier ? m.ast->name : "");
                n->children.push_back(m.ast);
                auto args = &child(suffix, 1);
                if (!args->children.empty()) {
                    n->children.push_back(child(*args, 0).ast); args = &child(*args, 1);
                    while (!args->children.empty()) {
                        n->children.push_back(child(*args, 1).ast); args = &child(*args, 2);
                    }
                }
            } else if (op == TT::LBRACKET) {
                n = node(NodeType::ArrayAccess, first(suffix));
                n->children = {m.ast, child(suffix, 1).ast};
            } else {
                n = node(NodeType::MemberAccess, first(suffix), first(child(suffix, 1)).lexeme);
                n->children.push_back(m.ast);
            }
            n->range.begin = m.ast->range.begin; n->range.file = m.ast->range.file;
            finish(n, suffix.end); m.ast = n;
            rest = &child(*rest, 1);
        }
    }
    // 执行已完整匹配的产生式动作；不查输入，不再识别语法。
    void reduce(std::size_t index) {
        auto& m = matches[index];
        switch (m.symbol) {
        case 程序:
            if (index == 0) {
                m.ast = node(NodeType::Program, first(m));
                auto rest = &m;
                while (!rest->children.empty()) {
                    const auto& decls = child(*rest, 0).declarations;
                    m.ast->children.insert(m.ast->children.end(), decls.begin(), decls.end());
                    rest = &child(*rest, 1);
                }
                finish(m.ast, m.end); m.ast->range.end = tokens[pos].range.end;
            }
            break;
        case 标量类型: case 成员类型: m.type = specs(m); break;
        case 外部声明: case 局部声明: case 成员声明: declaration(m); break;
        case 形参: {
            const bool record = first(m).type == TT::KW_STRUCT;
            auto& id = child(m, record ? 2 : 1);
            m.ast = node(NodeType::ParamDecl, first(m), first(id).lexeme);
            if (record) {
                Match array(static_cast<int>(TT::LBRACKET)); // struct 形参在文法中必须是数组。
                array.begin = child(m, 3).begin; array.end = child(m, 5).end;
                m.ast->declared_type = declarator(specs(m), array);
            } else m.ast->declared_type = declarator(specs(m), child(m, 2));
            finish(m.ast, m.end); break;
        }
        case 块: block(m); break;
        case 语句: statement(m); break;
        case 初值可选: case 否则可选:
            if (!m.children.empty()) m.ast = child(m, 1).ast;
            break;
        case 表达式可选:
            if (!m.children.empty()) m.ast = child(m, 0).ast;
            break;
        case 表达式: case 逻辑或式: case 逻辑与式: case 相等式: case 关系式: case 加法式: case 乘法式:
            expression(m); break;
        case 一元式: case 主式: case 字符串: unary(m); break;
        case 后缀式: postfix(m); break;
        default: break; // 列表和尾部由所属父节点遍历，不反复复制列表。
        }
    }
    // 非终结符错误时跳到其预测项或 FOLLOW；终结符错误时插入或跳过。
    void recover(int symbol) {
        const auto look = static_cast<int>(tokens[pos].type);
        report(std::string("需要 ") + names[symbol] + "，实际遇到 " +
               (look == 0 ? "文件结束" : tokens[pos].lexeme));
        if (symbol >= terminal_count) {
            const auto row = symbol - terminal_count;
            while (tokens[pos].type != TT::END_OF_FILE) {
                const auto t = static_cast<int>(tokens[pos].type);
                if (table[row][t] >= 0) return;
                if (follow[row][t]) break;
                ++pos;
            }
            stack.pop_back();
        } else {
            const auto t = tokens[pos].type;
            if (t == TT::END_OF_FILE || t == TT::SEMI || t == TT::RBRACE ||
                t == TT::RPAREN || t == TT::RBRACKET) stack.pop_back();
            else ++pos;
        }
    }

public:
    // 保存单词表引用，解析期间不改变原始单词。
    explicit Parser(const std::vector<Token>& input) : tokens(input) {}
    // 分析栈循环：匹配终结符、查表展开非终结符、完成后组合 AST。
    ParseResult run() {
        matches.emplace_back(程序); matches.emplace_back(static_cast<int>(TT::END_OF_FILE));
        stack.push_back({1, false}); stack.push_back({0, false});
        while (!stack.empty() && !stopped()) {
            const auto frame = stack.back();
            const auto symbol = matches[frame.match].symbol;
            if (frame.reduce) {
                stack.pop_back(); matches[frame.match].end = pos;
                if (result.diagnostics.empty()) {
                    try { reduce(frame.match); } catch (const ParseFailure&) {}
                }
                continue;
            }
            const auto look = static_cast<int>(tokens[pos].type);
            if (symbol < terminal_count) {
                if (symbol != look) { recover(symbol); continue; }
                stack.pop_back(); matches[frame.match].begin = pos;
                if (look != 0) {
                    if (tokens[pos].type == TT::LPAREN || tokens[pos].type == TT::LBRACE || tokens[pos].type == TT::LBRACKET) {
                        if (++nesting > 128) { report("括号嵌套超过 128 层", "PARSE_DEPTH", Level::Fatal); break; }
                    } else if (nesting && (tokens[pos].type == TT::RPAREN || tokens[pos].type == TT::RBRACE || tokens[pos].type == TT::RBRACKET)) --nesting;
                    ++pos;
                }
                matches[frame.match].end = pos;
                continue;
            }
            const int choice = table[symbol - terminal_count][look];
            if (choice < 0) { recover(symbol); continue; }
            stack.pop_back(); matches[frame.match].begin = pos;
            stack.push_back({frame.match, true});
            const auto& production = productions[choice];
            for (int i = 0; i < production.length; ++i) {
                const auto index = matches.size();
                matches.emplace_back(production.rhs[i]);
                matches[frame.match].children.push_back(index);
            }
            for (auto it = matches[frame.match].children.rbegin(); it != matches[frame.match].children.rend(); ++it)
                stack.push_back({*it, false});
        }
        if (result.diagnostics.empty()) result.root = matches[0].ast;
        return std::move(result);
    }
};
}

ParseResult parse(const std::vector<Token>& tokens) {
    if (tokens.empty() || tokens.back().type != TT::END_OF_FILE ||
        std::any_of(tokens.begin(), tokens.end() - 1, [](const Token& t) { return t.type == TT::END_OF_FILE; }) ||
        std::any_of(tokens.begin(), tokens.end(), [](const Token& t) {
            return static_cast<int>(t.type) < 0 || static_cast<int>(t.type) >= terminal_count;
        })) {
        ParseResult result;
        const auto range = tokens.empty() ? SourceRange{} : tokens.back().range;
        result.diagnostics.push_back({Phase::Parser, Level::Error, range.begin.line, range.begin.col,
            "单词表必须使用有效种别，且只包含一个末尾 EOF", "PARSE_TOKEN_STREAM", range, {}});
        return result;
    }
    return Parser(tokens).run();
}
}
