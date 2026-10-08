# AAACompilerDevelopment
软件实训大作业
我是好人

## 项目目标与当前进度

使用 **C++17** 实现可展示内部过程的 C 语言编译器：词法分析 → 语法分析 → 语义分析 → 四元式生成 → 解释器执行。符号表与统一诊断服务各阶段，最终词法规则采用 **C89** 范围，并保留项目要求的 `//` 注释扩展。

本次上传已完成的公共结构体、模块接口、最终词法正则及配套文档和示例。**当前是接口与规则基线，尚未实现完整编译器；NFA/DFA 生成器、正式扫描器和其余模块函数体仍待开发。**

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
| 原有四元式教学演示，不使用本次公共接口 | [ir_demo.cpp](ir_demo.cpp) |

## 验证方式

需要 Python 3.10+ 和支持 C++17 的 g++。正则验证工具只验证规则，不能替代自研 DFA 扫描器。

```powershell
python tools/check_lexer_patterns.py
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I include -fsyntax-only examples/compile_and_run.cpp
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I include examples/interface_demo.cpp -o interface_demo.exe
.\interface_demo.exe
```

接口声明尚无实现，不应将 compile_and_run.cpp 链接为已完成的编译器。interface_demo.cpp 可以独立编译运行；其输出演示数据组装，不代表完成了编译流程。

## 语言目标与协作

M1：基本类型、函数、控制流与 scanf/printf；M2：数组、struct 和更多控制语句；M3：指针、预处理及扩展类型；M4：完整 C89 核心、存储类别、限定符和标准库对接。词法规则可先识别完整种别，语法与执行能力按里程碑实现。原需求中的 C89/C99 验收口径需组内统一，此仓库最终词法规范明确使用 C89。

采用 Git Flow：main 为稳定分支，dev 为集成分支，feature/模块名用于开发；提交信息使用“类型(模块): 描述”。接口字段或合同变化应更新文档及版本，经组内评审后合入。

教师资料、早期 Word/PPT、需求分析原稿与过程渲染产物保留在本地，本次仓库上传上述已完成的软件定义及配套说明。
