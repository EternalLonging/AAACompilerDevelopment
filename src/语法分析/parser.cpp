#include "minic/parser.hpp"
#include "predictive_table.hpp"

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

// 分析栈查预测表，语义值栈保存已完成的结果；不建立中间语法树。
class Parser {
    struct Value {
        std::size_t begin = 0, end = 0; // 已匹配的单词区间，右端不包含。
        Node ast = nullptr; // 表达式、语句或待补名字的函数、结构体。
        TypePtr type = nullptr; // 基础类型或尚未填入基础类型的数组。
        std::vector<Node> nodes; // 声明、参数或运算尾表，逆序保存以便尾部追加。
        bool unary_form = false; // 是否具有一元式形状，供赋值左部检查。
    };
    struct Frame {
        int symbol; // 待匹配的终结符或非终结符。
        int production = -1; // -1 表示匹配符号，否则完成该产生式的 AST 动作。
        std::size_t base = 0; // 产生式第一个右部符号在语义值栈中的位置。
    };
    const std::vector<Token>& tokens; // 词法阶段给出的单词表，末尾为 EOF。
    std::size_t pos = 0; // 下一个输入单词的下标。
    unsigned nesting = 0; // 尚未闭合的括号层数。
    ParseResult result; // 返回的 AST 内存池、根节点和诊断。
    std::vector<Frame> stack; // 分析栈：文法符号和完成动作。
    std::vector<Value> values; // 语义值栈：每个已匹配符号对应一个结果。
    std::unordered_map<Node, unsigned> heights; // 已完成 AST 节点的高度。

