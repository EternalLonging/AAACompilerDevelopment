#include "minic/parser.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace minic {
namespace {
using TT = TokenType;
using Node = std::unique_ptr<ASTNode>;
struct ParseFailure {};

// 解析环境只用于 typedef 消歧，不写入语义阶段的持久符号表。
class Parser {
    const std::vector<Token>& tokens; // 词法阶段生成的单词表，最后一个单词是 EOF。
    std::size_t pos = 0; // 下一个要读取的单词下标。
    unsigned depth = 0; // 当前解析函数的嵌套层数，用来限制递归深度。
    ParseResult result; // 最终的语法树和本阶段诊断。
    std::vector<std::unordered_map<std::string, bool>> names{{}}; // 每层作用域的名字；true 表示 typedef 别名。
    unsigned anonymous = 0; // 已生成的匿名类型编号，避免内部名字重复。
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
    // 从内层向外查询，判断当前可见名字是否为 typedef 别名。
    bool alias(const std::string& name) const {
        for (auto scope = names.rbegin(); scope != names.rend(); ++scope) {
            const auto found = scope->find(name);
            if (found != scope->end()) return found->second;
        }
        return false;
    }
    // 判断当前单词是否可以作为声明的类型、限定符或存储类别。
    bool specifier() const {
        switch (peek().type) {
        case TT::KW_VOID: case TT::KW_CHAR: case TT::KW_SHORT: case TT::KW_INT:
        case TT::KW_LONG: case TT::KW_FLOAT: case TT::KW_DOUBLE: case TT::KW_SIGNED:
        case TT::KW_UNSIGNED: case TT::KW_CONST: case TT::KW_VOLATILE:
        case TT::KW_STATIC: case TT::KW_EXTERN: case TT::KW_AUTO: case TT::KW_REGISTER:
        case TT::KW_TYPEDEF: case TT::KW_STRUCT: case TT::KW_UNION: case TT::KW_ENUM:
            return true;
        default: return at(TT::ID) && alias(peek().lexeme);
        }
    }
    // 创建 AST 节点，填写种类、名字和开始位置。
    Node node(NodeType kind, const Token& start, const std::string& name = "") {
        auto n = std::make_unique<ASTNode>();
        n->kind = kind; n->name = name; n->range = start.range;
        heights[n.get()] = 1;
        return n;
    }
    // 补齐节点的结束位置和树高；树高超限时报告错误。
    void finish(ASTNode& n) {
        if (pos) n.range.end = tokens[pos - 1].range.end;
        unsigned height = 1;
        for (const auto& child : n.children) {
            const auto found = heights.find(child.get());
            height = std::max(height, 1 + (found == heights.end() ? 1 : found->second));
        }
        if (height > 128) fail("语法树嵌套超过 128 层", "PARSE_DEPTH", Level::Fatal);
        heights[&n] = height;
    }
    // 在原类型外包一层，例如把 int 变成指向 int 的指针类型。
    static TypePtr wrapped(TypeKind kind, TypePtr base) {
        auto type = make_type_info(); type->kind = kind; type->base = std::move(base);
        return type;
    }
    // 声明开头的类型、限定符和存储类别信息。
    struct Specs {
        TypePtr type = nullptr; // 声明中的基础类型，例如 int 或 struct S。
        StorageClass storage = StorageClass::None; // 声明写出的存储类别，例如 static、extern、typedef。
        std::vector<Node> definitions; // 声明中同时定义的结构体、联合体或枚举节点。
    };

