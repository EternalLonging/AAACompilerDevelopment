# 当前语法分析资料

依据当前递归下降实现整理，集中保存当前版本。历史 C89 草案仍放在[历史参考](../历史参考/README.md)。

| 文件 | 内容 |
|---|---|
| [文法表.xlsx](文法表.xlsx) | 独立的文法产生式表，127 条产生式，按编号列出左右部及实现位置 |
| [FIRST与FOLLOW表.xlsx](FIRST与FOLLOW表.xlsx) | 60 个非终结符的可空性、FIRST 与 FOLLOW |
| [SELECT集表.xlsx](SELECT集表.xlsx) | 127 条产生式各自的 SELECT 集 |
| [预测分析表.xlsx](预测分析表.xlsx) | 非空预测格及配套冲突、终结符、上下文约束 |
| [语法分析表.xlsx](语法分析表.xlsx) | 11 张工作表，提供文法、集合、预测表、优先级、AST 与诊断等可筛选数据 |
| [当前文法.bnf](当前文法.bnf) | 127 条有编号的产生式，60 个非终结符，使用真实 TokenType 名字 |
| [当前文法与约束.md](当前文法与约束.md) | 可直接阅读的文法、18 项上下文约束、3 个预测冲突及来源快照 |

Excel 工作表依次为：文法产生式、FIRST与FOLLOW、SELECT集、预测分析表、冲突与处理、终结符、运算符优先级、AST孩子顺序、语法约束、语法诊断、恢复与接口。

开始符号为“程序”，ε 表示空串，END_OF_FILE 是输入结束标记。FIRST 中的 ε 通过“可空”列表示；FOLLOW 不含 ε。预测表每行保存一个非空格，未列出的组合为错误格，冲突格保留全部候选产生式。

正式解析器使用递归下降和优先级攀升。这些集合只针对整理出的文法骨架计算，须与上下文约束一同阅读。例如骨架可接受 `struct S;`，实际解析器按项目范围拒绝前向声明；`a+b=1` 的赋值目标形状也由额外检查拒绝。不能用这份预测表代替正式解析器。

二元运算优先级、错误码和源码位置自动读取；文法结构、AST 孩子顺序、上下文约束和恢复说明由实现人工整理。来源哈希用于发现改动，不证明二者完全等价。回归通过独立 Earley 识别器核对真实前端接受样例的骨架覆盖，并用手算文法检查可空传播和 SELECT 冲突。

## 重新生成与验收

从项目根目录执行。Python 3.10+ 负责推导和检查；导出 Excel 还需要 Node.js 与外部提供的 `@oai/artifact-tool`。

```powershell
python tools/generate_parser_tables.py --data build/语法表格数据.json
node tools/export_frontend_tables.mjs build/语法表格数据.json 实验设计/语法分析/语法表格/语法分析表.xlsx build/语法表格预览 <node_modules目录>
python tools/generate_parser_tables.py --grammar-data build/文法表数据.json
node tools/export_frontend_tables.mjs build/文法表数据.json 实验设计/语法分析/语法表格/文法表.xlsx build/文法表预览 <node_modules目录>
python tools/generate_parser_tables.py --check
python tools/test_parser_tables.py build/ninja/workbench_driver.exe
```

`--data` 更新 BNF、Markdown 和中间数据；Excel 由第二步更新。修改解析器后先核对生成器中的文法与约束，再重新生成。`--check` 逐单元格检查实际 Excel，并核对 BNF、Markdown 与来源快照。普通 CTest 验收不需要 Node.js 或电子表格库。

`--grammar-data` 只提取独立文法表的数据，与综合表中的文法产生式保持一致。`--check` 同时核对两份 Excel。

另三份独立集合与预测表复用同一文法推导，随[五部分设计表工具](../../../tools/generate_design_tables.py)生成和核对，命令见[表格总览](../../表格总览.md)。

生成和预览共用前端导出器；原词法生成入口仍可使用。中间数据、预览和检查日志放在被忽略的 build 目录。
