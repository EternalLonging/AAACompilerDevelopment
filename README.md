# AAACompilerDevelopment · 教学编译器

使用 C++17 实现 MiniC：源码 → 词法分析 → 语法分析 → 语义分析 → 中间代码生成 → 解释执行。符号表管理为语义检查、四元式和结果展示提供统一数据。

## 按五部分查看

| 部分 | 源码 | 开发说明 | 主要职责 |
|---|---|---|---|
| 词法分析 | [src/词法分析](src/词法分析/) | [词法说明](docs/词法分析/README.md) | 正则规则、DFA、Token 和词法诊断 |
| 语法分析 | [src/语法分析](src/语法分析/) | [语法说明](docs/语法分析/README.md) | LL(1) 预测分析表、分析栈、AST |
| 语义分析 | [src/语义分析](src/语义分析/) | [语义说明](docs/语义分析/README.md) | 类型、左值、声明、调用和控制语句检查 |
| 中间代码生成 | [src/中间代码生成](src/中间代码生成/) | [四元式说明](docs/中间代码生成/README.md) | 四元式、常量池、常量折叠和解释执行 |
| 符号表管理 | [src/符号表管理](src/符号表管理/) | [符号表说明](docs/符号表管理/README.md) | 作用域、名字、符号编号、结构体成员 |

词法 DFA 由 `src/词法分析/词法规则.json` 自动生成，规则和生成结果保持一致。

## 构建和运行

需要 C++17 编译器、CMake、Ninja 和 Python 3.10+。在项目根目录执行；Windows 的中文路径建议使用 Ninja。

```powershell
cmake -S . -B build/ninja -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/ninja
ctest --test-dir build/ninja --output-on-failure
build/ninja/minic.exe run examples/showcase/01_pipeline.c
python tools/workbench_server.py --open
```

也可双击 `start-workbench.cmd`。构建配置、严格编译及验收方式见[构建与验收](docs/使用指南/构建与验收.md)，操作方法见[图形工作台](docs/使用指南/图形工作台.md)和[命令行](docs/使用指南/命令行.md)。Linux/macOS 的可执行文件不带 `.exe`。

## 当前支持范围

- `int`、`float`、`char`；函数返回值和空参数列表可用 `void`。
- 变量、赋值、一维数组、具名简单 `struct`、函数定义和直接调用。
- `if/else`、`while`、`for`、`break/continue` 和 `return`。
- 内建 `printf/scanf` 使用 `%d`、`%f`、`%c`、`%s` 和 `%%`。
- 同块先声明后执行，循环变量提前声明，控制体使用花括号。

详细语言范围以[语言支持范围](docs/公共约定/语言支持范围.md)为准；LL(1) 文法表用于说明当前语法。

## 其他目录

| 目录 | 用途 |
|---|---|
| [include/minic](include/minic/) | 各部分的公共接口，保留原头文件名称 |
| [src/公共支持](src/公共支持/) | 统一诊断与内部共用规则 |
| [src/总控与展示](src/总控与展示/) | 编译流程、命令行和结果展示 |
| [docs](docs/README.md) | 五部分开发说明、公共约定和使用指南 |
| [实验设计](实验设计/README.md) | 当前词法图与语法表格 |
| [tests](tests/README.md) | 按五部分索引的测试及源码联调用例 |
| [examples](examples/README.md) | 模块示例、源码演示与课堂综合程序 |
| [tools](tools/README.md) | 生成、校验、验收和工作台工具 |
| web | 图形工作台页面 |

[公共数据与 AST 约定](docs/公共约定/数据结构与AST约定.md) · [模块接口](docs/公共约定/模块接口.md) · [源码联调](docs/公共约定/源码联调说明.md) · [整理说明](docs/公共约定/仓库整理说明.md)
<img width="2364" height="171" alt="image" src="https://github.com/user-attachments/assets/bcf3c651-91fe-48b6-84a1-d9417602ba2f" />

