# 示例导航

| 所属部分 | 示例 | 用途 |
|---|---|---|
| 词法、语法、语义分析 | [frontend_demo.c](frontend_demo.c)、[compile_and_run.cpp](compile_and_run.cpp) | 展示真实源码和阶段接口调用 |
| 符号表管理 | [symbol_table_demo.cpp](symbol_table_demo.cpp) | 展示作用域、同名遮蔽和重复声明 |
| 中间代码生成 | [m1_pipeline_demo.cpp](m1_pipeline_demo.cpp)、[m2_pipeline_demo.cpp](m2_pipeline_demo.cpp) | 展示语义到四元式和解释执行 |
| 公共数据 | [interface_demo.cpp](interface_demo.cpp) | 手工组装公共数据，展示数据结构合同 |
| 五部分综合演示 | [showcase](showcase/) | 源码、各阶段产物、诊断和运行结果 |

手工组装示例可能包含公共类型系统的内部构造，当前源语言范围以[语言支持范围](../docs/公共约定/语言支持范围.md)为准。[课堂演示](../docs/使用指南/课堂演示.md)给出课堂准备方法。
