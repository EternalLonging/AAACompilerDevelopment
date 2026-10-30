#include "minic/parser.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace minic {
namespace {
using TT = TokenType;
using Node = ASTNode*;
struct ParseFailure {};

// 根据单词表建立语法树；名字绑定由语义阶段处理。
class Parser {
    const std::vector<Token>& tokens; // 词法阶段生成的单词表，最后一个单词是 EOF。
    std::size_t pos = 0; // 下一个要读取的单词下标。
    unsigned depth = 0; // 当前解析函数的嵌套层数，用来限制递归深度。
    ParseResult result; // 最终的语法树和本阶段诊断。
    std::unordered_map<const ASTNode*, unsigned> heights; // 各节点的树高，用来限制语法树深度。

    // 查看后面的单词，不移动读取位置；超过末尾时返回 EOF。
    const Token& peek(std::size_t ahead = 0) const {
        return tokens[std::min(pos + ahead, tokens.size() - 1)];
    }
    // 判断当前单词是否属于指定种别。
    bool at(TT type) const { return peek().type == type; }
    // 取出当前单词并向后移动；EOF 不再向后移动。
    Token take() { const auto token = peek(); if (!at(TT::END_OF_FILE)) ++pos; return token; }
    // 当前种别匹配时读取并返回 true，否则不读取并返回 false。
    bool accept(TT type) { if (!at(type)) return false; take(); return true; }
    // 记录语法错误，并结束当前解析分支，交给外层恢复处理。
    [[noreturn]] void fail(const std::string& message, const std::string& code = "PARSE_EXPECTED",
                           Level level = Level::Error) {
        const auto& token = peek();
        result.diagnostics.push_back({Phase::Parser, level, token.line, token.col,
                                       message, code, token.range, {}});
        if (result.diagnostics.size() == 20)
            result.diagnostics.back().message += "；错误过多，停止本阶段";
        throw ParseFailure{};
    }
    // 读取必须出现的单词；种别不符时报告错误。
    Token expect(TT type, const std::string& spelling) {
        if (!at(type)) fail("需要 " + spelling + "，实际遇到 " +
                            (at(TT::END_OF_FILE) ? "文件结束" : peek().lexeme));
        return take();
    }
    // 错误达到上限或出现致命错误时，停止继续解析。
    bool stopped() const {
        return result.diagnostics.size() >= 20 ||
            (!result.diagnostics.empty() && result.diagnostics.back().level == Level::Fatal);
    }
    // 在进入和离开解析函数时维护嵌套层数。
    struct Nest {
        Parser& parser; // 当前解析器；离开本层时自动减少嵌套计数。
        // 进入一层解析；达到上限时报告致命错误。
        explicit Nest(Parser& p) : parser(p) {
            if (p.depth >= 128) p.fail("语法嵌套超过 128 层", "PARSE_DEPTH", Level::Fatal);
            ++p.depth;
        }
        // 离开这一层，恢复嵌套计数。
        ~Nest() { --parser.depth; }
    };
    // 判断当前单词是否可以作为声明的类型。
    bool specifier() const {
        switch (peek().type) {
        case TT::KW_VOID: case TT::KW_CHAR: case TT::KW_INT:
        case TT::KW_FLOAT: case TT::KW_STRUCT:
            return true;
        default: return false;
        }
    }
    // 创建 AST 节点，填写种类、名字和开始位置。
    Node node(NodeType kind, const Token& start, const std::string& name = "") {
        auto n = result.arena.make();
        n->kind = kind; n->name = name; n->range = start.range;
        heights[n] = 1;
        return n;
    }
    // 补齐节点的结束位置和树高；树高超限时报告错误。
    void finish(ASTNode& n) {
        if (pos) n.range.end = tokens[pos - 1].range.end;
        unsigned height = 1;
        for (const auto& child : n.children) {
            const auto found = heights.find(child);
            height = std::max(height, 1 + (found == heights.end() ? 1 : found->second));
        }
        if (height > 128) fail("语法树嵌套超过 128 层", "PARSE_DEPTH", Level::Fatal);
        heights[&n] = height;
    }
    // 声明开头的类型和 const 限定符信息。
    struct Specs {
        TypePtr type = nullptr; // 声明中的基础类型，例如 int 或 struct S。
        std::vector<Node> definitions; // 声明中同时定义的结构体节点。
    };

