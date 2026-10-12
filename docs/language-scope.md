# 教学语言范围

## 当前语言范围（2026-10-12）

- 保留数组、struct、const、函数、if/else、while、for、return，以及循环中的 break/continue。
- 删除源语言宏预处理（`#define`、`#if` 等）、头文件包含、union 和 enum；常量可以写成 `const int N=5;`。
- static、extern、auto、register、typedef、do-while、switch/case/default、goto 和语句标签已删除。
- volatile 和编辑器自动补全已删除；括号配对和自动缩进保留。

- `for` 的变量在循环前声明，例如 `int i; for(i=0;i<3;i++){...}`；`for(int i=...)` 会报错。
- 同一个代码块先声明变量，再写执行语句。语句后可以进入新的 `{...}`，在新块开头声明变量。
- 不支持指针变量、指针转换、解引用、`->` 和函数指针调用。
- `scanf("%d", &x)`、数组元素或成员的输入地址保留；普通表达式不能取地址。位运算 `a & b` 保留。
- `TypeKind::Address` 仅供数组传参和输入输出的内部寻址；不能在源码中声明它。C++ 实现中的普通指针负责连接数据结构，与源语言指针功能不同。

## 拒绝发生在哪个阶段

| 写法 | 阶段 / 诊断 |
|---|---|
| `#define`、`#if`、`#include` 等指令 | 词法 / LEX_INVALID_CHAR |
| static、extern、auto、register、typedef | 语法 / PARSE_DECLARATION_REMOVED |
| do-while、switch/case/default、goto | 语法 / PARSE_CONTROL_REMOVED |
| 语句标签 `done:` | 语法 / PARSE_LABEL_REMOVED |
| union、enum 声明或类型名 | 语法 / PARSE_RECORD_REMOVED |
| 指针声明、指针转换、解引用、`->` | 语法 / PARSE_POINTER_REMOVED |
| `volatile int x;` | 语法 / 声明语法错误 |
| `for(int i=0;...)` | 语法 / PARSE_FOR_DECLARATION |
| 同一块执行语句后声明变量 | 语法 / PARSE_DECLARATION_ORDER |
| 普通表达式 `&x` | 语义 / SEM_ADDRESS |

词法规则仍识别 C89 关键字及运算符，以便后续阶段拒绝超出范围的写法；Token 种别不代表该功能可用。旧 BNF 和原始教学资料保留，实际范围以本文、公共头文件和回归测试为准。

`union` 和 `enum` 仍作为保留字识别，只用于给出明确的拒绝诊断；对应的 AST 节点、类型和专用符号表数据已删除。字符串和注释中的 `#` 不受影响。C++ 实现中的 `enum class` 用来区分节点、阶段等类别，继续保留。

当前变量只区分全局变量、局部变量和形参，没有存储类别字段或 typedef 类型别名。普通局部变量在每次函数调用时重新创建；全局变量在程序开始时初始化。结构体标签 `struct S` 保留，已删除的是用于 goto 的语句标签。四元式仍使用内部跳转标号实现 if 和循环。
