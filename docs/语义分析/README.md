# 语义分析

检查声明、类型、左值、函数调用和控制语句；绑定符号并标注 AST。

公开入口：`analyze(root, arena)`。

| 内容 | 文件 |
|---|---|
| 实现 | [analyzer.cpp](../../src/语义分析/analyzer.cpp)、[type_rules.cpp](../../src/语义分析/type_rules.cpp)、[type_arena.cpp](../../src/语义分析/type_arena.cpp) |
| 公共接口 | [semantic.hpp](../../include/minic/semantic.hpp)、[type_arena.hpp](../../include/minic/type_arena.hpp) |

- [语义实现](语义实现.md)
- [语言范围](../公共约定/语言支持范围.md)
- [存储与寻址](../中间代码生成/存储与寻址.md)

验证：type_arena_test.cpp、m1_pipeline_test.cpp 至 m4_pipeline_test.cpp，以及源码联调的语义诊断用例。

[返回文档导航](../README.md)
