# 中间代码生成

遍历标注后的 AST，生成带常量池的函数四元式；解释器执行产物。

公开入口：`generate(root, symbols)`。

| 内容 | 文件 |
|---|---|
| 实现 | [ir.cpp](../../src/中间代码生成/ir.cpp)、[constant_pool.cpp](../../src/中间代码生成/constant_pool.cpp)、[interpreter.cpp](../../src/中间代码生成/interpreter.cpp) |
| 公共接口 | [ir.hpp](../../include/minic/ir.hpp)、[constant_pool.hpp](../../include/minic/constant_pool.hpp)、[interpreter.hpp](../../include/minic/interpreter.hpp) |

- [四元式实现](四元式实现.md)
- [常量池](常量池.md)
- [存储与寻址](存储与寻址.md)

验证：constant_pool_test.cpp、m1_pipeline_test.cpp 至 m4_pipeline_test.cpp、workbench_test.cpp 和 source_integration。

[返回文档导航](../README.md)