    // 读取类型说明、限定符和允许的存储类别，返回声明的基础信息。
    Specs specs(bool allow_storage = true) {
        Nest nest(*this);
        Specs s;
        auto type = make_type_info();
        bool sign = false, unsign = false, short_type = false, long_type = false, int_type = false;
        bool explicit_type = false, saw = false;
        while (specifier()) {
            const auto token = peek();
            if (token.type == TT::ID && explicit_type) break;
            saw = true;
            switch (token.type) {
            case TT::KW_CONST: take(); type->is_const = true; continue;
            case TT::KW_VOLATILE: take(); type->is_volatile = true; continue;
            case TT::KW_STATIC: case TT::KW_EXTERN: case TT::KW_AUTO: case TT::KW_REGISTER:
            case TT::KW_TYPEDEF:
                if (!allow_storage || s.storage != StorageClass::None)
                    fail("此处不允许存储类别或重复的存储类别", "PARSE_SPECIFIERS");
                s.storage = token.type == TT::KW_STATIC ? StorageClass::Static :
                    token.type == TT::KW_EXTERN ? StorageClass::Extern :
                    token.type == TT::KW_AUTO ? StorageClass::Auto :
                    token.type == TT::KW_REGISTER ? StorageClass::Register : StorageClass::Typedef;
                take(); continue;
            case TT::KW_SIGNED: case TT::KW_UNSIGNED:
                if (sign || unsign) fail("重复或冲突的 signed/unsigned", "PARSE_SPECIFIERS");
                sign = token.type == TT::KW_SIGNED; unsign = !sign; take(); explicit_type = true; continue;
            case TT::KW_SHORT:
                if (short_type || long_type) fail("冲突的 short/long", "PARSE_SPECIFIERS");
                short_type = true; take(); explicit_type = true; continue;
            case TT::KW_LONG:
                if (long_type || short_type) fail("不支持重复 long 或 short long", "PARSE_SPECIFIERS");
                long_type = true; take(); explicit_type = true; continue;
            case TT::KW_INT:
                if (int_type || type->kind != TypeKind::Unknown)
                    fail("冲突的基本类型", "PARSE_SPECIFIERS");
                int_type = true; take(); explicit_type = true; continue;
            default: break;
            }
            if (type->kind != TypeKind::Unknown || int_type)
                fail("冲突的类型说明符", "PARSE_SPECIFIERS");
            take(); explicit_type = true;
            switch (token.type) {
            case TT::KW_VOID: type->kind = TypeKind::Void; break;
            case TT::KW_CHAR: type->kind = TypeKind::Char; break;
            case TT::KW_FLOAT: type->kind = TypeKind::Float; break;
            case TT::KW_DOUBLE: type->kind = TypeKind::Double; break;
            case TT::ID: type->kind = TypeKind::Named; type->name = token.lexeme; break;
            case TT::KW_STRUCT: case TT::KW_UNION: case TT::KW_ENUM: {
                const bool is_enum = token.type == TT::KW_ENUM;
                type->kind = is_enum ? TypeKind::Enum :
                    token.type == TT::KW_STRUCT ? TypeKind::Struct : TypeKind::Union;
                if (at(TT::ID)) type->name = take().lexeme;
                if (accept(TT::LBRACE)) {
                    // 匿名记录使用解析器内部标签，让定义和声明引用同一类型。
                    if (type->name.empty()) type->name = "<anonymous:" + std::to_string(++anonymous) + ">";
                    auto def = node(is_enum ? NodeType::EnumDef :
                        type->kind == TypeKind::Struct ? NodeType::StructDef : NodeType::UnionDef, token, type->name);
                    if (is_enum) {
                        if (at(TT::RBRACE)) fail("枚举定义不能为空", "PARSE_ENUM");
                        do {
                            const auto id = expect(TT::ID, "枚举项名字");
                            auto item = node(NodeType::EnumMember, id, id.lexeme);
                            if (accept(TT::ASSIGN)) item->children.push_back(expression(2));
                            finish(*item); def->children.push_back(std::move(item));
                            names.back()[id.lexeme] = false;
                        } while (accept(TT::COMMA) && !at(TT::RBRACE));
                    } else {
                        while (!at(TT::RBRACE) && !at(TT::END_OF_FILE)) {
                            auto members = declaration(false, true);
                            for (auto& member : members) def->children.push_back(std::move(member));
                        }
                        if (def->children.empty()) fail("记录定义不能为空", "PARSE_RECORD");
                    }
                    expect(TT::RBRACE, "'}'"); finish(*def);
                    s.definitions.push_back(std::move(def));
                } else if (type->name.empty()) fail("记录类型需要标签名或定义", "PARSE_RECORD");
                break;
            }
            default: fail("需要类型说明符", "PARSE_SPECIFIERS");
            }
        }
        if (!saw || !explicit_type) fail("声明需要明确的类型", "PARSE_SPECIFIERS");
        if (type->kind == TypeKind::Unknown)
            type->kind = short_type ? TypeKind::Short : long_type ? TypeKind::Long : TypeKind::Int;
        else if (type->kind == TypeKind::Double && long_type) type->kind = TypeKind::LongDouble;
        else if (short_type || long_type) fail("此类型不能带 short/long", "PARSE_SPECIFIERS");
        if ((sign || unsign) && type->kind != TypeKind::Int && type->kind != TypeKind::Char &&
            type->kind != TypeKind::Short && type->kind != TypeKind::Long)
            fail("signed/unsigned 只能修饰整数类型", "PARSE_SPECIFIERS");
        type->is_unsigned = unsign; s.type = type;
        return s;
    }

