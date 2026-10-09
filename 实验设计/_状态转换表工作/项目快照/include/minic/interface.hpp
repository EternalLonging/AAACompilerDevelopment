#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// 公共数据接口 v1.1；各模块统一使用，数据约定见 docs/interface.md，函数约定见 docs/modules.md。
namespace minic {

using SymbolId = std::uint32_t; // 符号编号，对应 SymbolTableData.symbols 的下标。
using ScopeId = std::uint32_t;  // 作用域编号，对应 SymbolTableData.scopes 的下标。
using RecordId = std::uint32_t; // 记录类型编号，对应 SymbolTableData.records 的下标。
// 无效编号：表示尚未绑定、没有父作用域或没有对应条目，不能用于访问容器。
inline constexpr std::uint32_t invalid_id =
    std::numeric_limits<std::uint32_t>::max();

// 源码中的一个位置；列按 UTF-8 字节计数，Tab 算一列，CRLF 算一次换行。
struct SourceLocation {
    std::uint32_t line = 1; // 行号，从 1 开始。
    std::uint32_t col = 1;  // 当前行内的字节列号，从 1 开始。
    std::size_t offset = 0; // 相对源文件开头的 UTF-8 字节偏移，从 0 开始。
};

// 一段源码的范围，使用左闭右开区间 [begin, end)。
struct SourceRange {
    std::string file;      // 来源文件名；内建声明使用 "<builtin>"。
    SourceLocation begin; // 范围起点，包含该位置的字节。
    SourceLocation end;   // 范围终点，不包含该位置的字节；文件结束处 begin == end。
};

// 单词种别：用于词法/语法分析，与 C 数据类型 TypeKind 不同。
// 大写命名与需求资料对应；预留的种别不代表相关语法已经实现。
enum class TokenType {
    END_OF_FILE, ID, INT_LITERAL, FLOAT_LITERAL, CHAR_LITERAL, STRING_LITERAL,
    KW_INT, KW_FLOAT, KW_CHAR, KW_STRUCT, KW_RETURN, KW_IF, KW_ELSE,
    KW_WHILE, KW_FOR, KW_VOID,
    PLUS, MINUS, STAR, SLASH, ASSIGN, LT, LE, GT, GE, EQ, NE,
    AND, OR, NOT, AMP,
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET, SEMI, COMMA, DOT,
    // M2 及后续阶段使用的关键字、运算符和预处理符号。
    KW_BREAK, KW_CONTINUE, KW_SWITCH, KW_CASE, KW_DEFAULT, KW_DO,
    KW_TYPEDEF, KW_UNION, KW_ENUM, KW_DOUBLE, KW_LONG, KW_SHORT, KW_UNSIGNED,
    KW_SIGNED, KW_STATIC, KW_EXTERN, KW_AUTO, KW_REGISTER, KW_CONST,
    KW_VOLATILE, KW_SIZEOF, KW_GOTO,
    PERCENT, PLUS_PLUS, MINUS_MINUS, PLUS_ASSIGN, MINUS_ASSIGN,
    STAR_ASSIGN, SLASH_ASSIGN, PERCENT_ASSIGN, BIT_OR, BIT_XOR, BIT_NOT,
    SHIFT_LEFT, SHIFT_RIGHT, AND_ASSIGN, OR_ASSIGN, XOR_ASSIGN,
    SHIFT_LEFT_ASSIGN, SHIFT_RIGHT_ASSIGN, QUESTION, COLON, ARROW,
    ELLIPSIS, HASH, HASH_HASH
};

// 词法分析产生的一个单词，例如关键字 int、标识符 r、运算符 +。
struct Token {
    TokenType type = TokenType::END_OF_FILE; // 单词种别；END_OF_FILE 为输入结束标记。
    std::string lexeme; // 源码原文，保留字面量的引号和转义；结束标记原文为空。
    std::uint32_t line = 1; // 起始行号，必须等于 range.begin.line。
    std::uint32_t col = 1;  // 起始列号，必须等于 range.begin.col。
    SourceRange range;     // 该单词在源文件中覆盖的完整范围。
};

// 诊断所属阶段：预处理、词法、语法、语义、中间代码生成、运行时。
enum class Phase { Preprocess, Lexer, Parser, Semantic, IR, Runtime };
// 诊断级别：说明、警告、错误、致命错误；后两者阻止进入下一阶段。
enum class Level { Note, Warning, Error, Fatal };

// 所有模块共用的诊断信息，用于汇总报错、定位源码和导出报告。
struct Diagnostic {
    Phase phase = Phase::Lexer; // 产生这条诊断的编译阶段。
    Level level = Level::Error; // 严重程度，决定是否阻止后续编译。
    std::uint32_t line = 1; // 主要报错位置的行号，等于 range.begin.line。
    std::uint32_t col = 1;  // 主要报错位置的列号，等于 range.begin.col。
    std::string message; // 给用户阅读的中文说明，例如“变量尚未声明”。
    std::string code;    // 稳定的诊断代码，例如 SEM_UNDECLARED，用于分类和检索。
    SourceRange range;  // 主要报错范围，供编辑器高亮或诊断报告定位。
    std::vector<SourceRange> related; // 关联位置，例如发生重复声明时的上一次声明。
};

// 判断诊断列表中是否存在错误或致命错误；仅有警告时返回 false。
inline bool has_errors(const std::vector<Diagnostic>& diagnostics) {
    for (const auto& d : diagnostics) {
        if (d.level == Level::Error || d.level == Level::Fatal) return true;
    }
    return false;
}

// C 类型的种类；Unknown 表示未确定，Error 表示语义检查已判定非法。
enum class TypeKind {
    Unknown, Error, Void, Char, Short, Int, Long, Float, Double, LongDouble,
    Pointer, Array, Function, Struct, Union, Enum, Named
};

struct TypeInfo;
// 共享的只读类型描述；AST、符号表和 IR 可引用同一份类型信息。
using TypePtr = std::shared_ptr<const TypeInfo>;

// 描述一个 C 数据类型；创建并共享后不可修改，类型比较应比较内容而非指针地址。
// 只有与 kind 对应的字段有效，例如 array_length 只用于数组类型。
struct TypeInfo {
    TypeKind kind = TypeKind::Unknown; // 基本类型或组合类型的种类，如 Int、Pointer、Function。
    bool is_unsigned = false; // 当前整型是否为 unsigned；浮点、指针等类型不使用该标志。
    bool is_const = false;    // 当前这一层类型是否带 const，注意所指类型与指针本身分层保存。
    bool is_volatile = false; // 当前这一层类型是否带 volatile 限定符。
    TypePtr base; // 指针：所指类型；数组：元素类型；函数：返回类型；基本类型不使用。
    std::optional<std::size_t> array_length; // 当前数组维度的元素个数；空值表示长度尚未确定。
    std::vector<TypePtr> params; // 函数的形参类型列表，按声明顺序保存，不是实参表达式。
    bool variadic = false; // 函数是否接受可变数量的实参，例如 printf 的省略号参数。
    bool has_prototype = true; // 是否有参数原型；C 中 f() 为 false，f(void) 为 true。
    std::string name; // struct/union/enum 标签名，或尚未解析的 typedef 名字。
    RecordId record_id = invalid_id; // 语义解析后的记录类型编号；未解析时为无效编号。
};

// 字面量解码后的值；monostate 表示未解码或不是常量。
// 分别保存有符号整数、无符号整数、浮点和字符串，避免大整数转成 double 后丢失精度。
using ConstantValue = std::variant<std::monostate, std::int64_t,
                                   std::uint64_t, double, std::string>;

// AST 的语法节点种类，例如函数定义、赋值、调用、二元运算。
enum class NodeType {
    Program, FunctionDecl, FunctionDef, ParamDecl, VarDecl, StructDef,
    MemberDecl, Block, ExprStmt, If, While, For, Return, Empty,
    Identifier, IntLiteral, FloatLiteral, CharLiteral, StringLiteral,
    Assign, BinaryOp, UnaryOp, Call, ImplicitCast,
    // M2 及后续阶段的节点；孩子顺序见 docs/interface.md。
    ArrayAccess, MemberAccess, Break, Continue, Switch, Case, Default,
    DoWhile, Conditional, Cast, Sizeof, UnionDef, EnumDef, EnumMember,
    TypedefDecl, InitList, Label, Goto, Error
};

// 表达式类别：非表达式/未标注、左值、右值、函数；用于赋值与取地址合法性检查。
enum class ValueCategory { None, LValue, RValue, Function };
// 声明中的存储类别；None 表示源码未显式指定，具体含义由声明所处环境决定。
enum class StorageClass { None, Auto, Register, Static, Extern, Typedef };

// 抽象语法树的一个节点；语法分析建树，语义分析填入类型、作用域和符号绑定。
struct ASTNode {
    NodeType kind = NodeType::Error; // 节点的语法种类；与下面表示 C 数据类型的 type 分开。
    std::string name; // 名字、运算符、标签或字面量原文；具体含义由 kind 决定。
    ConstantValue value; // 常量解码后的值，语义阶段填写；非常量保持 monostate。
    SourceRange range;   // 该节点对应的源码范围，包含其子表达式或子语句。
    TypePtr declared_type; // 语法阶段记录的声明/转换目标类型，可能含尚未解析的标签名。
    TypePtr type; // 语义阶段确定的类型；分析前为空，出错后指向 kind=Error 的类型。
    ValueCategory category = ValueCategory::None; // 语义标注的左值、右值或函数类别。
    StorageClass storage = StorageClass::None; // 声明中显式写出的 static、extern 等类别。
    SymbolId symbol_id = invalid_id; // 该声明或引用绑定的符号编号，用来区分同名变量。
    ScopeId scope_id = invalid_id;   // 语义遍历时该节点所属的作用域编号。
    RecordId record_id = invalid_id; // 记录定义或成员访问涉及的 struct/union/enum 编号。
    std::optional<std::size_t> member_index; // 成员访问对应 members 的下标；非成员访问为空。
    // 按节点合同排列的孩子，节点独占其所有权；不得放空指针。
    // 例如 Assign=[目标, 来源]，Call=[被调表达式, 实参0, 实参1, ...]。
    std::vector<std::unique_ptr<ASTNode>> children;
};

// 程序根仍使用同一种 ASTNode，其 kind 必须为 Program，无需再定义另一套树。
using Program = ASTNode;

// 普通符号的类别：变量、形参、数组、函数、类型别名、枚举常量。
enum class SymbolKind { Variable, Parameter, Array, Function, Typedef, EnumConstant };
// 内建函数标记，用于 printf/scanf 格式检查；普通用户函数使用 None。
enum class BuiltinKind { None, Printf, Scanf };

// 普通符号表中的一条声明记录，供语义检查、IR 生成和符号表展示使用。
struct SymbolEntry {
    SymbolId id = invalid_id; // 唯一符号编号，等于该条目在 symbols 中的下标。
    std::string name; // 源码中的名字，例如 r、main；不同作用域可以有同名条目。
    SymbolKind kind = SymbolKind::Variable; // 该名字代表变量、形参、函数等哪一种符号。
    TypePtr type; // 已解析的数据类型；函数保存完整 Function 类型，包含返回类型和形参。
    ScopeId scope = invalid_id; // 声明所在的作用域编号，不是当前访问它的作用域。
    std::uint32_t line = 1; // 声明起始行号，应与 range.begin.line 一致。
    std::uint32_t col = 1;  // 声明起始列号，应与 range.begin.col 一致。
    SourceRange range;     // 声明的源码范围，用于定位和重复声明诊断。
    StorageClass storage = StorageClass::None; // 声明的存储类别，例如 Static 或 Extern。
    bool is_defined = false; // 是否已有定义；仅有函数原型或 extern 对象声明时为 false。
    BuiltinKind builtin = BuiltinKind::None; // 是否为需要专门格式检查的内建函数。
    std::optional<std::size_t> offset; // 布局阶段填写的栈帧/全局区字节偏移；空值表示未分配。
};

// 结构体或联合体的一条成员记录；成员只能在所属记录类型内部查询。
struct MemberEntry {
    std::string name; // 成员名，例如 struct Point 中的 x。
    TypePtr type;    // 成员的已解析类型，供成员访问检查和寻址使用。
    std::optional<std::size_t> offset; // 相对对象起点的字节偏移，考虑对齐；空值表示未布局。
    SourceRange range; // 成员声明的源码范围，用于诊断和展示。
};

// C 标签命名空间中的记录种类：结构体、联合体、枚举。
enum class RecordKind { Struct, Union, Enum };

// 枚举定义中的一个枚举项，例如 enum Color 中的 RED=1。
struct EnumValue {
    std::string name; // 枚举项名字。
    std::int64_t value = 0; // 语义阶段计算出的整数值，包括自动递增得到的值。
    SymbolId symbol_id = invalid_id; // 对应的普通符号编号；枚举项也进入普通名字空间。
    SourceRange range; // 枚举项声明的源码范围。
};

// 保留需求中的 StructEntry 名称；后续 union/enum 也使用它保存标签身份。
struct StructEntry {
    RecordId id = invalid_id; // 唯一记录类型编号，等于该条目在 records 中的下标。
    std::string tag; // struct/union/enum 后面的标签名；匿名定义时为空。
    RecordKind kind = RecordKind::Struct; // 判断本条目是结构体、联合体还是枚举。
    ScopeId scope = invalid_id; // 标签声明所在的作用域编号。
    bool is_complete = false; // 是否已完成定义；只有前向声明时为 false，不能按值分配对象。
    std::vector<MemberEntry> members; // struct/union 的成员表，严格按声明顺序保存。
    std::vector<EnumValue> enumerators; // enum 的枚举项列表；struct/union 不使用。
    std::optional<std::size_t> size; // 对象总字节数，包含末尾对齐填充；空值表示尚未确定。
    std::optional<std::size_t> alignment; // 对象要求的字节对齐值；空值表示尚未布局。
    SourceRange range; // 标签声明/定义的源码范围。
};

// 作用域类别：全局、函数、嵌套块；函数体最外层与形参共享函数作用域。
enum class ScopeKind { Global, Function, Block };

// 一个持久保存的作用域，同时维护普通名字和标签两套独立映射。
struct ScopeEntry {
    ScopeId id = invalid_id; // 唯一作用域编号，等于该条目在 scopes 中的下标；全局为 0。
    ScopeId parent = invalid_id; // 外层作用域编号；全局没有外层，使用 invalid_id。
    ScopeKind kind = ScopeKind::Global; // 该作用域是全局、函数还是嵌套块。
    SourceRange range; // 该作用域覆盖的源码范围，用于展示及定位光标所属作用域。
    std::unordered_map<std::string, SymbolId> symbols; // 本层普通名字到符号编号的映射。
    std::unordered_map<std::string, RecordId> tags; // 本层标签名到记录编号的映射，与普通名字独立。
};

// 符号表模块的全部持久数据；编号是容器下标，各容器只追加，不删除或重新排序。
// 退出作用域只弹查询栈，保留条目供 IR、可视化和导出访问。
struct SymbolTableData {
    std::vector<SymbolEntry> symbols; // 本次编译登记的所有普通符号，包括已离开作用域的局部量。
    std::vector<StructEntry> records; // 本次编译登记的所有 struct/union/enum 记录类型。
    std::vector<ScopeEntry> scopes;   // 所有作用域及其父子关系，供按编号查询和导出。
    std::vector<ScopeId> active_scopes; // 当前查询栈，初始化为 {0}；名字查询从栈顶向外进行。
};

// 一条四元式中间指令，保留需求约定的四个字符串字段。
// 内部名字：%s<ID> 为符号，%t<ID> 为临时量，%L<ID> 为标号；不用的字段写 "-"。
struct Quadruple {
    std::string op; // 操作码，例如 +、=、arg、call、label、jmp；具体含义见指令合同。
    std::string arg1 = "-"; // 第一个操作数；例如赋值来源、call 的被调函数或 jz 的条件。
    std::string arg2 = "-"; // 第二个操作数；call 中为实参数量；不需要时保持 "-"。
    std::string result = "-"; // 结果去向；跳转/label 中为标号；无结果时保持 "-"。
};

// IR 表达式生成的返回信息，表示“结果放在哪里”，而不是直接返回运行时的值。
struct Place {
    std::string name; // 可作为四元式操作数的名字或字面量，例如 %s3、%t2、1.0。
    TypePtr type;    // 该结果的 C 类型，用于选择运算和类型转换。
    bool is_const = false; // 是否为编译期常量操作数，供常量提升和折叠使用。
};

// IR 中一条临时变量声明，其类型不从名字或操作码猜测。
struct TemporaryEntry {
    std::string name; // 临时变量的内部名字，例如 %t2；在所属函数/初始化序列内唯一。
    TypePtr type;    // 临时变量的类型，供解释器分配存储和执行转换。
};

// 一个函数的中间代码；解释器每次调用为其局部量和临时量建立独立调用帧。
struct IRFunction {
    SymbolId symbol_id = invalid_id; // 该函数在普通符号表中的编号。
    std::vector<SymbolId> parameters; // 按形参声明顺序保存编号，每项对应 Parameter 符号。
    std::vector<TemporaryEntry> temporaries; // 本函数内生成的临时变量及其类型。
    std::vector<Quadruple> quads; // 本函数的四元式指令序列，按执行流程排列。
    std::vector<SourceRange> locations; // 每条四元式对应的源码范围，长度必须等于 quads.size()。
};

// 整个程序的中间代码，区分全局初始化与各函数，并保存执行入口。
struct IRProgram {
    std::vector<SymbolId> globals; // 需要分配全局存储的对象编号，不包含函数。
    std::vector<Quadruple> global_initializers; // 进入 main 前执行的全局初始化指令。
    std::vector<SourceRange> global_locations; // 初始化指令的源码范围，与 global_initializers 一一对应。
    std::vector<TemporaryEntry> global_temporaries; // 全局初始化序列需要的临时变量及类型。
    std::vector<IRFunction> functions; // 本程序具有可执行函数体的函数 IR 集合。
    SymbolId entry_function = invalid_id; // 入口 main 的符号编号；未找到时仍可查看 IR，但不能执行。
};

// 词法阶段的返回结果；应在阶段执行完并填写产物后查询 ok()。
struct LexResult {
    std::vector<Token> tokens; // 识别出的单词序列，末尾恰好有一个 END_OF_FILE。
    std::vector<Diagnostic> diagnostics; // 本次词法分析产生的诊断列表。
    // 没有错误/致命错误则为 true；仅有警告不会使词法阶段失败。
    bool ok() const { return !has_errors(diagnostics); }
};

// 语法阶段的返回结果；root 独占整棵 AST 的所有权。
struct ParseResult {
    std::unique_ptr<ASTNode> root; // Program 根节点；只要有语法错误，恢复完成后也必须置空。
    std::vector<Diagnostic> diagnostics; // 本次语法分析及错误恢复产生的诊断列表。
    // 必须既有有效根节点，又没有错误/致命错误，才允许进入语义分析。
    bool ok() const { return root && !has_errors(diagnostics); }
};

// 语义阶段的返回结果；AST 的类型/符号标注直接写回传入的树，不在这里复制。
struct SemanticResult {
    SymbolTableData symbols; // 语义检查登记的持久符号、记录类型和作用域数据。
    std::vector<Diagnostic> diagnostics; // 声明、引用、类型、调用等语义检查产生的诊断。
    // 没有错误/致命错误才允许生成 IR；警告仍可保留在报告中。
    bool ok() const { return !has_errors(diagnostics); }
};

// 中间代码生成阶段的返回结果，交给展示、优化或四元式解释器。
struct IRResult {
    IRProgram program; // 已生成的全局初始化、函数四元式、临时量类型和入口信息。
    std::vector<Diagnostic> diagnostics; // IR 生成阶段发现的问题，例如不支持的节点或缺失标注。
    // 没有错误/致命错误才允许把中间代码交给解释器。
    bool ok() const { return !has_errors(diagnostics); }
};

// 本头文件只定义共用数据；各模块函数见对应头文件。
// 主程序可包含 minic/modules.hpp，一次引入全部模块接口。

} // minic 命名空间
