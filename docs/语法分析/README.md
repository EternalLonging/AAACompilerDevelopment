# 语法分析

使用分析栈和 LL(1) 预测分析表识别语法，语义值栈直接构造 AST，遇到第一条语法错误就停止，返回一条诊断和空根节点。

公开入口：`parse(tokens)`。

| 内容 | 文件 |
|---|---|
| 实现 | [parser.cpp](../../src/语法分析/parser.cpp) |
| 运行预测表 | [predictive_table.hpp](../../src/语法分析/predictive_table.hpp) |
| 算法与阅读顺序 | [语法实现](语法实现.md) |
| 文法唯一来源 | [generate_parser_tables.py](../../tools/generate_parser_tables.py) |
| 公共接口 | [parser.hpp](../../include/minic/parser.hpp) |
| 文法与集合 | [当前文法](../../实验设计/语法分析/语法表格/README.md) |
| AST 约定 | [数据结构与 AST](../公共约定/数据结构与AST约定.md) |

验证：`frontend_test.cpp`、`parser_tables_current`、`parser_grammar` 和源码联调测试。

[返回文档导航](../README.md)