    // 读取类型说明，返回声明的基础信息。
    Specs specs() {
        Nest nest(*this);
        Specs s;
        auto type = make_type_info();
        const auto token = peek();
        if (!specifier()) fail("声明需要明确的类型", "PARSE_SPECIFIERS");
        take();
        switch (token.type) {
        case TT::KW_VOID: type->kind = TypeKind::Void; break;
        case TT::KW_CHAR: type->kind = TypeKind::Char; break;
        case TT::KW_INT: type->kind = TypeKind::Int; break;
        case TT::KW_FLOAT: type->kind = TypeKind::Float; break;
        case TT::KW_STRUCT: {
            type->kind = TypeKind::Struct;
            if (at(TT::ID)) type->name = take().lexeme;
            if (accept(TT::LBRACE)) {
                if (type->name.empty())
                    fail("struct 定义必须有标签名", "PARSE_RECORD_ANONYMOUS");
                auto def = node(NodeType::StructDef, token, type->name);
                while (!at(TT::RBRACE) && !at(TT::END_OF_FILE)) {
                    auto members = declaration(false, true);
                    for (auto* member : members) def->children.push_back(member);
                }
                if (def->children.empty()) fail("结构体定义不能为空", "PARSE_RECORD");
                expect(TT::RBRACE, "'}'"); finish(*def);
                s.definitions.push_back(def);
            } else if (type->name.empty()) {
                fail("struct 类型需要标签名和定义", "PARSE_RECORD");
            }
            break;
        }
        default: fail("需要类型说明符", "PARSE_SPECIFIERS");
        }
        s.type = type;
        return s;
    }

