# 演示与答辩准备

系统头文件和完整标准库按用户决定暂不扩展。演示使用项目内建 printf/scanf，以及本地头文件，不依赖系统库、动态内存或 C++ 功能。

后续已增加[图形工作台](workbench.md)：可在编辑器补全名字、查看并导出 AST 图，以及关闭/开启常量折叠对比 IR 和执行结果。本文终端演示仍可使用；涉及“尚未提供图形或优化入口”的旧范围现在以工作台说明为准。

## 演示前准备

构建和完整验收：

```powershell
cmake -S . -B build/cmake -DCMAKE_CXX_FLAGS=-Werror
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

在项目根目录一次跑完演示，需 Python 3.10+：

```powershell
python tools/run_showcase.py
# 编译器放在其他位置时
python tools/run_showcase.py --compiler 'build/frontend/minic.exe'
```

脚本检查 11 项结果，包含正常程序和预期失败的错误演示；全部正确才返回 0。结果保存到 `build/showcase/`，其中 `.txt` 是阶段结果，`.diagnostics.txt` 是错误信息，`summary.txt` 是执行摘要。生成的文本为 UTF-8，适合在编辑器打开、复制到报告或截图，不需要提交到 GitHub。

手动演示只需要 minic，不需要 Python。PowerShell 要写出可执行程序路径；当前目录不是项目根目录时，请使用 `& "minic.exe 的完整路径" run "源码的完整路径"`。源程序是类 C 语言，不能拿使用 vector、iostream 的 C++ 程序代替。

## 建议演示顺序（约 8 分钟）

| 顺序 | 演示内容 | 操作与结果 | 讲解重点 |
|---|---|---|---|
| 1（1 分钟） | 基本源码 | 打开 `examples/showcase/01_pipeline.c` | 求 1 到 n 的和，包含变量、函数、循环与输入输出 |
| 2（2 分钟） | 各编译阶段 | 依次运行 tokens、parse、symbols、check、ir | Token 保存原文和位置；AST 表示结构；语义绑定名字和类型；四元式表示运算和控制流 |
| 3（1 分钟） | 实际执行 | 运行 run，输入 5，输出 `sum = 15` | 最终执行的是四元式，输入输出由解释器内建函数处理 |
| 4（1 分钟） | 数组、记录、宏与指针 | 运行 `02_records_macros.c` | 宏先展开，头文件从源码目录查找，结构体数组传入指针参数 |
| 5（1 分钟） | 静态变量与回调 | 运行 `03_callback_static.c` | 静态局部变量跨调用保留；函数指针间接调用 |
| 6（2 分钟） | 两种错误 | 运行 `04_semantic_error.c`、`05_runtime_error.c` | 未声明名字在语义阶段阻止执行；越界在运行阶段报告中文诊断 |

阶段演示命令：

```powershell
.\build\cmake\minic.exe tokens --raw examples/showcase/01_pipeline.c
.\build\cmake\minic.exe parse examples/showcase/01_pipeline.c
.\build\cmake\minic.exe symbols examples/showcase/01_pipeline.c
.\build\cmake\minic.exe check examples/showcase/01_pipeline.c
.\build\cmake\minic.exe ir examples/showcase/01_pipeline.c
.\build\cmake\minic.exe run examples/showcase/01_pipeline.c
```

最后一条运行后输入 `5` 并回车。先前保存的结果可直接在 `build/showcase/` 查看，不要在输入等待时误以为程序卡住。tokens 使用 --raw 是为了展示原始列号，其他命令默认预处理后只保证原始文件和行号。

第二、第三个程序的完整预期输出：

```text
Alice: 90
Bob: 55
Chen: 80
passed = 2
average = 75.000000
```

```text
counter = 1 2
callback = 12
```

两份错误程序返回 1 是演示成功的预期现象：语义错误代码为 SEM_UNDECLARED，指向第 3 行；数组越界为运行错误，指向第 5 行。默认预处理模式列号为 1，不能声称这就是错误表达式的精确列号。错误程序不会输出后续的提示字符串。

## 答辩时怎样解释

**项目做了什么？** 使用 C++17 实现一个可展示内部过程的类 C 编译器，源码依次经过预处理、词法、语法、语义和四元式生成，再由解释器执行。当前不是完整 C 标准实现，也不生成机器码。

**词法分析怎么做？** 正则规则是唯一规则源，生成工具先建立 NFA，再用子集构造得到 DFA，最后最小化并生成转移表。扫描器逐字节查表，记住最后接受位置，实现最长匹配；识别完整标识符后再判断关键字。

**语法分析怎么做？** 正式实现采用手写递归下降，表达式按优先级构造树。typedef 有独立的轻量名字作用域，标签需要看后一个冒号；不能把正式解析器介绍成对原始 Token 流的纯 LL(1) 表驱动分析器。资料中的 BNF 和预测表用于设计与教学说明，范围不完全等于实现。

**语义分析是什么方法？** 遍历 AST，结合符号表和类型规则检查声明、表达式、赋值、函数实参、返回值、成员访问等。计算结果回写节点的类型、符号编号和作用域，必要时插入隐式转换。它利用语法树结构执行语义规则，是语法制导思想的一种实现，但语义检查是独立的 AST 遍历阶段，不是在每次语法归约时生成最终代码。

**为什么有几个表？** 普通名字保存变量、参数、函数、typedef 和枚举常量；每层作用域有普通名字索引和类型标签索引；记录表保存 struct/union/enum，记录内部保存成员或枚举项；常量池按类型和值去重。goto 标签由函数内的语义环境检查，不和类型标签表混为一谈。函数和变量通过条目类别区分，不必分别复制成两张相同的表。

**哪些函数建表？** SymbolTable 构造函数建立全局作用域；enter_scope 建立函数或块作用域；insert、declare_object 登记普通符号；declare_record 登记记录标签，complete_record 补齐成员和布局；register_builtins 登记 printf/scanf。常量池通过 intern 登记或复用常量。

**四元式与常量池怎么配合？** 四元式保存 `(op, arg1, arg2, result)`，`%s` 引用符号，`%t` 引用临时变量，`%c` 引用常量池。运算、赋值、函数调用和跳转都有对应指令；不能把某个样例的指令数量当成接口固定要求。

**程序怎么运行？** 解释器按四元式执行；函数调用有独立调用帧，保存参数、局部对象和临时值。对象地址记录所属存储与调用帧，便于检查空指针、越界和返回后失效的局部地址。还检查整数运算、对象大小和执行步数等边界。

**有哪些优化？** 已有无副作用常量表达式的折叠；不会为了折叠去执行函数调用、赋值或变量读取。工作台已提供关闭/开启常量折叠的并排 IR 和独立执行对比，不应声称已经实现完整优化器。

**哪些限制要主动说明？** 控制体要加花括号；块内先声明后写语句；for 的变量提前声明；显式数组长度是正整数字面量。系统头文件与完整标准库本轮跳过，完整 C89/C99 和机器码输出仍未交付。编辑器补全和图形工作台已提供，但其补全恢复和头文件能力有明确范围，见 workbench.md。语言范围以 frontend.md、backend-completion.md 为准。

## 本轮验证

新增示例通过正式命令行的 11 项结果检查，已加入 CTest。补充 8 项真实源码边界检查：返回局部地址后解引用、尾后指针解引用、负数组下标、跨数组指针相减、非法移位、有符号溢出、超过 16 MiB 的数组、通过 const 指针写入。原源码清单及预期保持不变。

当前共 17 项 CTest，命令行验收包含 34 项；可选 GCC 参考另外检查 8 个正常综合程序。首次验收记录的 26/34 项计数是当时的结果，当前计数增加来自新增边界检查。
