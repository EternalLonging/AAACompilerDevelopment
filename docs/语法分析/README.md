# 语法分析

使用递归下降和表达式优先级构造 AST，语法错误时返回诊断。

公开入口：`parse(tokens)`。

| 内容 | 文件 |
|---|---|
| 实现 | [parser.cpp](../../src/语法分析/parser.cpp) |
| 公共接口 | [parser.hpp](../../include/minic/parser.hpp) |

- [语法实现](语法实现.md)
- [AST 孩子顺序](../公共约定/数据结构与AST约定.md)
- [历史 LL(1) 草案](../../实验设计/语法分析/历史参考/README.md)

验证：frontend_test.cpp，以及 tests/integration 中的语法拒绝用例。

[返回文档导航](../README.md)
