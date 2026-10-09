# M1 语义分析、四元式与解释执行

日期：2026-10-09。词法和语法分析由其他同学负责。本次完成后续模块，通过手动组装符合公共合同的 AST，独立运行 `analyze → generate → run`。正式入口没有伪造词法/语法结果，也未修改原有公共结构体字段。

## 1. 已完成什么

| 模块 | 文件 | 实际行为 |
|---|---|---|
| 语义分析 | `src/semantic.cpp` | 按源码顺序建表、标注类型/作用域/符号、检查声明、左值、运算、初始化、函数调用和返回 |
| IR 生成 | `src/ir.cpp` | 共享常量池、按函数分组、数值运算/转换、短路跳转、分支循环、break/continue、实参快照与调用 |
| 解释执行 | `src/interpreter.cpp` | 全局零初始化、局部声明重置、独立调用帧、递归、printf/scanf、步数/深度限制、运行报错 |
| 文本展示 | `src/display.cpp` | Token 种别、AST 树、符号/标签/成员/作用域、常量池与四元式、中文诊断 |
| 总控 | `src/compiler.cpp`、`src/compilation_result.cpp` | 按目标执行阶段，失败停止，保存各阶段产物和诊断；等待词法/语法链接 |
| 命令行 | `src/main.cpp` | 读取文件，支持 tokens/parse/symbols/check/ir/run，交付词法/语法后可链接使用 |
| 构建与测试 | `CMakeLists.txt`、`tools/test_backend.ps1` | 独立后续模块库、示例和测试；词法/语法交付后可建立完整流程目标 |

## 2. 当前支持范围

- 类型：M1 的有符号 char、int、float，以及函数返回 void。数字字面量支持十进制/八进制/十六进制整数、十进制浮点和科学计数法；浮点可带 f/F，其他数字类型后缀暂不支持。
- 窄字面量：标准单字符转义、最多三位八进制转义和十六进制字节转义。字符常量必须恰好一个可表示的字节，宽字符/宽字符串、多字符常量和相邻字符串拼接尚不支持。
- 声明：全局数值变量、局部变量、具名形参、函数原型和定义。同层对象冲突报错，内部块可遮蔽外层；形参与最外层函数体共用作用域。局部变量允许 Auto/Register，static/extern/typedef 的完整语义仍未实现。
- 运算：`+ - * / %`、比较、`&& || !`、数值显式转换、`=`/`+= -= *= /=`、前后置自增自减。赋值只允许相同类型及 char→int、char/int→float；缩窄需显式 Cast。
- 控制流：if/else、while、for、return；额外支持循环内 break/continue。for 固定四个孩子，缺失分量用 Empty；允许 for 初始化声明并创建本层作用域，这是项目便于演示的扩展。
- 函数：按声明顺序可见，兼容原型与后续定义共用编号，支持递归和嵌套调用。调用尚未声明的后置函数需要先提供原型；仅声明而未定义的被调普通函数会报错。M1 将空参数 `f()` 归一化为无参原型，完整 C 的未指定参数规则留待后续。
- 返回：非 void 函数要求能结构化证明所有路径返回；循环不自动视为必然返回，必要时在末尾添加 return。main 执行入口要求无参 int main，不自动补 return 0。
- 输入输出：格式必须为窄字符串字面量。支持 `%d`、`%f`、`%c`、`%%`，printf 额外支持 `%s` 字符串字面量。scanf 只允许对应数值变量的 `&` 实参，不支持 scanf `%s`、宽度、精度、长度修饰及动态格式串。

未支持的 AST 节点会明确报告 SEM_UNSUPPORTED。数组、struct/union/enum 的完整语义和寻址、通用指针、switch/do-while、goto、完整 C89 转换/链接及标准库仍属于 M2–M4 工作。符号表本身的标签和成员接口已有独立实现，不等于这些语法已经接入 M1。

## 3. 类型与执行约定

本组 M1 解释器采用 32 位有符号 int、8 位有符号 char、32 位 float，内部用 int64/double 保存值但每次存储检查范围和 float 舍入。整数运算溢出、除零、浮点非有限结果、未初始化对象读取和输入失败都返回运行诊断。整数除法向零截断，取余与之对应；float 的 printf `%f` 默认显示六位小数。

