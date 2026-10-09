# 词法与语法分析实现

日期：2026-10-09。正式入口为 `lex(source, filename)` 和 `parse(tokens)`，实现位于 `src/lexer.cpp`、`src/parser.cpp`。现已接入 `compile → analyze → generate → run`，命令行可以直接读取项目语言源文件。

## 词法分析

`lexer/patterns.json` 是唯一规则源。`tools/generate_lexer_dfa.py` 展开正则宏，用 Thompson NFA、子集构造和保留接受动作的最小化生成 `src/lexer_dfa.hpp`。当前普通 DFA 为 92 状态、48 输入字节类别，覆盖全部 256 字节；32 个关键字在完整 ID 识别后查表。生成表已提交，正常 C++ 构建不需要运行 Python。

扫描持续推进并记录最后接受状态；最长匹配优先，同长按规则顺序。空白和注释跳过，非法数字、转义、未闭合字面量/注释和非法字节使用既有中文诊断代码。错误恢复后仍输出一个末尾 EOF，20 条错误后停止；此时 EOF 位于停止扫描的位置。位置为左闭右开字节范围，CRLF 换行一次，Tab 算一列，UTF-8 列按字节计算。

头文件上下文不进入普通 DFA。`lex` 和 `compile` 接收直接源码，不执行续行、宏或头文件处理；这些功能由已有 `preprocess` / `compile_preprocessed` 负责。minic 命令行默认先预处理，使用 `tokens --raw` 可查看原始单词与列号，见 [command-line.md](command-line.md)。预处理只保证原始文件和行号映射，列号及字节偏移的限制见 [preprocessor.md](preprocessor.md)。

## 语法分析

使用递归下降构造 AST，表达式使用优先级攀升，保留左结合算术和右结合赋值、条件运算。括号、调用、数组下标、`.`/`->`、前后置自增自减、转换和 sizeof 按既有孩子顺序生成；`->` 显式生成解引用再访问成员。

解析器有独立的普通名字作用域，用于区分 typedef 类型名和变量名。声明符名字、形参及枚举项可遮蔽外层 typedef；离开块后恢复。标签通过 ID 后的冒号识别。赋值左部先检查一元表达式的语法形状，再由语义检查左值性质，例如 `a+b=1` 是语法错误，`(a+b)=1` 到语义阶段报告不可赋值。

| 已支持语法 | AST 与类型行为 |
|---|---|
| 基本类型、存储类别、限定符 | void、char、short、int、long、float、double、long double、signed/unsigned、const/volatile、static/extern/auto/register/typedef；冲突组合报错 |
| 声明、函数 | 多声明符、初始化、函数原型与定义、具名/无名原型形参、空参数列表、void 参数列表 |
| 复杂声明符 | 多层指针、指针限定符、多维数组、数组指针、函数指针、函数形参；类型逐层保存，不用指针层数代替 |
| 记录类型、别名 | struct/union 定义和显式前向声明、enum、typedef；匿名定义使用不可与源码名字冲突的内部标签 |
| 表达式 | 算术、比较、逻辑、位运算、移位、赋值与复合赋值、逗号、三目、调用、下标、成员、转换、sizeof |
| 控制流 | if/else、while、for、do-while、switch/case/default、return、break/continue、标签/goto |
| 初始化 | 嵌套初始化列表、外层数组长度省略、窄字符串；相邻字符串保留各段原文，由语义逐段解码后拼接 |

所有 if、else、while、for、do、switch 控制体必须有花括号。`else if` 必须写成 `else { if (...) { ... } }`。同一块先写声明再写语句，语句后的声明报告 PARSE_DECLARATION_ORDER；for 初始化只接收表达式，变量在循环前声明。数组已写出的界限必须是正整数字面量；内层缺失界限由语义拒绝，外层可由初始化推导。

记录标签须先声明或定义；例如先写 `struct Node;` 再声明 `struct Node *p;`。记录成员中可引用已有记录或自身指针；成员声明内的记录定义、形参类型内的记录定义和转换类型内的记录定义暂不支持，应提前单独定义。

语法错误在顶层和块内按分号/花括号恢复，最多 20 条错误；任何语法错误都使根节点为空。递归或 AST 高度超过 128 层产生致命诊断。输入必须有且只有一个末尾 EOF。解析阶段只填写源码声明类型和语法结构，值解码、持久符号表、类型标注和隐式转换由语义负责。

## 与设计草案的关系

目标是项目定义的类 C89 语言，C89 用于参考。已上传的 267 条上下文核心 BNF、FIRST/FOLLOW/SELECT 和预测表是设计草案；正式解析器采用本文范围，没有机械实现旧式函数、隐式 int、位域、常量表达式数组界限、指定初始化或完整 C89。它也不是对原始 Token 流的纯 LL(1) 表驱动分析器：typedef 使用上下文环境，标签使用两 Token 识别，声明符和表达式有专门解析逻辑。

省略号函数原型可以构造 Function.variadic；用户变参函数的语义和执行仍未实现。宽字面量和多字符常量按词法规则接受，现有语义模型会明确拒绝。语言范围与完整标准库、链接等限制继续见 [backend-completion.md](backend-completion.md)。

## 构建与验证

```powershell
cmake -S . -B build/frontend -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-Werror
cmake --build build/frontend
ctest --test-dir build/frontend --output-on-failure
build/frontend/minic.exe tokens examples/frontend_demo.c
build/frontend/minic.exe parse examples/frontend_demo.c
build/frontend/minic.exe run examples/frontend_demo.c
python tools/generate_lexer_dfa.py --check
```

需要支持 C++17 的编译器；上述生成器选择需安装 Ninja。Windows 含中文路径时建议 Ninja，MinGW Makefiles 在本环境无法处理该路径。也可运行 `tools/test_backend.ps1` 严格编译后端及前端。

`tests/frontend_test.cpp` 验证真实源码、AST 合同、作用域、位置、错误恢复、前端失败阻止后续阶段，以及语义、IR、执行联动；`tools/test_lexer_dfa.py` 将 C++ 扫描器与独立逐规则 Python 字节正则对照，包含全部单字节、关键字边界、固定符号及 3000 组固定种子随机输入。`lexer_dfa_current` 检查已提交表与当前规则一致。`compiler_flow_test` 的替身仍只用于隔离检查总控顺序，不进入生产库。

本次实测：严格 C++17 构建通过；181 项前端断言、3396 组词法正则对照、32 个源码联调用例和 14 项 CTest 检查全部通过。源码示例输出 `sum = 15`，compile_and_run 输出 `c = 6.283180`。
