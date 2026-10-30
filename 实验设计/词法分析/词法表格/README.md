# 当前词法表格

词法规则的唯一来源是 [src/词法分析/词法规则.json](../../../src/词法分析/词法规则.json)，生成结果是 `src/词法分析/lexer_dfa.hpp`。规则检查和 DFA 一致性检查直接读取源码文件，不依赖旧的静态表格或状态图。

```powershell
python tools/check_lexer_patterns.py
python tools/generate_lexer_dfa.py --check
python tools/generate_lexer_tables.py --check
```
