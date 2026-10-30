#pragma once

#include "minic/type_arena.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace minic {

template <typename T>
struct MaybeValue {
    bool present = false;
    T data{};
    MaybeValue() = default;
    MaybeValue(const T& value) : present(true), data(value) {}
    MaybeValue(T&& value) : present(true), data(std::move(value)) {}
    bool has_value() const noexcept { return present; }
    explicit operator bool() const noexcept { return present; }
    T& value() { return data; }
    const T& value() const { return data; }
    T& operator*() { return data; }
    const T& operator*() const { return data; }
    T* operator->() { return &data; }
    const T* operator->() const { return &data; }
    template <typename U>
    T value_or(U&& fallback) const { return present ? data : static_cast<T>(std::forward<U>(fallback)); }
    void reset() noexcept { present = false; data = T{}; }
    bool operator==(const MaybeValue& other) const { return present == other.present && (!present || data == other.data); }
    bool operator!=(const MaybeValue& other) const { return !(*this == other); }
};
struct EmptyValue { template <typename T> operator MaybeValue<T>() const { return {}; } };
inline constexpr EmptyValue emptyValue{};

// ==================== 公共基础：各阶段共用 ====================

// 符号编号。
using SymbolId = unsigned;
// 作用域编号。
using ScopeId = unsigned;
// 记录类型编号。
using RecordId = unsigned;
// 常量编号。
using ConstantId = unsigned;
// 无效编号，表示尚未绑定或没有对应条目。
inline constexpr unsigned invalid_id =
    std::numeric_limits<unsigned>::max();

// 源码中的一个位置。
struct SourceLocation {
    unsigned line = 1; // 行号，从 1 开始。
    unsigned col = 1; // 字节列号，从 1 开始。
    std::size_t offset = 0; // 从文件开头算起的字节位置，从 0 开始。
};

// 源码中的一段范围。
struct SourceRange {
    std::string file; // 源文件名。
    SourceLocation begin; // 开始位置，包含此位置。
    SourceLocation end; // 结束位置，不包含此位置。
};

// 编译阶段。
enum class Phase { Lexer, Parser, Semantic, IR, Runtime };
// 诊断级别。
enum class Level { Note, Warning, Error, Fatal };

// 一条错误、警告或说明信息。
struct Diagnostic {
    Phase phase = Phase::Lexer; // 产生诊断的编译阶段。
    Level level = Level::Error; // 诊断级别：说明、警告、错误或致命错误。
    unsigned line = 1; // 报错行号，与 range.begin.line 一致。
    unsigned col = 1; // 报错列号，与 range.begin.col 一致。
    std::string message; // 给用户看的中文提示。
    std::string code; // 错误代码，用于分类和查询。
    SourceRange range; // 主要报错位置。
    std::vector<SourceRange> related; // 关联位置，如上一次声明的位置。
};

// 判断诊断中是否有错误或致命错误。有则返回 true。
inline bool has_errors(const std::vector<Diagnostic>& diagnostics) {
    for (const auto& d : diagnostics) {
        if (d.level == Level::Error || d.level == Level::Fatal) return true;
    }
    return false;
}

struct TypeInfo;
// 指向只读类型信息的普通指针；对象由 TypeArena 统一释放。
using TypePtr = const TypeInfo*;

// 常量值，使用显式标签保存整数、浮点或字符串。
enum class ConstantKind { Empty, Integer, Unsigned, Floating, Text };
struct ConstantValue {
    ConstantKind kind = ConstantKind::Empty;
    long long integer = 0;
    unsigned long long unsigned_integer = 0;
    double floating = 0;
    std::string text;
    ConstantValue() = default;
    ConstantValue(long long value) : kind(ConstantKind::Integer), integer(value) {}
    ConstantValue(unsigned long long value) : kind(ConstantKind::Unsigned), unsigned_integer(value) {}
    ConstantValue(double value) : kind(ConstantKind::Floating), floating(value) {}
    ConstantValue(const std::string& value) : kind(ConstantKind::Text), text(value) {}
    ConstantValue(std::string&& value) : kind(ConstantKind::Text), text(std::move(value)) {}
};
inline bool constant_empty(const ConstantValue& value) { return value.kind == ConstantKind::Empty; }
template <typename T> const T* constant_get_if(const ConstantValue& value);
template <typename T> T* constant_get_if(ConstantValue& value) {
    return const_cast<T*>(constant_get_if<T>(static_cast<const ConstantValue&>(value)));
}
template <> inline const long long* constant_get_if<long long>(const ConstantValue& value) { return value.kind == ConstantKind::Integer ? &value.integer : nullptr; }
template <> inline const unsigned long long* constant_get_if<unsigned long long>(const ConstantValue& value) { return value.kind == ConstantKind::Unsigned ? &value.unsigned_integer : nullptr; }
template <> inline const double* constant_get_if<double>(const ConstantValue& value) { return value.kind == ConstantKind::Floating ? &value.floating : nullptr; }
template <> inline const std::string* constant_get_if<std::string>(const ConstantValue& value) { return value.kind == ConstantKind::Text ? &value.text : nullptr; }
template <typename T> const T& constant_get(const ConstantValue& value);
template <> inline const long long& constant_get<long long>(const ConstantValue& value) { return value.integer; }
template <> inline const unsigned long long& constant_get<unsigned long long>(const ConstantValue& value) { return value.unsigned_integer; }
template <> inline const double& constant_get<double>(const ConstantValue& value) { return value.floating; }
template <> inline const std::string& constant_get<std::string>(const ConstantValue& value) { return value.text; }

