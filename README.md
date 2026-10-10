# AAACompilerDevelopment
软件实训大作业
我是好人

## 项目目标与当前进度

使用 **C++17** 实现可展示内部过程的类 C89 项目语言编译器：词法分析 → 语法分析 → 语义分析 → 四元式生成 → 解释器执行。符号表与统一诊断服务各阶段，最终词法规则采用 **C89** 范围，并保留项目要求的 `//` 注释扩展。

用户已决定暂不扩展系统头文件和完整标准库，当前优先验证已有功能与准备课堂演示。内建 printf/scanf 和本地头文件继续可用；一键演示与答辩说明见 [演示准备](docs/showcase.md)。

本地[图形工作台](docs/workbench.md)现已提供源码编辑与真实作用域补全、可缩放导出的 AST 图、各阶段表格、优化前后独立执行与结果对比。支持 Ctrl+N 新建 .c/.h 文件、Ctrl+S 保存，新文件切换前必须保存或放弃。构建后运行 `python tools/workbench_server.py --open`，或双击 `start-workbench.cmd`。

已完成公共结构体、模块接口、最终词法正则、常量池、符号表、统一诊断、M1/M2 后续流程，以及对象指针、数组传参、typedef、enum、goto、位运算、静态存储和安全常量折叠。本轮进一步实现 union、short/long/unsigned/double、函数指针回调、extern 对象声明、聚合函数传值与返回、字符串指针输入输出，以及独立预处理和总控入口。**正式词法与语法现已接入，可以直接完成“源码 → Token → AST → 语义 → 四元式 → 执行”，并可通过 compile_preprocessed 使用宏和头文件。** 前端采用项目语言范围，C89 仅作参考；控制体必须有花括号，具体范围见 [词法与语法实现](docs/frontend.md)。 完整 C89、跨文件链接、动态内存和完整标准库仍未完成，见 [本轮交付范围](docs/backend-completion.md)。

