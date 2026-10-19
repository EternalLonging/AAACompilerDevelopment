# 工具导航

| 所属部分 | 工具 | 用途 |
|---|---|---|
| 词法分析 | [generate_lexer_dfa.py](generate_lexer_dfa.py) | 从词法规则生成 C++ DFA；`--check` 检查是否过期 |
| 词法分析 | [generate_lexer_tables.py](generate_lexer_tables.py)、[generate_lexer_tables.mjs](generate_lexer_tables.mjs) | 推导并生成中文 Excel；`--check` 逐单元格核对已提交表格 |
| 词法分析 | [check_lexer_patterns.py](check_lexer_patterns.py)、[test_lexer_dfa.py](test_lexer_dfa.py) | 正则规则检查、独立对照真实扫描器 |
| 词法分析 | [generate_reference_lexer_diagram.py](generate_reference_lexer_diagram.py) | 生成或核对已确认的单张总图 |
| 五部分联调 | [test_backend.ps1](test_backend.ps1) | 严格编译模块和真实前端并执行测试 |
| 五部分联调 | [test_integration.py](test_integration.py)、[test_acceptance.py](test_acceptance.py) | 源码联调、命令行验收 |
| 总控与展示 | [run_showcase.py](run_showcase.py) | 检查课堂演示并保存各阶段结果 |
| 总控与展示 | [workbench_driver.cpp](workbench_driver.cpp)、[workbench_server.py](workbench_server.py) | 工作台数据驱动和本地服务 |
| 总控与展示 | [test_workbench_service.py](test_workbench_service.py)、[test_workbench_browser.py](test_workbench_browser.py) | 服务与浏览器交互检查 |

`资源/词法参考布局.drawio` 是生成器使用的布局模板，供当前词法图生成和验证；展示成品统一放在[状态转换图目录](../实验设计/词法分析/状态转换图/README.md)。工具保留英文文件名，以便现有脚本调用；用途按中文归类。
