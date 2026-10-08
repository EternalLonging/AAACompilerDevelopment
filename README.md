# AAACompilerDevelopment
软件实训大作业
我是好人

## 项目目标与当前进度

使用 **C++17** 实现可展示内部过程的 C 语言编译器：词法分析 → 语法分析 → 语义分析 → 四元式生成 → 解释器执行。符号表与统一诊断服务各阶段，最终词法规则采用 **C89** 范围，并保留项目要求的 `//` 注释扩展。

已完成公共结构体、模块接口、最终词法正则，并实现常量池、符号表管理、统一诊断和基础类型规则。**这些支持模块可以独立运行和测试；完整 C 源程序编译流程尚未实现，NFA/DFA 生成器、正式扫描器、语法分析、完整语义遍历、IR 和解释器入口仍待开发。**

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

compile_and_run.cpp 调用的完整编译入口尚无实现，目前只做声明层编译检查。interface_demo.cpp 需链接常量池实现，其输出演示数据组装；symbol_table_demo.cpp 展示作用域遮蔽、前缀查询及重复声明报错。

两个行为测试分别验证常量池和符号表/诊断/类型规则。类型转换目前限 M1 的有符号 char、int、float；结构体布局由语义模块计算后交给符号表校验；补全目前按单源文件字节位置处理。详细范围见上述模块文档。头文件和实现中的说明采用简短中文注释。

## 语言目标与协作

M1：基本类型、函数、控制流与 scanf/printf；M2：数组、struct 和更多控制语句；M3：指针、预处理及扩展类型；M4：完整 C89 核心、存储类别、限定符和标准库对接。词法规则可先识别完整种别，语法与执行能力按里程碑实现。原需求中的 C89/C99 验收口径需组内统一，此仓库最终词法规范明确使用 C89。

采用 Git Flow：main 为稳定分支，dev 为集成分支，feature/模块名用于开发；提交信息使用“类型(模块): 描述”。接口字段或合同变化应更新文档及版本，经组内评审后合入。

教师资料、早期 Word/PPT、需求分析原稿与过程渲染产物保留在本地，仓库保存上述已完成的代码、接口、规则、测试及配套说明。
