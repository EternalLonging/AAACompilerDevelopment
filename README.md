# AAACompilerDevelopment
软件实训大作业
我是好人

## 项目目标与当前进度

使用 **C++17** 实现可展示内部过程的 C 语言编译器：词法分析 → 语法分析 → 语义分析 → 四元式生成 → 解释器执行。符号表与统一诊断服务各阶段，最终词法规则采用 **C89** 范围，并保留项目要求的 `//` 注释扩展。

已完成公共结构体、模块接口、最终词法正则、常量池、符号表和统一诊断；现已实现 M1 语义检查、四元式生成、解释执行、结果展示、总控和命令行入口。**目前可以独立运行“手动 AST → 语义 → 四元式 → 执行”；词法和语法由其他同学交付，接入前还不能直接编译 C 源文件。** M2–M4 的数组、记录寻址、通用指针及完整 C 规则仍待扩展。

| 已完成内容 | 入口 |
|---|---|
| 公共结构体，全部成员附中文注释 | [include/minic/interface.hpp](include/minic/interface.hpp) |
| 五模块、符号表、诊断、解释器、总控及展示接口 | [include/minic/modules.hpp](include/minic/modules.hpp) |
| 数据与 AST 孩子顺序约定 | [docs/interface.md](docs/interface.md) |
| 函数参数、返回值、失败行为和调用顺序 | [docs/modules.md](docs/modules.md) |
| 最终 C89 正则：32 关键字、64 普通规则、2 头文件上下文规则 | [lexer/patterns.json](lexer/patterns.json) |
| 正则中文说明、最长匹配及预处理约定 | [docs/lexer-regex.md](docs/lexer-regex.md) |
| 正则验证工具，17 组检查 | [tools/check_lexer_patterns.py](tools/check_lexer_patterns.py) |
| 可运行的结构体组装示例 | [examples/interface_demo.cpp](examples/interface_demo.cpp) |
| 模块实现后的总控调用示例，目前只能编译检查 | [examples/compile_and_run.cpp](examples/compile_and_run.cpp) |
| 常量池：按类型和值去重、稳定编号与四元式常量操作数 | [src/constant_pool.cpp](src/constant_pool.cpp)、[docs/constant-pool.md](docs/constant-pool.md) |
| 符号表：作用域、普通名字、标签、成员、函数声明、内建函数与补全 | [src/symbol_table.cpp](src/symbol_table.cpp)、[docs/symbol-table.md](docs/symbol-table.md) |
| 统一诊断：中文消息、行列、关联位置、错误数量限制 | [src/diagnostic.cpp](src/diagnostic.cpp) |
| 类型规则：类型比较、M1 算术提升及赋值兼容 | [src/type_rules.cpp](src/type_rules.cpp) |
| 符号表运行示例与行为测试 | [examples/symbol_table_demo.cpp](examples/symbol_table_demo.cpp)、[tests/symbol_table_test.cpp](tests/symbol_table_test.cpp) |
| 常量池行为测试 | [tests/constant_pool_test.cpp](tests/constant_pool_test.cpp) |
| M1 语义检查：类型/左值、声明、函数调用、返回与格式串 | [src/semantic.cpp](src/semantic.cpp) |
| 四元式生成：运算、短路、分支、循环与函数调用 | [src/ir.cpp](src/ir.cpp) |
| 解释执行：独立调用帧、递归、输入输出与运行错误检查 | [src/interpreter.cpp](src/interpreter.cpp) |
| Token、AST、符号表、常量池、四元式和诊断展示 | [src/display.cpp](src/display.cpp) |
| 编译总控与命令行：等待词法/语法链接 | [src/compiler.cpp](src/compiler.cpp)、[src/main.cpp](src/main.cpp) |
| M1 联动示例、9 组测试及总控流程测试 | [examples/m1_pipeline_demo.cpp](examples/m1_pipeline_demo.cpp)、[tests/m1_pipeline_test.cpp](tests/m1_pipeline_test.cpp)、[tests/compiler_flow_test.cpp](tests/compiler_flow_test.cpp) |
| 支持范围、构建方法与给同学的 AST 交接说明 | [docs/m1-backend.md](docs/m1-backend.md) |
| 构建配置与独立测试脚本 | [CMakeLists.txt](CMakeLists.txt)、[tools/test_backend.ps1](tools/test_backend.ps1) |
| 原有四元式教学演示，不使用本次公共接口 | [ir_demo.cpp](ir_demo.cpp) |

