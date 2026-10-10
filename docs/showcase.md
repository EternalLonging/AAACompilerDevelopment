# 课堂演示准备

构建后执行 `python tools/run_showcase.py --compiler build/cmake/minic.exe`。脚本检查 11 个实际结果，保存各阶段文本到 build/showcase。

| 示例 | 展示内容 | 输入 / 输出 |
|---|---|---|
| 01_pipeline.c | 单词、AST、符号表、语义、IR、循环和输入 | 输入 5，输出 sum = 15 |
| 02_records_macros.c | 数组参数、学生记录和同文件宏 | passed = 2，average = 75.000000 |
| 03_callback_static.c | 直接函数调用和 static 变量 | counter = 1 2，twice = 12 |
| 04_semantic_error.c | 未声明变量定位 | 预期语义失败 |
| 05_runtime_error.c | 数组越界定位 | 预期运行失败 |

03 的文件名沿用原示例名，函数指针回调已删除。工作台可选择这些例子；支持查看真实阶段表格、AST 导出和优化前后对比。演示前先完成 CTest，不能把上一次生成的摘要当成本次验收结果。

当前语法限制和已删除功能见 [language-scope.md](language-scope.md)，界面操作见 [workbench.md](workbench.md)。