    // 声明符中的一层指针、数组或函数。
    struct Layer {
        TypeKind kind = TypeKind::Pointer; // 这一层是指针、数组还是函数。
        bool is_const = false; // 这一层指针是否不能被重新赋值。
        bool is_volatile = false; // 这一层指针是否带 volatile 限定符。
        std::optional<std::size_t> length; // 数组元素个数；省略界限时为空。
        bool prototype = true; // 函数是否写了参数类型；空括号表示未写原型。
        bool variadic = false; // 函数参数表是否以省略号结束。
        std::vector<Node> params; // 本层函数的形参节点，按声明顺序保存。
    };
    // 声明符信息，例如 a[3] 或 (*fn)(int)。
    struct Declarator {
        std::string name; // 声明的名字；无名形参或类型名中可以为空。
        std::vector<Layer> layers; // 从名字向外保存指针、数组、函数。
        SourceRange range; // 声明符从开始到结束的源码范围。
    };
    // 读取名字及其指针、数组、函数层次；abstract 为 true 时允许没有名字。
    Declarator declarator(bool abstract = false) {
        Nest nest(*this);
        Declarator d; d.range = peek().range;
        std::vector<Layer> pointers;
        while (accept(TT::STAR)) {
            if (pointers.size() >= 128) fail("指针嵌套过深", "PARSE_DEPTH", Level::Fatal);
            Layer layer;
            while (at(TT::KW_CONST) || at(TT::KW_VOLATILE)) {
                if (take().type == TT::KW_CONST) layer.is_const = true;
                else layer.is_volatile = true;
            }
            pointers.push_back(std::move(layer));
        }
        if (at(TT::ID)) d.name = take().lexeme;
        else if (at(TT::LPAREN) && (peek(1).type == TT::STAR || peek(1).type == TT::LPAREN ||
                 (peek(1).type == TT::ID && !alias(peek(1).lexeme)))) {
            take(); auto inner = declarator(abstract); expect(TT::RPAREN, "')'");
            d.name = inner.name; d.layers = std::move(inner.layers);
        } else if (!abstract) fail("声明符需要名字", "PARSE_DECLARATOR");
        while (at(TT::LBRACKET) || at(TT::LPAREN)) {
            if (d.layers.size() >= 128) fail("声明符嵌套过深", "PARSE_DEPTH", Level::Fatal);
            Layer layer;
            if (accept(TT::LBRACKET)) {
                layer.kind = TypeKind::Array;
                if (!at(TT::RBRACKET)) {
                    const auto size = expect(TT::INT_LITERAL, "正整数数组长度（暂不支持常量表达式界限）");
                    std::size_t used = 0;
                    std::uint64_t value = 0;
                    try { value = std::stoull(size.lexeme, &used, 0); }
                    catch (const std::exception&) { fail("数组长度超出范围", "PARSE_ARRAY_BOUND"); }
                    for (std::size_t i = used; i < size.lexeme.size(); ++i)
                        if (std::string("uUlL").find(size.lexeme[i]) == std::string::npos)
                            fail("非法数组长度", "PARSE_ARRAY_BOUND");
                    if (!value || value > std::numeric_limits<std::size_t>::max())
                        fail("数组长度必须为可表示的正整数", "PARSE_ARRAY_BOUND");
                    layer.length = static_cast<std::size_t>(value);
                }
                expect(TT::RBRACKET, "']'");
            } else {
                take(); layer.kind = TypeKind::Function;
                names.emplace_back();
                try {
                    if (at(TT::RPAREN)) layer.prototype = false;
                    else if (at(TT::KW_VOID) && peek(1).type == TT::RPAREN) take();
                    else {
                        do {
                            if (accept(TT::ELLIPSIS)) {
                                if (layer.params.empty()) fail("省略号前需要至少一个形参", "PARSE_PARAMETER");
                                layer.variadic = true; break;
                            }
                            const auto start = peek();
                            auto ps = specs();
                            if (ps.storage != StorageClass::None && ps.storage != StorageClass::Register)
                                fail("形参只允许 register 存储类别", "PARSE_PARAMETER");
                            if (!ps.definitions.empty()) fail("请先单独声明形参使用的记录类型", "PARSE_PARAMETER");
                            auto pd = declarator(true);
                            auto param = node(NodeType::ParamDecl, start, pd.name);
                            param->declared_type = build_type(ps.type, pd); param->storage = ps.storage;
                            if (param->declared_type->kind == TypeKind::Void)
                                fail("void 只能单独表示空形参列表", "PARSE_PARAMETER");
                            finish(*param);
                            if (!pd.name.empty()) names.back()[pd.name] = false;
                            layer.params.push_back(std::move(param));
                        } while (accept(TT::COMMA));
                    }
                    expect(TT::RPAREN, "')'");
                } catch (...) { names.pop_back(); throw; }
                names.pop_back();
            }
            d.layers.push_back(std::move(layer));
        }
        for (auto i = pointers.rbegin(); i != pointers.rend(); ++i) d.layers.push_back(std::move(*i));
        if (pos) d.range.end = tokens[pos - 1].range.end;
        return d;
    }
    // 按声明符的层次组合完整类型，不改变基础类型。
    TypePtr build_type(TypePtr base, const Declarator& d) {
        for (auto i = d.layers.rbegin(); i != d.layers.rend(); ++i) {
            auto type = make_type_info(); type->kind = i->kind; type->base = base;
            type->is_const = i->is_const; type->is_volatile = i->is_volatile;
            type->array_length = i->length; type->has_prototype = i->prototype; type->variadic = i->variadic;
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
    // 读取转换或 sizeof 中的无名类型，返回完整类型。
    TypePtr type_name() {
        auto s = specs(false);
        if (!s.definitions.empty()) fail("类型名中的内联记录定义暂不支持", "PARSE_TYPE_NAME");
        auto d = declarator(true);
        if (!d.name.empty()) fail("类型名不能带声明名字", "PARSE_TYPE_NAME");
        return build_type(s.type, d);
    }
    // 读取一条声明，返回其中的类型定义、变量或函数节点。
    std::vector<Node> declaration(bool external, bool member = false) {
        Nest nest(*this);
        const auto start = peek();
        auto s = specs(!member);
        if (member && !s.definitions.empty())
            fail("成员类型的内联定义暂不支持，请先单独定义记录类型", "PARSE_MEMBER");
        std::vector<Node> nodes = std::move(s.definitions);
        if (accept(TT::SEMI)) {
            if (member || s.storage != StorageClass::None)
                fail("此声明需要声明符", "PARSE_DECLARATOR");
            if (nodes.empty() && (s.type->kind == TypeKind::Struct || s.type->kind == TypeKind::Union))
                nodes.push_back(node(s.type->kind == TypeKind::Struct ? NodeType::StructDef : NodeType::UnionDef,
                                     start, s.type->name));
            if (nodes.empty()) fail("声明缺少声明符", "PARSE_DECLARATOR");
            for (auto& n : nodes) finish(*n);
            return nodes;
        }
        bool first_declarator = true;
        do {
            auto d = declarator();
            const auto type = build_type(s.type, d);
            const bool function = type->kind == TypeKind::Function && s.storage != StorageClass::Typedef;
            if (member && (function || at(TT::COLON)))
                fail("成员不支持函数声明或位域", "PARSE_MEMBER");
            auto n = node(member ? NodeType::MemberDecl : s.storage == StorageClass::Typedef ?
                NodeType::TypedefDecl : function ? NodeType::FunctionDecl : NodeType::VarDecl, start, d.name);
            n->declared_type = type; n->storage = s.storage;
            if (!member) names.back()[d.name] = s.storage == StorageClass::Typedef;
            if (function) {
                n->children = std::move(d.layers.front().params);
                if (at(TT::LBRACE)) {
                    if (!external) fail("函数定义只能出现在顶层", "PARSE_FUNCTION");
                    if (!first_declarator) fail("函数定义不能与其他声明符共用声明", "PARSE_FUNCTION");
                    for (const auto& param : n->children)
                        if (param->name.empty()) fail("函数定义中的形参需要名字", "PARSE_PARAMETER");
                    n->kind = NodeType::FunctionDef;
                    names.emplace_back();
                    for (const auto& param : n->children) names.back()[param->name] = false;
                    try { n->children.push_back(block(false)); }
                    catch (...) { names.pop_back(); throw; }
                    names.pop_back(); finish(*n); nodes.push_back(std::move(n));
                    return nodes;
                }
            }
            if (accept(TT::ASSIGN)) {
                if (member || function || s.storage == StorageClass::Typedef)
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
    // 读取初始化表达式或花括号列表，返回初始化节点。
    Node initializer() {
        Nest nest(*this);
        if (!at(TT::LBRACE)) return expression(2);
        const auto start = take(); auto n = node(NodeType::InitList, start);
        if (at(TT::RBRACE)) fail("初始化列表不能为空", "PARSE_INITIALIZER");
        do { n->children.push_back(initializer()); }
        while (accept(TT::COMMA) && !at(TT::RBRACE));
        expect(TT::RBRACE, "'}'"); finish(*n); return n;
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
    // 读取花括号块；默认建立一层 typedef 名字作用域。
    Node block(bool scope = true) {
        Nest nest(*this);
        const auto start = expect(TT::LBRACE, "'{'（控制体必须使用花括号）");
        auto n = node(NodeType::Block, start);
        bool saw_statement = false;
        if (scope) names.emplace_back();
        try {
            while (!at(TT::RBRACE) && !at(TT::END_OF_FILE) && !stopped()) {
                const auto before = pos;
                const auto saved_names = names;
                try {
                    if (specifier() && !(at(TT::ID) && peek(1).type == TT::COLON)) {
                        if (saw_statement)
                            fail("同一块中的声明必须放在语句之前", "PARSE_DECLARATION_ORDER");
                        auto ds = declaration(false);
                        for (auto& d : ds) n->children.push_back(std::move(d));
                    } else {
                        saw_statement = true;
                        n->children.push_back(statement());
                    }
                } catch (const ParseFailure&) {
                    names = saved_names;
                    if (stopped()) throw;
                    recover(before, false);
                }
            }
            expect(TT::RBRACE, "'}'");
        } catch (...) { if (scope) names.pop_back(); throw; }
        if (scope) names.pop_back();
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
        if (at(TT::ID) && peek(1).type == TT::COLON) {
            take(); take(); auto n = node(NodeType::Label, start, start.lexeme);
            n->children.push_back(statement()); finish(*n); return n;
        }
        if (accept(TT::KW_IF)) {
            auto n = node(NodeType::If, start); n->children.push_back(condition());
            n->children.push_back(block());
            if (accept(TT::KW_ELSE)) n->children.push_back(block());
            finish(*n); return n;
        }
        if (accept(TT::KW_WHILE) || accept(TT::KW_SWITCH)) {
            auto n = node(start.type == TT::KW_WHILE ? NodeType::While : NodeType::Switch, start);
            n->children.push_back(condition()); n->children.push_back(block()); finish(*n); return n;
        }
        if (accept(TT::KW_DO)) {
            auto n = node(NodeType::DoWhile, start); n->children.push_back(block());
            expect(TT::KW_WHILE, "while"); n->children.push_back(condition());
            expect(TT::SEMI, "';'"); finish(*n); return n;
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
        if (accept(TT::KW_CASE) || accept(TT::KW_DEFAULT)) {
            auto n = node(start.type == TT::KW_CASE ? NodeType::Case : NodeType::Default, start);
            if (start.type == TT::KW_CASE) n->children.push_back(expression(3));
            expect(TT::COLON, "':'"); n->children.push_back(statement()); finish(*n); return n;
        }
        if (accept(TT::KW_RETURN)) {
            auto n = node(NodeType::Return, start);
            if (!at(TT::SEMI)) n->children.push_back(expression());
            expect(TT::SEMI, "';'"); finish(*n); return n;
        }
        if (accept(TT::KW_BREAK) || accept(TT::KW_CONTINUE) || accept(TT::KW_GOTO)) {
            auto n = node(start.type == TT::KW_BREAK ? NodeType::Break :
                start.type == TT::KW_CONTINUE ? NodeType::Continue : NodeType::Goto, start);
            if (start.type == TT::KW_GOTO) n->name = expect(TT::ID, "标签名字").lexeme;
            expect(TT::SEMI, "';'"); finish(*n); return n;
        }
        auto n = node(NodeType::ExprStmt, start); n->children.push_back(expression());
        expect(TT::SEMI, "';'"); finish(*n); return n;
    }

    // 返回二元运算符优先级；不是二元运算符时返回 0。
    static int precedence(TT type) {
        switch (type) {
        case TT::COMMA: return 1;
        case TT::ASSIGN: case TT::PLUS_ASSIGN: case TT::MINUS_ASSIGN: case TT::STAR_ASSIGN:
        case TT::SLASH_ASSIGN: case TT::PERCENT_ASSIGN: case TT::AND_ASSIGN: case TT::OR_ASSIGN:
        case TT::XOR_ASSIGN: case TT::SHIFT_LEFT_ASSIGN: case TT::SHIFT_RIGHT_ASSIGN: return 2;
        case TT::QUESTION: return 3;
        case TT::OR: return 4;
        case TT::AND: return 5;
        case TT::BIT_OR: return 6;
        case TT::BIT_XOR: return 7;
        case TT::AMP: return 8;
        case TT::EQ: case TT::NE: return 9;
        case TT::LT: case TT::LE: case TT::GT: case TT::GE: return 10;
        case TT::SHIFT_LEFT: case TT::SHIFT_RIGHT: return 11;
        case TT::PLUS: case TT::MINUS: return 12;
        case TT::STAR: case TT::SLASH: case TT::PERCENT: return 13;
        default: return 0;
        }
    }
    // 判断节点是否符合赋值左部的一元表达式语法；可否赋值由语义阶段检查。
    static bool unary_shape(const ASTNode& n) {
        switch (n.kind) {
        case NodeType::Identifier: case NodeType::IntLiteral: case NodeType::FloatLiteral:
        case NodeType::CharLiteral: case NodeType::StringLiteral: case NodeType::UnaryOp:
        case NodeType::Call: case NodeType::ArrayAccess: case NodeType::MemberAccess: case NodeType::Sizeof:
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
            auto n = node(priority == 2 ? NodeType::Assign : priority == 3 ?
                NodeType::Conditional : NodeType::BinaryOp, op, op.lexeme);
            n->range.begin = left->range.begin; n->range.file = left->range.file;
            if (priority == 2 && !shape) fail("赋值左部必须符合一元表达式形状", "PARSE_ASSIGNMENT_TARGET");
            n->children.push_back(std::move(left));
            if (priority == 3) {
                n->children.push_back(expression()); expect(TT::COLON, "':'");
                n->children.push_back(expression(3));
            } else n->children.push_back(expression(priority == 2 ? priority : priority + 1));
            finish(*n); left = std::move(n); shape = false;
        }
        parenthesized = false;
        return left;
    }
    bool parenthesized = false; // 最近的一元表达式是否来自括号，供赋值左部语法检查使用。
    // 读取前缀运算、类型转换、sizeof 或基本表达式。
    Node unary() {
        Nest nest(*this);
        const auto start = peek(); parenthesized = false;
        if (accept(TT::KW_SIZEOF)) {
            auto n = node(NodeType::Sizeof, start);
            if (at(TT::LPAREN)) {
                take();
                if (specifier()) n->declared_type = type_name();
                else n->children.push_back(expression());
                expect(TT::RPAREN, "')'");
            } else n->children.push_back(unary());
            finish(*n); parenthesized = false; return n;
        }
        switch (start.type) {
        case TT::PLUS: case TT::MINUS: case TT::NOT: case TT::BIT_NOT:
        case TT::AMP: case TT::STAR: case TT::PLUS_PLUS: case TT::MINUS_MINUS: {
            take(); auto n = node(NodeType::UnaryOp, start,
                start.type == TT::PLUS_PLUS ? "pre++" : start.type == TT::MINUS_MINUS ? "pre--" : start.lexeme);
            n->children.push_back(unary()); finish(*n); parenthesized = false; return n;
        }
        default: break;
        }
        if (at(TT::LPAREN)) {
            take();
            if (specifier()) {
                auto n = node(NodeType::Cast, start); n->declared_type = type_name();
                expect(TT::RPAREN, "')'"); n->children.push_back(unary()); finish(*n);
                parenthesized = false; return n;
            }
            auto n = expression(); expect(TT::RPAREN, "')'");
            n->range.begin = start.range.begin; finish(*n);
            n = postfix(std::move(n)); parenthesized = true; return n;
        }
        Node n;
        switch (start.type) {
        case TT::ID:
            if (alias(start.lexeme)) fail("类型别名不能作为表达式", "PARSE_TYPE_NAME");
            n = node(NodeType::Identifier, take(), start.lexeme); break;
        case TT::INT_LITERAL: n = node(NodeType::IntLiteral, take(), start.lexeme); break;
        case TT::FLOAT_LITERAL: n = node(NodeType::FloatLiteral, take(), start.lexeme); break;
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
    // 在已有表达式后读取调用、下标、成员访问和后置自增自减。
    Node postfix(Node base) {
        while (at(TT::LPAREN) || at(TT::LBRACKET) || at(TT::DOT) || at(TT::ARROW) ||
               at(TT::PLUS_PLUS) || at(TT::MINUS_MINUS)) {
            const auto op = take(); Node n;
            if (op.type == TT::LPAREN) {
                n = node(NodeType::Call, op, base->kind == NodeType::Identifier ? base->name : "");
                n->children.push_back(std::move(base));
                if (!at(TT::RPAREN)) do { n->children.push_back(expression(2)); } while (accept(TT::COMMA));
                expect(TT::RPAREN, "')'");
            } else if (op.type == TT::LBRACKET) {
                n = node(NodeType::ArrayAccess, op); n->children.push_back(std::move(base));
                n->children.push_back(expression()); expect(TT::RBRACKET, "']'");
            } else if (op.type == TT::DOT || op.type == TT::ARROW) {
                const auto member = expect(TT::ID, "成员名字");
                n = node(NodeType::MemberAccess, op, member.lexeme);
                if (op.type == TT::ARROW) {
                    auto dereference = node(NodeType::UnaryOp, op, "*");
                    dereference->range = base->range; dereference->children.push_back(std::move(base));
                    finish(*dereference);
                    base = std::move(dereference);
                }
                n->children.push_back(std::move(base));
            } else {
                n = node(NodeType::UnaryOp, op, op.type == TT::PLUS_PLUS ? "post++" : "post--");
                n->children.push_back(std::move(base));
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
            const auto before = pos; const auto saved_names = names;
            try {
                auto declarations = declaration(true);
                for (auto& d : declarations) root->children.push_back(std::move(d));
            } catch (const ParseFailure&) {
                names = saved_names;
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