    // 在指定单词处记录错误；不指定位置时使用当前输入。
    void report(const std::string& message, const std::string& code = "PARSE_EXPECTED",
                Level level = Level::Error, std::size_t at = std::numeric_limits<std::size_t>::max()) {
        const auto& token = tokens[at == std::numeric_limits<std::size_t>::max() ? pos : at];
        result.diagnostics.push_back({Phase::Parser, level, token.line, token.col, message, code, token.range, {}});
        if (result.diagnostics.size() == 20) result.diagnostics.back().message += "；错误过多，停止本阶段";
    }
    // 报告 AST 约束错误，结束本次完成动作。
    [[noreturn]] void fail(const std::string& message, const std::string& code,
                          Level level = Level::Error, std::size_t at = std::numeric_limits<std::size_t>::max()) {
        report(message, code, level, at); throw ParseFailure{};
    }
    // 判断是否达到错误上限或出现致命错误。
    bool stopped() const {
        return result.diagnostics.size() >= 20 ||
            (!result.diagnostics.empty() && result.diagnostics.back().level == Level::Fatal);
    }
    // 创建 AST 节点，填写种类、源码范围、名字和孩子。
    Node node(NodeType kind, std::size_t begin, std::size_t end,
              const std::string& name = "", std::vector<Node> children = {}) {
        auto n = result.arena.make();
        n->kind = kind; n->name = name; n->children = std::move(children);
        n->range = tokens[begin].range;
        if (end) n->range.end = tokens[end - 1].range.end;
        heights[n] = 1;
        return n;
    }
    // 用孩子计算树高；超过 128 层时报告致命错误。
    void finish(Node n) {
        unsigned height = 1;
        for (auto c : n->children) height = std::max(height, 1 + heights.at(c));
        if (height > 128) fail("语法树嵌套超过 128 层", "PARSE_DEPTH", Level::Fatal);
        heights[n] = height;
    }
    // 把前项接到尾表前面；移动尾表并追加，避免反复复制长列表。
    Value sequence(Value& head, Value& tail) {
        Value out = std::move(tail);
        if (head.ast) out.nodes.push_back(head.ast);
        else out.nodes.insert(out.nodes.end(), head.nodes.begin(), head.nodes.end());
        return out;
    }
    // 根据已经匹配的类型单词返回基础类型。
    TypePtr specs(std::size_t begin) {
        auto t = make_type_info();
        switch (tokens[begin].type) {
        case TT::KW_VOID: t->kind = TypeKind::Void; break;
        case TT::KW_CHAR: t->kind = TypeKind::Char; break;
        case TT::KW_INT: t->kind = TypeKind::Int; break;
        case TT::KW_FLOAT: t->kind = TypeKind::Float; break;
        default: t->kind = TypeKind::Struct; t->name = tokens[begin + 1].lexeme; break;
        }
        return t;
    }
    // 根据已匹配的数组区间返回数组类型；空区间返回空指针。
    TypePtr declarator(std::size_t begin, std::size_t end) {
        if (begin == end) return nullptr;
        auto t = make_type_info(); t->kind = TypeKind::Array;
        const auto& size = tokens[begin + 1];
        if (size.type != TT::RBRACKET) {
            std::size_t used = 0;
            unsigned long long length = 0;
            try { length = std::stoull(size.lexeme, &used, 0); }
            catch (const std::exception&) { fail("数组长度超出范围", "PARSE_ARRAY_BOUND", Level::Error, begin + 1); }
            if (used != size.lexeme.size()) fail("数组长度不能带 U 和 L 后缀", "PARSE_ARRAY_BOUND", Level::Error, begin + 1);
            if (!length || length > std::numeric_limits<std::size_t>::max())
                fail("数组长度必须为可表示的正整数", "PARSE_ARRAY_BOUND", Level::Error, begin + 1);
            t->array_length = static_cast<std::size_t>(length);
        }
        return t;
    }
    // 为一个变量、成员或形参补全类型，不修改已有类型对象。
    void bind_type(Node n, TypePtr base) {
        if (!n->declared_type) n->declared_type = base;
        else { auto t = make_type_info(*n->declared_type); t->base = base; n->declared_type = t; }
    }
    // 建立暂缺名字和基础类型的对象，交给所属声明补全。
    Node object(NodeType kind, Value& array, Node init = nullptr, std::size_t end = 0) {
        auto n = node(kind, array.begin, end ? end : array.end);
        n->declared_type = array.type;
        if (init) n->children.push_back(init);
        finish(n); return n;
    }
    // 完成声明：为对象补名字、基础类型和声明起点，或完成函数与结构体。
    void declaration(Value& out, Value* v, int symbol) {
        const auto begin = v[0].begin;
        if (symbol == 成员声明) {
            out = std::move(v[3]);
            out.nodes.push_back(object(NodeType::MemberDecl, v[2]));
            out.nodes.back()->name = tokens[v[1].begin].lexeme;
            out.nodes.front()->range.end = tokens[v[4].begin].range.end;
        } else out = std::move(v[2]);
        auto base = v[0].type ? v[0].type : specs(begin);
        if (out.ast && out.ast->kind == NodeType::FunctionDef) {
            auto fn = out.ast;
            fn->name = tokens[v[1].begin].lexeme; fn->range.begin = tokens[begin].range.begin;
            auto type = make_type_info(); type->kind = TypeKind::Function; type->base = base;
            for (std::size_t i = 0; i + 1 < fn->children.size(); ++i) type->params.push_back(fn->children[i]->declared_type);
            fn->declared_type = type; out.nodes.push_back(fn); out.ast = nullptr;
            return;
        }
        if (!out.nodes.empty() && out.nodes.back()->name.empty()) out.nodes.back()->name = tokens[v[1].begin].lexeme;
        for (auto n : out.nodes) { n->range.begin = tokens[begin].range.begin; bind_type(n, base); }
        if (out.ast) {
            out.ast->name = tokens[v[1].begin].lexeme; out.ast->range.begin = tokens[begin].range.begin;
            out.nodes.push_back(out.ast); out.ast = nullptr;
        }
    }
    // 把逆序的局部声明和语句列表一次性装入块节点。
    void block(Value& out, Value* v) {
        std::vector<Node> children(v[1].nodes.rbegin(), v[1].nodes.rend());
        children.insert(children.end(), v[2].nodes.rbegin(), v[2].nodes.rend());
        out.ast = node(NodeType::Block, v[0].begin, v[3].end, "", std::move(children));
    }
    // 按语句产生式建立控制流节点，保持公共 AST 孩子顺序。
    void statement(Value& out, Value* v, int count) {
        const auto token = tokens[v[0].begin].type;
        if (token == TT::LBRACE) { out.ast = v[0].ast; return; }
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
        out.ast = node(kind, v[0].begin, v[count - 1].end);
        auto& children = out.ast->children;
        if (kind == NodeType::If || kind == NodeType::While) {
            children = {v[2].ast, v[4].ast};
            if (kind == NodeType::If && v[5].ast) children.push_back(v[5].ast);
        } else if (kind == NodeType::For) {
            for (int i : {2, 4, 6}) children.push_back(v[i].ast ? v[i].ast : node(NodeType::Empty, v[i].begin, v[i].begin + 1));
            children.push_back(v[8].ast);
        } else if (kind == NodeType::Return) { if (v[1].ast) children.push_back(v[1].ast); }
        else if (kind == NodeType::ExprStmt) children.push_back(v[0].ast);
    }
    // 从左向右接上运算尾表；赋值右部已是完整表达式，所以自然右结合。
    void expression(Value& out, Value* v) {
        out.ast = v[0].ast; out.unary_form = v[0].unary_form;
        for (auto i = v[1].nodes.rbegin(); i != v[1].nodes.rend(); ++i) {
            auto op = *i;
            if (op->kind == NodeType::Assign && !out.unary_form)
                fail("赋值左部必须符合一元表达式形状", "PARSE_ASSIGNMENT_TARGET", Level::Error, v[1].begin);
            op->range.begin = out.ast->range.begin; op->children.insert(op->children.begin(), out.ast);
            finish(op); out.ast = op; out.unary_form = false;
        }
    }
    // 完成一元式、主式或字符串；括号主式保留一元式形状。
    void unary(Value& out, Value* v, int symbol, int count) {
        const auto begin = v[0].begin, end = v[count - 1].end;
        const auto& token = tokens[begin]; out.unary_form = true;
        if (symbol == 一元式) {
            out.ast = count == 1 ? v[0].ast : node(NodeType::UnaryOp, begin, end, token.lexeme, {v[1].ast});
        } else if (symbol == 字符串) {
            std::string text = token.lexeme;
            for (auto i = begin + 1; i < end; ++i) text += " " + tokens[i].lexeme;
            out.ast = node(NodeType::StringLiteral, begin, end, text);
        } else if (token.type == TT::LPAREN) {
            out.ast = v[1].ast; out.ast->range.begin = token.range.begin; out.ast->range.end = tokens[end - 1].range.end;
        } else if (v[0].ast) out.ast = v[0].ast;
        else {
            NodeType kind = NodeType::Identifier;
            if (token.type == TT::INT_LITERAL) {
                if (token.lexeme.find_first_of("uUlL") != std::string::npos)
                    fail("整数常量不接受后缀", "PARSE_LITERAL", Level::Error, begin);
                kind = NodeType::IntLiteral;
            } else if (token.type == TT::FLOAT_LITERAL) {
                if (token.lexeme.find_last_of("lL") == token.lexeme.size() - 1)
                    fail("浮点常量不接受 L 后缀", "PARSE_LITERAL", Level::Error, begin);
                kind = NodeType::FloatLiteral;
            } else if (token.type == TT::CHAR_LITERAL) kind = NodeType::CharLiteral;
            out.ast = node(kind, begin, end, token.lexeme);
        }
    }
    // 将调用、下标或成员后缀依次接到主式上。
    void postfix(Value& out, Value* v) {
        out.ast = v[0].ast; out.unary_form = true;
        for (auto i = v[1].nodes.rbegin(); i != v[1].nodes.rend(); ++i) {
            auto suffix = *i;
            if (suffix->kind == NodeType::Call && out.ast->kind == NodeType::Identifier) suffix->name = out.ast->name;
            suffix->range.begin = out.ast->range.begin; suffix->children.insert(suffix->children.begin(), out.ast);
            finish(suffix); out.ast = suffix;
        }
    }
    // 消费产生式右部的语义值，返回一个结果；不保存文法孩子或匹配树。
    Value reduce(int symbol, Value* v, int count) {
        Value out;
        if (!count) return out;
        switch (symbol) {
        case 程序: case 成员序列: case 局部声明序列: case 语句序列: case 表达后缀序列:
        case 形参表可选: case 实参可选:
            if (count == 2) out = sequence(v[0], v[1]);
            break;
        case 形参续: case 实参续: out = sequence(v[1], v[2]); break;
        case 标量类型: case 成员类型: out.type = v[0].type ? v[0].type : specs(v[0].begin); break;
        case 数组可选: out.type = declarator(v[0].begin, v[2].end); break;
        case 对象续: case 成员续: {
            const bool member = symbol == 成员续;
            out = std::move(v[member ? 3 : 4]);
            auto n = object(member ? NodeType::MemberDecl : NodeType::VarDecl, v[2], member ? nullptr : v[3].ast,
                            member ? v[2].end : v[3].end);
            n->name = tokens[v[1].begin].lexeme; out.nodes.push_back(n); break;
        }
        case 对象尾:
            out = std::move(v[2]); out.nodes.push_back(object(NodeType::VarDecl, v[0], v[1].ast, v[1].end));
            out.nodes.front()->range.end = tokens[v[3].begin].range.end; break;
        case 定义后:
            if (count == 2) { out = std::move(v[1]); out.nodes.back()->name = tokens[v[0].begin].lexeme; }
            break;
        case 结构声明尾:
            if (count == 2) { out = std::move(v[1]); out.nodes.back()->name = tokens[v[0].begin].lexeme; }
            else {
                auto members = sequence(v[1], v[2]); out = std::move(v[4]);
                out.ast = node(NodeType::StructDef, v[0].begin, out.nodes.empty() ? v[4].end : v[3].end,
                               "", {members.nodes.rbegin(), members.nodes.rend()});
            }
            break;
        case 函数尾:
            out.ast = node(NodeType::FunctionDef, v[0].begin, v[3].end, "", {v[1].nodes.rbegin(), v[1].nodes.rend()});
            out.ast->children.push_back(v[3].ast); break;
        case 外部声明: case 局部声明: case 成员声明: declaration(out, v, symbol); break;
        case 形参:
            out.ast = node(NodeType::ParamDecl, v[0].begin, v[count - 1].end, tokens[v[count == 3 ? 1 : 2].begin].lexeme);
            out.ast->declared_type = count == 3 ? v[2].type : declarator(v[3].begin, v[5].end);
            bind_type(out.ast, v[0].type ? v[0].type : specs(v[0].begin)); break;
        case 块: block(out, v); break;
        case 语句: statement(out, v, count); break;
        case 初值可选: case 否则可选: out.ast = v[1].ast; break;
        case 表达式: case 逻辑或式: case 逻辑与式: case 相等式: case 关系式: case 加法式: case 乘法式:
            expression(out, v); break;
        case 赋值续: case 逻辑或续: case 逻辑与续: case 相等续: case 关系续: case 加法续: case 乘法续:
            if (count == 3) out = std::move(v[2]);
            out.nodes.push_back(node(symbol == 赋值续 ? NodeType::Assign : NodeType::BinaryOp,
                                     v[0].begin, v[1].end, tokens[v[0].begin].lexeme, {v[1].ast})); break;
        case 一元式: case 主式: case 字符串: unary(out, v, symbol, count); break;
        case 后缀式: postfix(out, v); break;
        case 表达后缀:
            if (count == 2) out.ast = node(NodeType::MemberAccess, v[0].begin, v[1].end, tokens[v[1].begin].lexeme);
            else if (tokens[v[0].begin].type == TT::LPAREN)
                out.ast = node(NodeType::Call, v[0].begin, v[2].end, "", {v[1].nodes.rbegin(), v[1].nodes.rend()});
            else out.ast = node(NodeType::ArrayAccess, v[0].begin, v[2].end, "", {v[1].ast});
            break;
        default: if (count == 1) out = std::move(v[0]); break;
        }
        if (out.ast) finish(out.ast);
        return out;
    }
    // 按预测项或 FOLLOW 同步；终结符失配时补缺或跳过输入。
    void recover(int symbol) {
        report(std::string("需要 ") + names[symbol] + "，实际遇到 " +
               (tokens[pos].type == TT::END_OF_FILE ? "文件结束" : tokens[pos].lexeme));
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
            if (t == TT::END_OF_FILE || t == TT::SEMI || t == TT::RBRACE || t == TT::RPAREN || t == TT::RBRACKET) stack.pop_back();
            else ++pos;
        }
    }