| 已完成内容 | 入口 |
|---|---|
| 编辑器补全、AST 图形与优化前后对比 | [docs/workbench.md](docs/workbench.md)、[web](web)、[include/minic/completion.hpp](include/minic/completion.hpp) |
| 课堂演示、阶段结果保存和答辩说明 | [docs/showcase.md](docs/showcase.md)、[examples/showcase](examples/showcase)、[tools/run_showcase.py](tools/run_showcase.py) |
| 命令行自动预处理、本地头文件与原始源码模式 | [docs/command-line.md](docs/command-line.md)、[tools/test_cli_preprocessing.py](tools/test_cli_preprocessing.py) |
| 整体验收结果、命令行与综合程序复验 | [docs/acceptance.md](docs/acceptance.md)、[tools/test_acceptance.py](tools/test_acceptance.py) |
| 词法 DFA 扫描器、递归下降解析器及真实源码测试 | [src/lexer.cpp](src/lexer.cpp)、[src/parser.cpp](src/parser.cpp)、[docs/frontend.md](docs/frontend.md)、[tests/frontend_test.cpp](tests/frontend_test.cpp) |
| 公共结构体，全部成员附中文注释 | [include/minic/interface.hpp](include/minic/interface.hpp) |
| 五模块、符号表、诊断、解释器、总控及展示接口 | [include/minic/modules.hpp](include/minic/modules.hpp) |
| 数据与 AST 孩子顺序约定 | [docs/interface.md](docs/interface.md) |
| 函数参数、返回值、失败行为和调用顺序 | [docs/modules.md](docs/modules.md) |
| 最终 C89 正则：32 关键字、64 普通规则、2 头文件上下文规则 | [lexer/patterns.json](lexer/patterns.json) |
| 正则中文说明、最长匹配及预处理约定 | [docs/lexer-regex.md](docs/lexer-regex.md) |
| 正则验证工具，17 组检查 | [tools/check_lexer_patterns.py](tools/check_lexer_patterns.py) |
| 可运行的结构体组装示例 | [examples/interface_demo.cpp](examples/interface_demo.cpp) |
| 可运行的真实源码总控示例 | [examples/compile_and_run.cpp](examples/compile_and_run.cpp) |
| 常量池：按类型和值去重、稳定编号与四元式常量操作数 | [src/constant_pool.cpp](src/constant_pool.cpp)、[docs/constant-pool.md](docs/constant-pool.md) |
| 符号表：作用域、普通名字、标签、成员、函数声明、内建函数与补全 | [src/symbol_table.cpp](src/symbol_table.cpp)、[docs/symbol-table.md](docs/symbol-table.md) |
| 统一诊断：中文消息、行列、关联位置、错误数量限制 | [src/diagnostic.cpp](src/diagnostic.cpp) |
| 类型规则：类型比较、扩展数值算术提升及赋值兼容 | [src/type_rules.cpp](src/type_rules.cpp) |
| 符号表运行示例与行为测试 | [examples/symbol_table_demo.cpp](examples/symbol_table_demo.cpp)、[tests/symbol_table_test.cpp](tests/symbol_table_test.cpp) |
| 常量池行为测试 | [tests/constant_pool_test.cpp](tests/constant_pool_test.cpp) |
| M1 语义检查：类型/左值、声明、函数调用、返回与格式串 | [src/semantic.cpp](src/semantic.cpp) |
| 四元式生成：运算、短路、分支、循环与函数调用 | [src/ir.cpp](src/ir.cpp) |
| 解释执行：独立调用帧、递归、输入输出与运行错误检查 | [src/interpreter.cpp](src/interpreter.cpp) |
| Token、AST、符号表、常量池、四元式和诊断展示 | [src/display.cpp](src/display.cpp) |
| 编译总控与命令行：真实源码入口 | [src/compiler.cpp](src/compiler.cpp)、[src/main.cpp](src/main.cpp) |
| M1 联动示例、9 组测试及总控流程测试 | [examples/m1_pipeline_demo.cpp](examples/m1_pipeline_demo.cpp)、[tests/m1_pipeline_test.cpp](tests/m1_pipeline_test.cpp)、[tests/compiler_flow_test.cpp](tests/compiler_flow_test.cpp) |
| M2 数组/结构体/控制流与指针等扩展说明 | [docs/backend-extensions.md](docs/backend-extensions.md) |
| M2 示例与 7 组行为测试 | [examples/m2_pipeline_demo.cpp](examples/m2_pipeline_demo.cpp)、[tests/m2_pipeline_test.cpp](tests/m2_pipeline_test.cpp) |
| 指针、别名、枚举、标签、位运算和静态存储的 9 组测试 | [tests/m3_pipeline_test.cpp](tests/m3_pipeline_test.cpp) |
| 扩展类型、union、函数指针、extern、字符串与聚合调用的 7 组测试 | [tests/m4_pipeline_test.cpp](tests/m4_pipeline_test.cpp) |
| 独立预处理、宏、条件表达式与包含，以及原始行号映射 | [include/minic/preprocessor.hpp](include/minic/preprocessor.hpp)、[docs/preprocessor.md](docs/preprocessor.md) |
| 宏字符串化/拼接/单词重扫描、形参限定符兼容、临时聚合数组成员读取 | [docs/backend-completion.md](docs/backend-completion.md) |
| 支持范围、构建方法与给同学的 AST 交接说明 | [docs/m1-backend.md](docs/m1-backend.md) |
| 构建配置与独立测试脚本 | [CMakeLists.txt](CMakeLists.txt)、[tools/test_backend.ps1](tools/test_backend.ps1) |
| 原有四元式教学演示，不使用本次公共接口 | [ir_demo.cpp](ir_demo.cpp) |
| 32 个真实 C 联调样例、输入输出预期和错误阶段 | [tests/integration/README.md](tests/integration/README.md)、[tests/integration/manifest.json](tests/integration/manifest.json) |
| AST 对照、接入顺序与自动验收入口 | [docs/integration-handoff.md](docs/integration-handoff.md)、[tools/test_integration.py](tools/test_integration.py) |

## 验证方式

需要 Python 3.10+ 和支持 C++17 的 g++。正则验证工具只验证规则，不能替代自研 DFA 扫描器。

