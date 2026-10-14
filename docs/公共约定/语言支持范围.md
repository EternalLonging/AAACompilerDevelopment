# 教学语言范围

## 当前语言范围（2026-10-12）

- 基础类型只有 `int`、`float`、`char`；`void` 只用于函数返回值或空参数列表。
- 支持变量声明和赋值、一维数组、具名简单 `struct`、函数定义和直接调用。
- 支持 `if/else`、`while`、`for`、`break/continue`、`return`、`const`、`printf/scanf`。
- `for` 的变量在循环前声明；同一代码块必须先声明、后写语句；控制体必须使用花括号。
- `scanf` 的 `&变量`、`&数组下标` 和 `&结构体成员` 只作为输入参数的内部寻址，不提供源语言指针。
- 保留 `+ - * / %`、比较和 `&& || !`；赋值只用 `=`，循环更新写成 `i=i+1`。
- struct 必须具名并先定义；支持成员、成员数组和对象赋值，不支持前向声明、按值传参和返回。
- 输入输出格式只用 `%d`、`%f`、`%c`、`%s`、`%%`；字符串输入超出数组容量会报错。
- 不支持头文件、宏、指针、union、enum、typedef、存储类别、volatile、do-while、switch/case/default、goto、标签、位运算、三目、强制转换、sizeof、复合赋值和多维数组。

## 拒绝发生在哪个阶段

| 写法 | 阶段 / 诊断 |
|---|---|
| `#define`、`#if`、`#include` 等指令 | 词法 / LEX_INVALID_CHAR |
| static、extern、auto、register、typedef | 语法 / PARSE_DECLARATION_REMOVED |
| do-while、switch/case/default、goto | 语法 / PARSE_CONTROL_REMOVED |
| 语句标签 `done:` | 语法 / PARSE_LABEL_REMOVED |
| union、enum 声明或类型名 | 语法 / PARSE_RECORD_REMOVED |
| 指针声明、指针转换、解引用、`->` | 语法 / PARSE_POINTER_REMOVED |
| short、long、double、signed、unsigned、volatile | 语法 / PARSE_TYPE_REMOVED |
| 函数声明、函数原型 | 语法 / PARSE_FUNCTION_DECL_REMOVED |
| 多维数组 | 语法 / PARSE_ARRAY_DIMENSION |
| 复合赋值、自增自减、三目、强制转换、sizeof | 语法 / PARSE_OPERATOR_REMOVED 或 PARSE_COMPOUND_ASSIGNMENT |
| 匿名 struct、struct 前向声明 | 语法 / PARSE_RECORD_ANONYMOUS、PARSE_RECORD_FORWARD |
| struct 按值传参、返回或用户变参函数 | 语法 / PARSE_FUNCTION_REMOVED |
| `for(int i=0;...)` | 语法 / PARSE_FOR_DECLARATION |
| 同一块执行语句后声明变量 | 语法 / PARSE_DECLARATION_ORDER |
| 普通表达式 `&x` | 语义 / SEM_ADDRESS |

词法规则仍识别一部分 C 关键字及运算符，以便后续阶段拒绝超出范围的写法；Token 种别不代表该功能可用。实际范围以本文、公共头文件和回归测试为准。

`union` 和 `enum` 仍作为保留字识别，只用于给出明确的拒绝诊断；对应的 AST 节点、类型和专用符号表数据已删除。字符串和注释中的 `#` 不受影响。C++ 实现中的 `enum class` 用来区分节点、阶段等类别，继续保留。

当前变量只区分全局变量、局部变量和形参，没有存储类别字段或 typedef 类型别名。普通局部变量在每次函数调用时重新创建；全局变量在程序开始时初始化。结构体标签 `struct S` 保留，已删除的是用于 goto 的语句标签。四元式仍使用内部跳转标号实现 if 和循环。

公共接口的旧枚举值暂时保留，以保持同学负责的 AST 对接和孩子顺序；它们不是源码支持列表。自动生成的 ImplicitCast 仍用于 int/float 的转换和数组传参，不能删除它，否则普通浮点运算和函数调用会出错。
