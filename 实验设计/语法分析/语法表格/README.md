# 当前语法分析资料

文法资料依据当前递归下降实现整理。预测分析表用于讲解和核对 LL(1) 文法，实际 AST 构造入口仍是 `src/语法分析/parser.cpp`。

| 文件 | 内容 |
|---|---|
| [当前文法.bnf](当前文法.bnf) | 122 条有编号的产生式，56 个非终结符，使用真实 TokenType 名字 |
| [当前文法与约束.md](当前文法与约束.md) | 文法、9 项上下文约束、0 个预测冲突和来源快照 |

开始符号为“程序”，ε 表示空串，`END_OF_FILE` 是输入结束标记。FIRST、FOLLOW、SELECT 集和预测格由 `tools/generate_parser_tables.py` 推导，未列出的组合都是错误格。

正式解析器使用递归下降和优先级攀升。文法资料说明产生式结构，AST 孩子顺序、类型检查和错误恢复由实现负责。

## 重新生成与验收

```powershell
python tools/generate_parser_tables.py --data build/语法表格数据.json
python tools/generate_parser_tables.py --grammar-data build/文法表数据.json
python tools/generate_parser_tables.py --check
python tools/test_parser_tables.py build/ninja/workbench_driver.exe
```

`--data` 更新 BNF、Markdown 和综合数据；`--grammar-data` 生成独立文法数据。中间数据放在被忽略的 `build` 目录。
