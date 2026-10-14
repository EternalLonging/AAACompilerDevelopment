# 词法分析

将源码扫描为 Token，保留位置和中文错误。

公开入口：`lex(source, filename)`。

| 内容 | 文件 |
|---|---|
| 实现 | [lexer.cpp](../../src/词法分析/lexer.cpp)、[lexer_dfa.hpp](../../src/词法分析/lexer_dfa.hpp)、[词法规则.json](../../src/词法分析/词法规则.json) |
| 公共接口 | [lexer.hpp](../../include/minic/lexer.hpp) |

- [词法实现](词法实现.md)
- [正则规则](正则规则说明.md)
- [单张状态转换图](../../实验设计/词法分析/状态转换图/README.md)

验证：frontend_test.cpp、lexer_probe.cpp；词法规则、表一致性和独立正则对照工具见 tools/README.md。

[返回文档导航](../README.md)
