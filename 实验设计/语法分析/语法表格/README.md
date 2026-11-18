# LR 运行文法与 LL 对照资料

正式解析器使用规范 LR(1) 表。运行文法见 [LR文法.bnf](LR文法.bnf)，生成的 ACTION/GOTO 表位于 [lr_table.hpp](../../../src/语法分析/lr_table.hpp)。

以下工作簿和文法文件保留原 LL(1) 版本，仅用于比较；不是当前解析器的运行表。

| 文件 | 内容 |
|---|---|
| [当前文法.bnf](当前文法.bnf) | 原 LL(1) 产生式对照 |
| [文法表.xlsx](文法表.xlsx) | 原 LL 文法的产生式与源码位置 |
| [FIRST与FOLLOW表.xlsx](FIRST与FOLLOW表.xlsx) | LL 文法的 FIRST、FOLLOW 与可空性 |
| [SELECT集表.xlsx](SELECT集表.xlsx) | LL 文法的 SELECT 集 |
| [预测分析表.xlsx](预测分析表.xlsx) | 原 LL(1) 预测表 |
| [当前文法与约束.md](当前文法与约束.md) | 原 LL 文法的检查职责与来源快照 |

开始符号为“程序”。ε 表示空串，END_OF_FILE 表示输入结束。LR 文法对加减、乘除、比较和逻辑运算使用左递归，以产生左结合结构。规范 LR(1) 项目集生成移进、归约、接受动作及 GOTO 转移。

函数定义、变量、成员、单层数组和非空结构体的形状仍写在产生式中。字面量数值、赋值目标、名字和类型由原有阶段检查。

## 重新生成与验收

```powershell
python tools/generate_lr_tables.py --runtime
python tools/generate_lr_tables.py --check
python tools/test_parser_tables.py build/ninja/workbench_driver.exe --lr
```

文法测试用独立 Earley 识别器检查语言范围，再由真实 LR 解析器处理相同样例，核对接受和拒绝结果。