普通未初始化的全局数值对象先置零，之后按源码顺序执行常量初始化序列。全局初始化只接受无副作用的数值常量表达式，暂不进行编译期常量求值/折叠，其除零等错误由解释器在全局初始化执行时检测；局部声明生成 `local` 指令，进入本次声明时将对象重新置为未初始化，再执行可选初始化。包括循环反复进入的块，每次都重新执行这一规则。

语义检查把提升表达式包成 ImplicitCast；比较结果是 int，但浮点比较的两个操作数保持浮点类型。IR 用跳转实现逻辑短路。函数实参按本组约定从左到右求值并快照，外层连续 arg/call 之前先完成所有嵌套调用；普通二元表达式也按左到右求值并保存左侧值。此顺序是解释器约定，不代表 C 标准规定了该顺序。

每次 run 都建立独立全局存储和显式调用栈，局部量/临时量属于各自调用帧。max_steps 包含全局初始化、标号和 main；max_call_depth 包含 main；0 表示不限制对应项。默认限制仍为原接口的 1,000,000 步与 1024 层。执行中失败保留已经写出的输出，exit_code 为空；main 返回非零数本身不表示解释失败。

run 会先检查整份 IR 的入口、常量、符号/临时量、参数、操作码、标号、位置表和基本类型结构，失败时 executed_steps 为 0。运行时再检查值是否初始化、调用队列、格式串、地址归属、算术与输入输出错误。IR 和符号表须来自同次成功语义分析；这一检查不代替完整 C 语义证明。

## 4. 交给词法/语法同学的合同

1. 实现 `lex(source, filename)` 和 `parse(tokens)`，保持头文件签名。建议交付文件名为 `src/lexer.cpp` 和 `src/parser.cpp`。
2. 根节点必须是 Program；所有孩子只存 children，禁止空指针；节点孩子顺序按 [interface.md](interface.md)。FunctionDef 最后一个孩子是函数体 Block，前面是 ParamDecl。
3. 函数 declared_type 为 Function，base 存返回类型，params 与 ParamDecl 的 declared_type 顺序和类型一致。零形参不放一个 void ParamDecl。
4. Call 的第一个孩子是被调表达式，之后才是实参；UnaryOp 的前后置自增名称为 pre++/post++，自减为 pre--/post--。
5. 字面量 name 保留源码拼写、引号和转义。M1 语义阶段自行解码并填写 value。节点初始 type、symbol_id、scope_id 保持未标注，不要自行填语义结果或插入 ImplicitCast。
6. 正确填写 range.file 和起止字节位置；标识符在其声明后才能使用。参数和函数最外层 Block 不要由解析器额外操作符号表。
7. 词法 tokens 末尾必须是 EOF；语法有 Error/Fatal 时 root 为空。总控会调用 ok() 决定是否进入下一阶段。

## 5. 运行与验证

Windows 且已有支持 C++17 的 g++ 时，运行：

```powershell
& tools/test_backend.ps1
# 编译器不在 PATH 时可指定：
& tools/test_backend.ps1 -Compiler 'D:/G++/MinGW/bin/g++.exe'
```

脚本严格编译各后续模块，运行常量池测试、符号表测试、9 组后续模块联动测试、总控流程测试，以及输入 5 输出 sum=15 的独立示例。生成文件位于 build/，不属于源码交付。

安装 CMake 后也可使用：

本机未安装 CMake，本次实际编译和行为验证使用上述 g++ 脚本；CMake 配置尚未在本机执行验证。

```powershell
cmake -S . -B build/cmake
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

CMake 当前无需 lexer/parser 即可构建 minic_backend 和全部后续模块测试。两个源文件交付后重新配置，即可生成 minic_compiler、minic 命令行与 compile_and_run 目标；若同学将模块拆成多个实现文件，需要同步扩展该目标的源文件列表。接入后命令示例为 `minic run circle.c`；当前只能检查命令行源码的编译兼容性，尚不能对真实 C 文件验证这些命令。

`tests/compiler_flow_test.cpp` 的 lex/parse 是**仅在测试可执行文件中链接的替身**，用于验证总控阶段顺序及错误传播。它们不属于正式编译器，也不代表完成了词法/语法。生产库 minic_backend 不包含替身。

联动测试覆盖求和输入输出、阶乘递归、短路跳过除零/副作用、嵌套调用实参快照、float/char/字符串、全局变量、void 返回、同名遮蔽、break/continue、未初始化变量与循环局部重置、类型/声明/格式错误、运行溢出、执行上限、坏 IR，以及展示和阶段结果的成功条件。
