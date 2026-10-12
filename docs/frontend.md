# 词法与语法分析实现

日期：2026-10-12。正式入口为 `lex(source, filename)` 和 `parse(tokens)`，实现位于 `src/frontend.cpp`。现已接入 `compile → analyze → generate → run`，命令行可以直接读取项目语言源文件。

当前前端按最小 MiniC 范围工作，限制见 [language-scope.md](language-scope.md)。超出范围的关键字仍可被词法识别，但会在语法阶段报告中文错误。

## 词法分析

`lexer/patterns.json` 是唯一规则源。`tools/generate_lexer_dfa.py` 展开规则中的正则定义引用（不是源语言的 #define），用 Thompson NFA、子集构造和保留接受动作的最小化生成 `src/lexer_dfa.hpp`。当前普通 DFA 为 90 状态、47 输入字节类别，覆盖全部 256 字节；32 个关键字在完整 ID 识别后查表。生成表已提交，正常 C++ 构建不需要运行 Python。

扫描持续推进并记录最后接受状态；最长匹配优先，同长按规则顺序。空白和注释跳过，非法数字、转义、未闭合字面量/注释和非法字节使用既有中文诊断代码。错误恢复后仍输出一个末尾 EOF，20 条错误后停止；此时 EOF 位于停止扫描的位置。位置为左闭右开字节范围，CRLF 换行一次，Tab 算一列，UTF-8 列按字节计算。


## 语法分析

使用递归下降构造 AST，表达式使用优先级攀升，保留左结合算术和右结合赋值。括号、调用、数组下标和`.` 按既有孩子顺序生成；指针相关写法会被拒绝。

解析器按类型关键字识别声明，不维护 typedef 分类环境。语句标签遇到 ID 后的冒号时明确拒绝；结构体标签保留。赋值左部先检查一元表达式的语法形状，再由语义检查左值性质，例如 `a+b=1` 是语法错误，`(a+b)=1` 到语义阶段报告不可赋值。

| 已支持语法 | AST 与类型行为 |
|---|---|
| 基本类型、限定符 | void、char、int、float、const；冲突组合报错 |
| 声明、函数 | 变量声明、初始化、函数定义、形参和空参数列表；函数声明与原型被拒绝 |
| 数组声明 | 一维数组和数组形参；多维数组、指针声明和函数指针被拒绝 |
| 结构体 | 具名 struct 定义和成员访问；匿名定义、union、enum 被拒绝 |
| 表达式 | 加减乘除取余、比较、逻辑、赋值、调用、下标和成员访问 |
| 控制流 | if/else、while、for、return、循环中的 break/continue |
| 初始化 | 嵌套初始化列表、外层数组长度省略、窄字符串；相邻字符串保留各段原文，由语义逐段解码后拼接 |

所有 if、else、while、for 控制体必须有花括号。`else if` 必须写成 `else { if (...) { ... } }`。同一块先写声明再写语句，语句后的声明报告 PARSE_DECLARATION_ORDER；for 初始化只接收表达式，变量在循环前声明。数组已写出的界限必须是正整数字面量；内层缺失界限由语义拒绝，外层可由初始化推导。

记录标签须先定义；完整记录可声明对象。不允许按值包含自身，也不提供自身指针；成员声明内的记录定义、形参类型内的记录定义和转换类型内的记录定义暂不支持，应提前单独定义。

语法错误在顶层和块内按分号/花括号恢复，最多 20 条错误；任何语法错误都使根节点为空。递归或 AST 高度超过 128 层产生致命诊断。输入必须有且只有一个末尾 EOF。解析阶段只填写源码声明类型和语法结构，值解码、持久符号表、类型标注和隐式转换由语义负责。

## 与设计草案的关系

目标是项目定义的类 C89 语言，C89 用于参考。已上传的 267 条上下文核心 BNF、FIRST/FOLLOW/SELECT 和预测表是设计草案；正式解析器采用本文范围，没有机械实现旧式函数、隐式 int、位域、常量表达式数组界限、指定初始化或完整 C89。它也不是对原始 Token 流的纯 LL(1) 表驱动分析器：声明符和表达式有专门解析逻辑。

源码只支持直接函数定义和调用。`printf/scanf` 是内建教学函数；格式串只使用项目已实现的基本格式。语言范围与执行限制继续见 [backend-completion.md](backend-completion.md)。

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

当前完整回归共 18 项 CTest，词法对照、46 个源码联调用例及新增精简范围检查纳入验收。源码示例输出 `sum = 15`，compile_and_run 输出 `c = 6.283180`。
