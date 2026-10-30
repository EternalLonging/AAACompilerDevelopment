# 当前语法分析资料

110 条产生式，49 个非终结符，0 个预测冲突格。

| 文件 | 内容 |
|---|---|
| [当前文法.bnf](当前文法.bnf) | 有编号的 LL(1) 产生式 |
| [文法表.xlsx](文法表.xlsx) | 产生式、对应解析函数及源码位置 |
| [FIRST与FOLLOW表.xlsx](FIRST与FOLLOW表.xlsx) | 非终结符的 FIRST、FOLLOW 与可空性 |
| [SELECT集表.xlsx](SELECT集表.xlsx) | 每条产生式的 SELECT 集 |
| [预测分析表.xlsx](预测分析表.xlsx) | 非空预测格、终结符及阶段检查约定 |
| [当前文法与约束.md](当前文法与约束.md) | 文法、检查职责和来源快照 |

开始符号为“程序”。ε 表示空串，END_OF_FILE 表示输入结束。FIRST 中的 ε 单独列在“可空”列，FOLLOW 不含 ε。预测表每格至多一条产生式，未列出的格子表示语法错误。

函数定义、变量、成员、单层数组和非空结构体的形状直接写在产生式中。字面量数值、赋值左部形状、名字和类型检查仍由对应阶段完成。

正式解析器使用分析栈和 LL(1) 预测表；运行表和这些资料从同一份文法生成。AST 在产生式匹配完成后组合，节点及孩子顺序未改变，语义分析继续使用原接口。

## 重新生成与验收

```powershell
python tools/generate_parser_tables.py --runtime
python tools/generate_parser_tables.py --data build/语法表格数据.json
python tools/generate_parser_tables.py --grammar-data build/文法表数据.json
python tools/generate_design_tables.py --data build/设计表格 --driver build/ninja/workbench_driver.exe
# Excel 导出方式见实验设计/表格总览.md。
python tools/generate_parser_tables.py --check
python tools/test_parser_tables.py build/ninja/workbench_driver.exe
python tools/generate_design_tables.py --check --driver build/ninja/workbench_driver.exe
```

文法与集合来自同一份产生式。测试同时使用独立 Earley 识别器、生成的 LL(1) 预测表和真实解析器核对声明边界。
