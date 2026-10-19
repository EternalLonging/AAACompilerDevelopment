# 符号表管理

管理作用域、名字、符号编号、记录和成员；供语义、IR 和展示共用。

公开入口：`SymbolTable / register_builtins`。

| 内容 | 文件 |
|---|---|
| 实现 | [symbol_table.cpp](../../src/符号表管理/symbol_table.cpp) |
| 公共接口 | [symbol_table.hpp](../../include/minic/symbol_table.hpp) |

- [符号表使用说明](符号表说明.md)
- [符号表管理表](../../实验设计/符号表管理/符号表格/符号表管理表.xlsx)
- [模块接口](../公共约定/模块接口.md)

验证：symbol_table_test.cpp；examples/symbol_table_demo.cpp 展示遮蔽和重复声明。

[返回文档导航](../README.md)
