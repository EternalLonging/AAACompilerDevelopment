# AAACompilerDevelopment
软件实训大作业
我是好人

## 项目目标与当前进度

使用 C++17 实现教学语言编译器：词法分析 → 语法分析 → 语义分析 → 四元式生成 → 解释器执行。符号表、常量池和统一诊断为这些阶段提供数据。词法采用 C89 规则，并支持项目要求的 `//` 注释。

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