public:
    // 保存词法单词表引用，解析期间不改变输入。
    explicit Parser(const std::vector<Token>& input) : tokens(input) {}
    // 匹配终结符、查表展开、完成 AST 动作，返回整棵树或错误诊断。
    ParseResult run() {
        stack = {{static_cast<int>(TT::END_OF_FILE)}, {程序}};
        while (!stack.empty() && !stopped()) {
            const auto frame = stack.back();
            if (frame.production >= 0) {
                stack.pop_back();
                if (result.diagnostics.empty()) {
                    const int count = productions[frame.production].length;
                    const auto begin = count ? values[frame.base].begin : pos;
                    try {
                        auto value = reduce(frame.symbol, count ? &values[frame.base] : nullptr, count);
                        value.begin = begin; value.end = pos;
                        values.resize(frame.base); values.push_back(std::move(value));
                    } catch (const ParseFailure&) {}
                }
            } else if (frame.symbol < terminal_count) {
                if (frame.symbol != static_cast<int>(tokens[pos].type)) { recover(frame.symbol); continue; }
                stack.pop_back();
                if (result.diagnostics.empty()) {
                    Value value; value.begin = pos; value.end = pos + (frame.symbol != 0);
                    values.push_back(std::move(value));
                }
                if (frame.symbol == 0) continue;
                const auto t = tokens[pos++].type;
                if (t == TT::LPAREN || t == TT::LBRACE || t == TT::LBRACKET) {
                    if (++nesting > 128) report("括号嵌套超过 128 层", "PARSE_DEPTH", Level::Fatal);
                } else if (nesting && (t == TT::RPAREN || t == TT::RBRACE || t == TT::RBRACKET)) --nesting;
            } else {
                const int choice = table[frame.symbol - terminal_count][static_cast<int>(tokens[pos].type)];
                if (choice < 0) { recover(frame.symbol); continue; }
                stack.pop_back(); stack.push_back({frame.symbol, choice, values.size()});
                const auto& production = productions[choice];
                for (int i = production.length - 1; i >= 0; --i) stack.push_back({production.rhs[i]});
            }
        }
        if (result.diagnostics.empty()) {
            auto root = node(NodeType::Program, 0, pos, "", {values[0].nodes.rbegin(), values[0].nodes.rend()});
            root->range.end = tokens[pos].range.end;
            try { finish(root); result.root = root; } catch (const ParseFailure&) {}
        }
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