    // 声明符中的一层数组或函数。
    struct Layer {
        TypeKind kind = TypeKind::Array; // 这一层是数组还是函数。
        MaybeValue<std::size_t> length; // 数组元素个数；省略界限时为空。
        std::vector<Node> params; // 本层函数的形参节点，按声明顺序保存。
    };
    // 声明符信息，例如 a[3] 或 fn(int)。
    struct Declarator {
        std::string name; // 声明的名字；无名形参或类型名中可以为空。
        std::vector<Layer> layers; // 从名字向外保存数组、函数。
        SourceRange range; // 声明符从开始到结束的源码范围。
    };
    // 读取名字及其数组、函数层次；abstract 为 true 时允许没有名字。
    Declarator declarator(bool abstract = false) {
        Nest nest(*this);
        Declarator d; d.range = peek().range;
        if (at(TT::ID)) d.name = take().lexeme;
        else if (!abstract) fail("声明符需要名字", "PARSE_DECLARATOR");
        while (at(TT::LBRACKET) || at(TT::LPAREN)) {
            if (d.layers.size() >= 128) fail("声明符嵌套过深", "PARSE_DEPTH", Level::Fatal);
            Layer layer;
            if (accept(TT::LBRACKET)) {
                layer.kind = TypeKind::Array;
                for (const auto& previous : d.layers)
                    if (previous.kind == TypeKind::Array)
                        fail("MiniC 只支持一维数组", "PARSE_ARRAY_DIMENSION");
                if (!at(TT::RBRACKET)) {
                    const auto size = expect(TT::INT_LITERAL, "正整数数组长度（暂不支持常量表达式界限）");
                    std::size_t used = 0;
                    unsigned long long value = 0;
                    try { value = std::stoull(size.lexeme, &used, 0); }
                    catch (const std::exception&) { fail("数组长度超出范围", "PARSE_ARRAY_BOUND"); }
                    if (used != size.lexeme.size())
                        fail("数组长度不能带 U 和 L 后缀", "PARSE_ARRAY_BOUND");
                    if (!value || value > std::numeric_limits<std::size_t>::max())
                        fail("数组长度必须为可表示的正整数", "PARSE_ARRAY_BOUND");
                    layer.length = static_cast<std::size_t>(value);
                }
                expect(TT::RBRACKET, "']'");
            } else {
                take(); layer.kind = TypeKind::Function;
                {
                    if (at(TT::KW_VOID) && peek(1).type == TT::RPAREN) take();
                    else if (!at(TT::RPAREN)) {
                        do {
                            const auto start = peek();
                            auto ps = specs();
                            if (!ps.definitions.empty()) fail("请先单独声明形参使用的记录类型", "PARSE_PARAMETER");
                            auto pd = declarator(true);
                            auto param = node(NodeType::ParamDecl, start, pd.name);
                            param->declared_type = build_type(ps.type, pd);
                            if (param->declared_type->kind == TypeKind::Struct)
                                fail("struct 不能作为函数参数", "PARSE_FUNCTION");
                            if (param->declared_type->kind == TypeKind::Void)
                                fail("void 只能单独表示空形参列表", "PARSE_PARAMETER");
                            finish(*param);
                            layer.params.push_back(std::move(param));
                        } while (accept(TT::COMMA));
                    }
                    expect(TT::RPAREN, "')'");
                }
            }
            d.layers.push_back(std::move(layer));
        }
        if (pos) d.range.end = tokens[pos - 1].range.end;
        return d;
    }
    // 按声明符的层次组合完整类型，不改变基础类型。
    TypePtr build_type(TypePtr base, const Declarator& d) {
        for (auto i = d.layers.rbegin(); i != d.layers.rend(); ++i) {
            auto type = make_type_info(); type->kind = i->kind; type->base = base;

            type->array_length = i->length;
            for (const auto& param : i->params) type->params.push_back(param->declared_type);
            if (i->kind == TypeKind::Function &&
                (base->kind == TypeKind::Array || base->kind == TypeKind::Function))
                fail("函数不能返回数组或函数", "PARSE_DECLARATOR");
            if (i->kind == TypeKind::Array &&
                (base->kind == TypeKind::Function || base->kind == TypeKind::Void))
                fail("数组元素不能是函数或 void", "PARSE_DECLARATOR");
            base = type;
        }
        return base;
    }
    // 读取一条声明，返回其中的类型定义、变量或函数节点。
    std::vector<Node> declaration(bool external, bool member = false) {
        Nest nest(*this);
        const auto start = peek();
        auto s = specs();
        if (member && !s.definitions.empty())
            fail("成员类型的内联定义暂不支持，请先单独定义记录类型", "PARSE_MEMBER");
        std::vector<Node> nodes = std::move(s.definitions);
        if (accept(TT::SEMI)) {
            if (member)
                fail("此声明需要声明符", "PARSE_DECLARATOR");
            if (nodes.empty() && s.type->kind == TypeKind::Struct)
                fail("请先写出完整 struct 定义，不支持前向声明", "PARSE_RECORD_FORWARD");
            if (nodes.empty()) fail("声明缺少声明符", "PARSE_DECLARATOR");
            for (auto& n : nodes) finish(*n);
            return nodes;
        }
        bool first_declarator = true;
        do {
            auto d = declarator();
            const auto type = build_type(s.type, d);
            const bool function = type->kind == TypeKind::Function;
            if (member && function)
                fail("成员不支持函数或位域", "PARSE_MEMBER");
            auto n = node(member ? NodeType::MemberDecl : function ? NodeType::FunctionDef : NodeType::VarDecl, start, d.name);
            n->declared_type = type;
            if (function) {
                if (type->base->kind == TypeKind::Struct)
                    fail("struct 不能作为函数返回值", "PARSE_FUNCTION");
                n->children = std::move(d.layers.front().params);
                if (at(TT::LBRACE)) {
                    if (!external) fail("函数定义只能出现在顶层", "PARSE_FUNCTION");
                    if (!first_declarator) fail("函数定义不能与其他声明符共用声明", "PARSE_FUNCTION");
                    for (const auto& param : n->children)
                        if (param->name.empty()) fail("函数定义中的形参需要名字", "PARSE_PARAMETER");
                    n->kind = NodeType::FunctionDef;
                    n->children.push_back(block());
                    finish(*n); nodes.push_back(std::move(n));
                    return nodes;
                }
                fail("函数必须带函数体", "PARSE_FUNCTION");
            }
            if (accept(TT::ASSIGN)) {
                if (member || function)
                    fail("此声明不允许初始化", "PARSE_INITIALIZER");
                n->children.push_back(initializer());
            }
            finish(*n); nodes.push_back(std::move(n));
            first_declarator = false;
        } while (accept(TT::COMMA));
        expect(TT::SEMI, "';'");
        if (!nodes.empty()) finish(*nodes.back());
        return nodes;
    }
    // 读取一个赋值式初始化。
    Node initializer() {
        Nest nest(*this);
        return expression(2);
    }
    // 恢复到当前块的下一个分号；跳过完整的内嵌花括号，保证每次推进。
    void recover(std::size_t before, bool external) {
        unsigned braces = 0;
        while (!at(TT::END_OF_FILE)) {
            if (at(TT::RBRACE) && braces == 0) {
                if (external) take();
                return;
            }
            if (at(TT::LBRACE)) ++braces;
            else if (at(TT::RBRACE)) --braces;
            if (take().type == TT::SEMI && braces == 0) return;
            if (external && pos > before && braces == 0 && specifier()) return;
        }
    }
    // 读取花括号块，要求本块声明写在语句之前。
    Node block() {
        Nest nest(*this);
        const auto start = expect(TT::LBRACE, "'{'（控制体必须使用花括号）");
        auto n = node(NodeType::Block, start);
        bool saw_statement = false;
        while (!at(TT::RBRACE) && !at(TT::END_OF_FILE) && !stopped()) {
            const auto before = pos;
            try {
                if (specifier()) {
                    if (saw_statement)
                        fail("同一块中的声明必须放在语句之前", "PARSE_DECLARATION_ORDER");
                    auto ds = declaration(false);
                    for (auto& d : ds) n->children.push_back(std::move(d));
                } else {
                    saw_statement = true;
                    n->children.push_back(statement());
                }
            } catch (const ParseFailure&) {
                if (stopped()) throw;
                recover(before, false);
            }
        }
        expect(TT::RBRACE, "'}'");
        finish(*n); return n;
    }
    // 读取括号中的条件表达式。
    Node condition() {
        expect(TT::LPAREN, "'('"); auto n = expression(); expect(TT::RPAREN, "')'"); return n;
    }
    // 读取一条语句，返回控制流、表达式或块节点。
    Node statement() {
        Nest nest(*this);
        const auto start = peek();
        if (at(TT::LBRACE)) return block();
        if (accept(TT::SEMI)) return node(NodeType::Empty, start);
        if (accept(TT::KW_IF)) {
            auto n = node(NodeType::If, start); n->children.push_back(condition());
            n->children.push_back(block());
            if (accept(TT::KW_ELSE)) n->children.push_back(block());
            finish(*n); return n;
        }
        if (accept(TT::KW_WHILE)) {
            auto n = node(NodeType::While, start);
            n->children.push_back(condition()); n->children.push_back(block()); finish(*n); return n;
        }
        if (accept(TT::KW_FOR)) {
            auto n = node(NodeType::For, start); expect(TT::LPAREN, "'('");
            if (specifier()) fail("for 初始化只支持表达式，请在循环前声明变量", "PARSE_FOR_DECLARATION");
            n->children.push_back(at(TT::SEMI) ? node(NodeType::Empty, peek()) : expression());
            expect(TT::SEMI, "';'");
            n->children.push_back(at(TT::SEMI) ? node(NodeType::Empty, peek()) : expression());
            expect(TT::SEMI, "';'");
            n->children.push_back(at(TT::RPAREN) ? node(NodeType::Empty, peek()) : expression());
            expect(TT::RPAREN, "')'"); n->children.push_back(block()); finish(*n); return n;
        }
        if (accept(TT::KW_RETURN)) {
            auto n = node(NodeType::Return, start);
            if (!at(TT::SEMI)) n->children.push_back(expression());
            expect(TT::SEMI, "';'"); finish(*n); return n;
        }
        if (accept(TT::KW_BREAK) || accept(TT::KW_CONTINUE)) {
            auto n = node(start.type == TT::KW_BREAK ? NodeType::Break :
                NodeType::Continue, start);
            expect(TT::SEMI, "';'"); finish(*n); return n;
        }
        auto n = node(NodeType::ExprStmt, start); n->children.push_back(expression());
        expect(TT::SEMI, "';'"); finish(*n); return n;
    }

