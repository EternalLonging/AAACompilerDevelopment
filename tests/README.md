# 测试导航

| 部分 | 入口 | 检查内容 |
|---|---|---|
| 词法分析 | frontend_test.cpp、lexer_probe.cpp 与词法工具 | Token、位置、恢复及独立正则对照 |
| 语法分析 | frontend_test.cpp、[语法资料核对](../tools/generate_parser_tables.py)、[集合与文法回归](../tools/test_parser_tables.py) | AST 顺序、优先级、拒绝范围、表格一致性和独立文法覆盖 |
| 语义分析 | type_arena_test.cpp、arena_lifetime_test.cpp、m1_pipeline_test.cpp 至 m4_pipeline_test.cpp | 类型归属、声明、类型和控制语句检查 |
| 中间代码生成 | constant_pool_test.cpp、各 pipeline 测试、workbench_test.cpp | 常量去重、四元式、执行和折叠对比 |
| 符号表管理 | symbol_table_test.cpp | 作用域、遮蔽、重复声明、记录和成员 |
| 总控联调 | compiler_flow_test.cpp、integration_driver.cpp、[源码夹具](integration/README.md) | 隔离的流程替身、真实源码编译与执行 |

测试文件名和 CTest 名称保留，避免破坏脚本调用。当前 46 份源码夹具既有成功程序，也有已删除功能的拒绝用例。

`design_tables_current` 运行[设计表格核对](../tools/generate_design_tables.py)，验证六份新增表、共用示例的优化执行对照、九种赋值转换和 14 个语义拒绝例子。

[构建与验收](../docs/使用指南/构建与验收.md)给出全部 22 项 CTest 的执行方法，包含当前词法、语法表格与对应来源的一致性检查。