// 表达式类别：非表达式、左值、右值或函数。
enum class ValueCategory { None, LValue, RValue, Function };
// ==================== 1. 词法分析：单词与单词表 ====================

// 单词种别，预留种别不代表语法功能已实现。
enum class TokenType {
    END_OF_FILE, ID, INT_LITERAL, FLOAT_LITERAL, CHAR_LITERAL, STRING_LITERAL,
    KW_INT, KW_FLOAT, KW_CHAR, KW_STRUCT, KW_RETURN, KW_IF, KW_ELSE,
    KW_WHILE, KW_FOR, KW_VOID,
    PLUS, MINUS, STAR, SLASH, ASSIGN, LT, LE, GT, GE, EQ, NE,
    AND, OR, NOT, AMP,
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET, SEMI, COMMA, DOT,
    KW_BREAK, KW_CONTINUE,
    PERCENT
};

// 词法分析
struct Token {
    TokenType type = TokenType::END_OF_FILE; // 单词种别，如关键字、标识符、运算符。
    std::string lexeme; // 单词原文，保留引号和转义。
    unsigned line = 1; // 单词起始行号，与 range.begin.line 一致。
    unsigned col = 1; // 单词起始列号，与 range.begin.col 一致。
    SourceRange range; // 单词在源码中的位置范围。
};

// 词法分析结果。
struct LexResult {
    std::vector<Token> tokens; // 词法分析得到的单词表，末尾包含 EOF。
    std::vector<Diagnostic> diagnostics; // 词法分析的诊断列表。
    // 判断本阶段是否成功。成功返回 true，否则返回 false。
    bool ok() const { return !has_errors(diagnostics); }
};

// ==================== 2. 语法分析：语法树 ====================

// 语法树节点的种类；源码语法和各阶段之间共用这一组节点。
enum class NodeType {
    Program, FunctionDef, ParamDecl, VarDecl, StructDef,
    MemberDecl, Block, ExprStmt, If, While, For, Return, Empty,
    Identifier, IntLiteral, FloatLiteral, CharLiteral, StringLiteral,
    Assign, BinaryOp, UnaryOp, Call, ImplicitCast,
    ArrayAccess, MemberAccess, Break, Continue,
    Error
};

// 抽象语法树节点。
struct ASTNode {
    NodeType kind = NodeType::Error; // 语法节点种类，如声明、赋值、调用。
    std::string name; // 名字、运算符、标签或字面量原文。
    ConstantValue value; // 解码后的常量值；非常量时为空。
    SourceRange range; // 节点对应的源码范围。
    TypePtr declared_type = nullptr; // 源码中写出的声明或转换目标类型。
    TypePtr type = nullptr; // 语义检查后的类型；检查前为空。
    ValueCategory category = ValueCategory::None; // 节点是左值、右值、函数还是非表达式。
    SymbolId symbol_id = invalid_id; // 对应的变量、形参或函数编号。
    ScopeId scope_id = invalid_id; // 节点所属的作用域编号。
    RecordId record_id = invalid_id; // 节点涉及的结构体编号。
    MaybeValue<std::size_t> member_index; // 成员在成员表中的下标；非成员访问时为空。
    std::vector<ASTNode*> children; // 子节点列表；排列顺序见 docs/公共约定/数据结构与AST约定.md。
};

