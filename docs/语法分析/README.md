# 语法分析

使用递归下降和表达式优先级构造 AST，语法错误时返回诊断。

公开入口：`parse(tokens)`。

| 内容 | 文件 |
|---|---|
| 实现 | [parser.cpp](../../src/语法分析/parser.cpp) |
| 公共接口 | [parser.hpp](../../include/minic/parser.hpp) |
| 文法与集合 | [当前文法](../../实验设计/语法分析/语法表格/README.md) |
| AST 约定 | [数据结构与 AST](../公共约定/数据结构与AST约定.md) |

验证：`frontend_test.cpp`、`parser_tables_current`、`parser_grammar` 和源码联调测试。

[返回文档导航](../README.md)
