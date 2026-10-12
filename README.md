# AAACompilerDevelopment
软件实训大作业
我是好人

## 项目目标与当前进度

使用 C++17 实现最小 MiniC 编译器：词法分析 → 语法分析 → 语义分析 → 四元式生成 → 解释器执行。符号表、常量池和统一诊断为这些阶段提供数据。

## 当前语言范围（2026-10-12）

- 只保留 `int`、`float`、`char`，以及函数返回值使用的 `void`。
- 支持变量声明和赋值、一维数组、具名简单 `struct`、函数定义和调用。
- 支持 `if/else`、`while`、`for`、`break/continue`、`return`、`const`。
- `printf/scanf` 只支持 `%d`、`%f`、`%c`、`%s` 和 `%%`；`scanf("%d", &x)` 的取地址只用于输入参数。
- 不支持头文件、宏、指针、联合体、枚举、类型别名、存储类别、位运算、三目、强制转换、`sizeof`、`++/--`、复合赋值和多维数组。
- `for` 的变量在循环前声明；同一个代码块先声明变量再写语句；控制体必须使用花括号。

## 代码按阶段查看

| 阶段 | 数据与接口 | 实现 |
|---|---|---|
| 公共数据结构 | [interface.hpp](include/minic/interface.hpp) | [接口约定](docs/interface.md) |
| 词法 | Token、LexResult、lex | src/frontend.cpp、src/lexer_dfa.hpp |
| 语法 | ASTNode、ParseResult、parse | src/frontend.cpp |
| 语义 | TypeInfo、SemanticResult、analyze | src/semantic.cpp |
| 符号表 | SymbolTable、SymbolTableData | src/semantic.cpp；[建表说明](docs/symbol-table.md) |
| 中间代码 | Quadruple、IRProgram、generate | src/backend.cpp；[常量池](docs/constant-pool.md) |
| 执行 | RunResult、run | src/backend.cpp |


正式模块接口见 [modules.md](docs/modules.md)，AST 孩子顺序见 [interface.md](docs/interface.md)。
## 构建和运行

需要 C++17 编译器、CMake 和 Python 3.10+。选择本机已安装的构建生成器：

```powershell
cmake -S . -B build/cmake -G "MinGW Makefiles"
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
build/cmake/minic.exe run examples/showcase/01_pipeline.c
python tools/workbench_server.py --open
```

也可双击 `start-workbench.cmd`。工作台保留 Ctrl+N 新建 `.c`、Ctrl+S 保存、保存或放弃新文件后切换、括号和引号配对、Tab 缩进、语法树导出和优化对比；[操作说明](docs/workbench.md)。


[语言范围](docs/language-scope.md) · [前端说明](docs/frontend.md) · [命令行](docs/command-line.md) · [演示准备](docs/showcase.md) · [联调用例](tests/integration/README.md)

<img width="1188" height="89" alt="image" src="https://github.com/user-attachments/assets/43b5046a-3ec6-4bd2-881e-b5622ff722fd" />