// AST 节点的统一内存池。节点之间只保存普通指针，由 Arena 在析构时统一释放。
struct NodeArena {
    std::vector<ASTNode*> nodes;
    NodeArena() = default;
    ~NodeArena() { clear(); }
    ASTNode* make() {
        ASTNode* node = new ASTNode();
        nodes.push_back(node);
        return node;
    }
    void clear() {
        for (ASTNode* node : nodes) delete node;
        nodes.clear();
    }
    NodeArena(const NodeArena&) = delete;
    NodeArena& operator=(const NodeArena&) = delete;
    NodeArena(NodeArena&& other) noexcept { nodes.swap(other.nodes); }
    NodeArena& operator=(NodeArena&& other) noexcept {
        if (this != &other) { clear(); nodes.swap(other.nodes); }
        return *this;
    }
};

// 程序根节点，其 kind 为 Program。
using Program = ASTNode;

// 语法分析结果。
struct ParseResult {
    NodeArena arena; // 语法树节点的所有权池。
    ASTNode* root = nullptr; // 语法树根节点；语法失败时为空。
    std::vector<Diagnostic> diagnostics; // 语法分析的诊断列表。
    // 判断本阶段是否成功。成功返回 true，否则返回 false。
    bool ok() const { return root && !has_errors(diagnostics); }
};

// ==================== 3. 语义分析：类型检查与类型信息 ====================

// 语义分析会填写 ASTNode 的 type、category、symbol_id 等字段。
// SemanticResult 用到完整符号表数据，定义在下面的 SymbolTableData 后。

// 类型类别；源码支持 void、char、int、float 和 struct。
// Void 用于函数；Address 只供数组参数和输入输出使用。
enum class TypeKind {
    Unknown, Error, Void, Char, Short, Int, Long, Float, Double, LongDouble,
    Address, Array, Function, Struct // Address 只供内部寻址，源码不能声明。
};

// C 类型信息。
struct TypeInfo {
    TypeKind kind = TypeKind::Unknown; // 类型种类，如 int、float、数组、结构体。
    bool is_unsigned = false; // 后端数值表示使用的无符号标记，源码不产生它。
    bool is_const = false; // 内部类型限定，源码教学子集不开放 const。
    TypePtr base = nullptr; // 内部地址的目标类型、数组的元素类型或函数的返回类型。
    MaybeValue<std::size_t> array_length; // 本层数组长度；为空表示尚未确定。
    std::vector<TypePtr> params; // 函数的形参类型表，按声明顺序保存。
    bool variadic = false; // 仅供 printf/scanf 内建签名使用。
    // 函数参数表和返回类型组成完整签名。
    std::string name; // 结构体标签名。
    RecordId record_id = invalid_id; // 对应的结构体编号。
};

// ==================== 4. 符号表：名字、作用域、记录类型与常量池 ====================

// 普通符号的类别。
enum class SymbolKind { Variable, Parameter, Array, Function };
// 内建函数种类。
enum class BuiltinKind { None, Printf, Scanf };

// 一条普通符号信息。
struct SymbolEntry {
    SymbolId id = invalid_id; // 符号编号，等于符号表下标。
    std::string name; // 变量、形参或函数等的名字。
    SymbolKind kind = SymbolKind::Variable; // 符号类别，如变量、形参、函数。
    TypePtr type = nullptr; // 符号类型；函数类型包含返回类型和形参。
    ScopeId scope = invalid_id; // 声明所在的作用域编号。
    unsigned line = 1; // 声明行号，与 range.begin.line 一致。
    unsigned col = 1; // 声明列号，与 range.begin.col 一致。
    SourceRange range; // 声明在源码中的位置范围。
    bool is_defined = false; // 是否已完成定义：函数有函数体，对象有初始化。
    BuiltinKind builtin = BuiltinKind::None; // 内建函数标记，如 printf、scanf。
    MaybeValue<std::size_t> offset; // 对象的存储字节偏移；未分配时为空。
};

// 一条成员信息。
struct MemberEntry {
    std::string name; // 成员名字。
    TypePtr type = nullptr; // 成员的数据类型。
    MaybeValue<std::size_t> offset; // 成员相对对象起点的字节偏移；未计算时为空。
    SourceRange range; // 成员声明的位置。
};

// 记录类型种类：结构体。
enum class RecordKind { Struct };

// 结构体的记录信息。
struct StructEntry {
    RecordId id = invalid_id; // 记录类型编号，等于记录表下标。
    std::string tag; // 标签名；匿名类型时为空。
    RecordKind kind = RecordKind::Struct; // 记录种类：结构体。
    ScopeId scope = invalid_id; // 标签声明所在的作用域编号。
    bool is_complete = false; // 是否已完成定义。
    std::vector<MemberEntry> members; // 结构体的成员信息表。
    MaybeValue<std::size_t> size; // 对象总字节数，包含填充；未计算时为空。
    MaybeValue<std::size_t> alignment; // 对象的字节对齐要求；未计算时为空。
    SourceRange range; // 标签声明或定义的位置。
};

