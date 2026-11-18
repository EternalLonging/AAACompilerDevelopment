# 语法分析

使用状态栈和规范 LR(1) ACTION/GOTO 表识别语法，语义值栈在归约时直接构造 AST，遇到第一条语法错误就停止，返回一条诊断和空根节点。

公开入口：`parse(tokens)`。

| 内容 | 文件 |
|---|---|
| 实现 | [parser.cpp](../../src/语法分析/parser.cpp) |
| 运行分析表 | [lr_table.hpp](../../src/语法分析/lr_table.hpp) |
| 算法与阅读顺序 | [语法实现](语法实现.md) |
| LR 表生成器 | [generate_lr_tables.py](../../tools/generate_lr_tables.py) |
| LL 对照材料 | [原预测表](../../src/语法分析/predictive_table.hpp) |
| 公共接口 | [parser.hpp](../../include/minic/parser.hpp) |
| 文法与集合 | [当前文法](../../实验设计/语法分析/语法表格/README.md) |
| AST 约定 | [数据结构与 AST](../公共约定/数据结构与AST约定.md) |

验证：`frontend_test.cpp`、`lr_tables_current`、`parser_grammar` 和源码联调测试。

[返回文档导航](../README.md)