```powershell
python tools/check_lexer_patterns.py
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I include -fsyntax-only examples/compile_and_run.cpp
New-Item -ItemType Directory -Force build | Out-Null
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I include examples/interface_demo.cpp src/constant_pool.cpp src/type_arena.cpp -o build/interface_demo.exe
& build/interface_demo.exe
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I include tests/constant_pool_test.cpp src/constant_pool.cpp src/type_arena.cpp -o build/constant_pool_test.exe
& build/constant_pool_test.exe
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I include tests/symbol_table_test.cpp src/symbol_table.cpp src/diagnostic.cpp src/type_rules.cpp src/type_arena.cpp -o build/symbol_table_test.exe
& build/symbol_table_test.exe
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I include examples/symbol_table_demo.cpp src/symbol_table.cpp src/diagnostic.cpp src/type_rules.cpp src/type_arena.cpp -o build/symbol_table_demo.exe
& build/symbol_table_demo.exe
```

compile_and_run.cpp 已能链接并运行真实源码编译流程；执行示例输出 `c = 6.283180`。interface_demo.cpp 需链接常量池实现，其输出演示数据组装；symbol_table_demo.cpp 展示作用域遮蔽、前缀查询及重复声明报错。

基础行为测试分别验证常量池和符号表/诊断/类型规则，扩展类型及 struct/union 布局已接入后续流程；补全目前按单源文件字节位置处理。详细范围见上述模块文档。头文件和实现中的说明采用简短中文注释。

新增 M1 后续模块可直接使用统一脚本严格编译并测试：

```powershell
& tools/test_backend.ps1
# 编译器不在 PATH 时：
& tools/test_backend.ps1 -Compiler 'D:/G++/MinGW/bin/g++.exe'
```

脚本严格编译并运行基础测试、M1–M4、预处理、编译总控、真实前端测试、词法随机对照及示例。M1 示例输入 5 输出 `sum = 15`，M2 示例输出 `S = 9`。总控测试中的词法/语法替身只在测试程序中链接，不属于正式编译器实现。

安装 CMake 后也可以使用：

```powershell
cmake -S . -B build/cmake
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

验证使用严格 g++ 脚本；CMake/CTest 加入图形工作台回归后共 20 项测试。命令行验收包含 34 项，演示回归包含 11 项；工作台服务含 34 项检查，并通过桌面和手机尺寸的浏览器验收。minic 命令行和 compile_and_run 已建立并实际运行。前端测试覆盖真实源码编译执行，词法对照验证 3396 组输入及起止位置。Windows 中文路径建议使用 `cmake -S . -B build/frontend -G Ninja`，具体命令见 [frontend.md](docs/frontend.md)。当前支持范围、限制、节点顺序、目标布局、指针边界和新四元式见 [后续模块交接说明](docs/backend-extensions.md) 和 [本轮交付](docs/backend-completion.md)。TypePtr 已改为普通指针 const TypeInfo*，CompilationResult.types 统一保存和释放类型对象；独立阶段调用的生命周期约定见 [公共接口](docs/interface.md)。

## 语言目标与协作

新增 source_integration 使用正式前端执行 32 个夹具，逐一检查输出、返回值和最早失败阶段；integration_preparation 继续独立核对预处理和词法规则。

M1：基本类型、函数、控制流与 scanf/printf；M2：数组、struct 和更多控制语句；M3：指针、预处理及扩展类型；M4 原规划：扩展核心语法、存储类别、限定符和标准库对接。词法规则可先识别完整种别，语法与执行能力按里程碑实现。目标是项目定义的类 C89 语言，C89 用于参考，不默认追求完整标准符合性；词法保留已确认的 C89 拼写规则与 // 注释扩展。

采用 Git Flow：main 为稳定分支，dev 为集成分支，feature/模块名用于开发；提交信息使用“类型(模块): 描述”。接口字段或合同变化应更新文档及版本，经组内评审后合入。

教师资料、早期 Word/PPT、需求分析原稿与过程渲染产物保留在本地，仓库保存上述已完成的代码、接口、规则、测试及配套说明。