// 作用域种类：全局、函数或块。
enum class ScopeKind { Global, Function, Block };

// 一个作用域的信息。
struct ScopeEntry {
    ScopeId id = invalid_id; // 作用域编号，等于作用域表下标。
    ScopeId parent = invalid_id; // 外层作用域编号；全局没有外层。
    ScopeKind kind = ScopeKind::Global; // 作用域种类：全局、函数或块。
    SourceRange range; // 作用域在源码中的位置范围。
    std::unordered_map<std::string, SymbolId> symbols; // 本层普通名字表：名字对应符号编号。
    std::unordered_map<std::string, RecordId> tags; // 本层标签表：标签名对应记录编号。
};

// 符号表管理的数据。
struct SymbolTableData {
    std::vector<SymbolEntry> symbols; // 所有普通符号信息，包括已退出作用域的符号。
    std::vector<StructEntry> records; // 结构体的记录信息表。
    std::vector<ScopeEntry> scopes; // 所有作用域的信息表。
    std::vector<ScopeId> active_scopes; // 当前作用域查询栈，从栈顶向外查找名字。
};

// 一条常量信息。
struct ConstantEntry {
    ConstantId id = invalid_id; // 常量编号，等于常量表下标。
    TypePtr type = nullptr; // 常量的数据类型。
    ConstantValue value; // 解码后的常量值；字符串不附末尾零。
    std::string spelling; // 常量第一次出现时的源码写法。
    SourceRange range; // 常量第一次出现的位置。
};

// 常量池数据。
struct ConstantPoolData {
    std::vector<ConstantEntry> entries; // 常量信息表，按首次登记顺序保存。
};

// 以下仍是语义分析的结果；先定义符号表，才能把它作为成员保存。
// 语义分析结果。
struct SemanticResult {
    SymbolTableData symbols; // 语义检查建立的符号表数据。
    std::vector<Diagnostic> diagnostics; // 语义检查的诊断列表。
    // 判断本阶段是否成功。成功返回 true，否则返回 false。
    bool ok() const { return !has_errors(diagnostics); }
};

// ==================== 5. 中间代码生成：四元式与程序 IR ====================

// 一条四元式指令。
struct Quadruple {
    std::string op; // 操作码
    std::string arg1 = "-"; // 第一个操作数；不用时为 "-"。
    std::string arg2 = "-"; // 第二个操作数；call 中为实参数量。
    std::string result = "-"; // 结果位置或跳转标号；不用时为 "-"。
};

// 表达式结果所在位置的信息。
struct Place {
    std::string name; // 结果所在位置，如 %s3、%t2、%c0。
    TypePtr type = nullptr; // 结果的数据类型。
    bool is_const = false; // 结果是否为编译期常量。
};

// 一条临时变量信息。
struct TemporaryEntry {
    std::string name; // 临时变量名字，如 %t2。
    TypePtr type = nullptr; // 临时变量的数据类型。
};

// 一个函数的中间代码。
struct IRFunction {
    SymbolId symbol_id = invalid_id; // 函数在符号表中的编号。
    std::vector<SymbolId> parameters; // 形参编号表，按声明顺序保存。
    std::vector<TemporaryEntry> temporaries; // 本函数的临时变量表。
    std::vector<Quadruple> quads; // 本函数的四元式指令表。
    std::vector<SourceRange> locations; // 指令的源码位置表，与 quads 一一对应。
};

// 整个程序的中间代码。
struct IRProgram {
    ConstantPoolData constants; // 整个程序共享的常量池。
    std::vector<SymbolId> globals; // 全局变量和全局数组的符号编号表。
    std::vector<Quadruple> global_initializers; // 进入 main 前执行的初始化指令。
    std::vector<SourceRange> global_locations; // 全局初始化指令的位置表，与指令一一对应。
    std::vector<TemporaryEntry> global_temporaries; // 全局初始化使用的临时变量表。
    std::vector<IRFunction> functions; // 程序中各函数的中间代码。
    SymbolId entry_function = invalid_id; // 入口 main 的符号编号；未找到时为无效编号。
};

// 中间代码生成结果。
struct IRResult {
    IRProgram program; // 生成的程序中间代码。
    std::vector<Diagnostic> diagnostics; // 中间代码生成的诊断列表。
    // 判断本阶段是否成功。成功返回 true，否则返回 false。
    bool ok() const { return !has_errors(diagnostics); }
};

}