    // 返回二元运算符优先级；不是二元运算符时返回 0。
    static int precedence(TT type) {
        switch (type) {
        case TT::ASSIGN: return 2;
        case TT::OR: return 3;
        case TT::AND: return 4;
        case TT::EQ: case TT::NE: return 5;
        case TT::LT: case TT::LE: case TT::GT: case TT::GE: return 6;
        case TT::PLUS: case TT::MINUS: return 7;
        case TT::STAR: case TT::SLASH: case TT::PERCENT: return 8;
        default: return 0;
        }
    }
    // 判断节点是否符合赋值左部的一元表达式语法；可否赋值由语义阶段检查。
    static bool unary_shape(const ASTNode& n) {
        switch (n.kind) {
        case NodeType::Identifier: case NodeType::IntLiteral: case NodeType::FloatLiteral:
        case NodeType::CharLiteral: case NodeType::StringLiteral: case NodeType::UnaryOp:
        case NodeType::Call: case NodeType::ArrayAccess: case NodeType::MemberAccess:
            return true;
        default: return false;
        }
    }
    // 从最低允许的优先级开始读取表达式，返回表达式树。
    Node expression(int minimum = 1) {
        Nest nest(*this);
        auto left = unary();
        // 括号表达式在语法上属于 PrimaryExpression，保留其赋值左部资格。
        bool shape = unary_shape(*left) || parenthesized;
        while (precedence(peek().type) >= minimum) {
            const auto op = take(); const int priority = precedence(op.type);
            auto n = node(priority == 2 ? NodeType::Assign : NodeType::BinaryOp, op, op.lexeme);
            n->range.begin = left->range.begin; n->range.file = left->range.file;
            if (priority == 2 && !shape) fail("赋值左部必须符合一元表达式形状", "PARSE_ASSIGNMENT_TARGET");
            n->children.push_back(std::move(left));
            n->children.push_back(expression(priority == 2 ? priority : priority + 1));
            finish(*n); left = std::move(n); shape = false;
        }
        parenthesized = false;
        return left;
    }
    bool parenthesized = false; // 最近的一元表达式是否来自括号，供赋值左部语法检查使用。
    // 读取前缀运算、括号或字面量，返回表达式节点。
    Node unary() {
        Nest nest(*this);
        const auto start = peek(); parenthesized = false;
        switch (start.type) {
        case TT::PLUS: case TT::MINUS: case TT::NOT: case TT::AMP: {
            take(); auto n = node(NodeType::UnaryOp, start,
                start.lexeme);
            n->children.push_back(unary()); finish(*n); parenthesized = false; return n;
        }
        default: break;
        }
        if (at(TT::LPAREN)) {
            take();
            auto n = expression(); expect(TT::RPAREN, "')'");
            n->range.begin = start.range.begin; finish(*n);
            n = postfix(std::move(n)); parenthesized = true; return n;
        }
        Node n;
        switch (start.type) {
        case TT::ID:
            n = node(NodeType::Identifier, take(), start.lexeme); break;
        case TT::INT_LITERAL:
            if (start.lexeme.find_first_of("uUlL") != std::string::npos)
                fail("整数常量不接受后缀", "PARSE_LITERAL");
            n = node(NodeType::IntLiteral, take(), start.lexeme); break;
        case TT::FLOAT_LITERAL:
            if (start.lexeme.back() == 'l' || start.lexeme.back() == 'L')
                fail("浮点常量不接受 L 后缀", "PARSE_LITERAL");
            n = node(NodeType::FloatLiteral, take(), start.lexeme); break;
        case TT::CHAR_LITERAL: n = node(NodeType::CharLiteral, take(), start.lexeme); break;
        case TT::STRING_LITERAL: {
            n = node(NodeType::StringLiteral, take(), start.lexeme);
            // 保留每段引号和转义；语义解码器按多段字符串处理。
            while (at(TT::STRING_LITERAL)) { n->name += " " + take().lexeme; finish(*n); }
            break;
        }
        default: fail("需要表达式，实际遇到 " +
                      (at(TT::END_OF_FILE) ? "文件结束" : start.lexeme), "PARSE_EXPRESSION");
        }
        n = postfix(std::move(n)); parenthesized = false; return n;
    }
    // 在已有表达式后读取调用、下标或成员访问，返回最终节点。
    Node postfix(Node base) {
        while (at(TT::LPAREN) || at(TT::LBRACKET) || at(TT::DOT)) {
            const auto op = take(); Node n;
            if (op.type == TT::LPAREN) {
                n = node(NodeType::Call, op, base->kind == NodeType::Identifier ? base->name : "");
                n->children.push_back(std::move(base));
                if (!at(TT::RPAREN)) do { n->children.push_back(expression(2)); } while (accept(TT::COMMA));
                expect(TT::RPAREN, "')'");
            } else if (op.type == TT::LBRACKET) {
                n = node(NodeType::ArrayAccess, op); n->children.push_back(std::move(base));
                n->children.push_back(expression()); expect(TT::RBRACKET, "']'");
            } else {
                const auto member = expect(TT::ID, "成员名字");
                n = node(NodeType::MemberAccess, op, member.lexeme);
                n->children.push_back(base);
            }
            n->range.begin = n->children.front()->range.begin;
            n->range.file = n->children.front()->range.file;
            finish(*n); base = std::move(n);
        }
        return base;
    }

public:
    // 使用词法单词表建立解析器，开始位置为第一个单词。
    explicit Parser(const std::vector<Token>& input) : tokens(input) {}
    // 解析整个文件；任何语法错误都会使结果中的根节点为空。
    ParseResult run() {
        auto root = node(NodeType::Program, peek());
        while (!at(TT::END_OF_FILE) && !stopped()) {
            const auto before = pos;
            try {
                auto declarations = declaration(true);
                for (auto& d : declarations) root->children.push_back(std::move(d));
            } catch (const ParseFailure&) {
                if (!stopped()) recover(before, true);
            }
        }
        root->range.end = peek().range.end;
        if (!has_errors(result.diagnostics)) result.root = std::move(root);
        return std::move(result);
    }
};
}

ParseResult parse(const std::vector<Token>& tokens) {
    if (tokens.empty() || tokens.back().type != TT::END_OF_FILE ||
        std::any_of(tokens.begin(), tokens.end() - 1, [](const Token& t) { return t.type == TT::END_OF_FILE; })) {
        ParseResult result;
        const auto range = tokens.empty() ? SourceRange{} : tokens.back().range;
        result.diagnostics.push_back({Phase::Parser, Level::Error, range.begin.line, range.begin.col,
            "单词表必须包含且只包含一个末尾 EOF", "PARSE_TOKEN_STREAM", range, {}});
        return result;
    }
    return Parser(tokens).run();
}
}