## 验证方式

需要 Python 3.10+ 和支持 C++17 的 g++。正则验证工具只验证规则，不能替代自研 DFA 扫描器。

```powershell
python tools/check_lexer_patterns.py
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I include -fsyntax-only examples/compile_and_run.cpp
New-Item -ItemType Directory -Force build | Out-Null
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I include examples/interface_demo.cpp src/constant_pool.cpp -o build/interface_demo.exe
& build/interface_demo.exe
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I include tests/constant_pool_test.cpp src/constant_pool.cpp -o build/constant_pool_test.exe
& build/constant_pool_test.exe
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I include tests/symbol_table_test.cpp src/symbol_table.cpp src/diagnostic.cpp src/type_rules.cpp -o build/symbol_table_test.exe
& build/symbol_table_test.exe
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I include examples/symbol_table_demo.cpp src/symbol_table.cpp src/diagnostic.cpp src/type_rules.cpp -o build/symbol_table_demo.exe
& build/symbol_table_demo.exe
```

compile_and_run.cpp 的总控已有实现，但仍需要词法/语法函数才能链接完整流程，目前可做声明层编译检查。interface_demo.cpp 需链接常量池实现，其输出演示数据组装；symbol_table_demo.cpp 展示作用域遮蔽、前缀查询及重复声明报错。

两个行为测试分别验证常量池和符号表/诊断/类型规则。类型转换目前限 M1 的有符号 char、int、float；结构体布局由语义模块计算后交给符号表校验；补全目前按单源文件字节位置处理。详细范围见上述模块文档。头文件和实现中的说明采用简短中文注释。

新增 M1 后续模块可直接使用统一脚本严格编译并测试：

```powershell
& tools/test_backend.ps1
# 编译器不在 PATH 时：
& tools/test_backend.ps1 -Compiler 'D:/G++/MinGW/bin/g++.exe'
```

脚本运行原有测试、9 组语义→IR→执行联动测试、编译总控测试及求和示例。示例输入 5，输出 `sum = 15`。总控测试中的词法/语法替身只在测试程序中链接，不属于正式编译器实现。

安装 CMake 后也可以使用：

```powershell
cmake -S . -B build/cmake
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

本次实际验证使用 g++ 脚本，CMake 配置尚未在本机执行。`src/lexer.cpp`、`src/parser.cpp` 交付后重新配置 CMake，即可建立完整 minic 命令行和 compile_and_run 示例。节点结构、孩子顺序、函数签名和字面量约定见 [M1 交接说明](docs/m1-backend.md)。

## 语言目标与协作

M1：基本类型、函数、控制流与 scanf/printf；M2：数组、struct 和更多控制语句；M3：指针、预处理及扩展类型；M4：完整 C89 核心、存储类别、限定符和标准库对接。词法规则可先识别完整种别，语法与执行能力按里程碑实现。原需求中的 C89/C99 验收口径需组内统一，此仓库最终词法规范明确使用 C89。

采用 Git Flow：main 为稳定分支，dev 为集成分支，feature/模块名用于开发；提交信息使用“类型(模块): 描述”。接口字段或合同变化应更新文档及版本，经组内评审后合入。

教师资料、早期 Word/PPT、需求分析原稿与过程渲染产物保留在本地，仓库保存上述已完成的代码、接口、规则、测试及配套说明。
