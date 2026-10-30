# 真实源码联调夹具

输入、输出、退出码和失败阶段见 [manifest.json](manifest.json)。当前夹具覆盖成功程序、词法错误、语法错误、语义错误和运行时错误。

| 用例 | 检查内容 | 预期 |
|---|---|---|
| 01_circle | 基本声明、浮点运算和输出 | 正常运行 |
| 02_input_sum | 输入、for 循环和普通赋值 | 正常运行 |
| 03_functions | 函数参数、递归和返回值 | 正常运行 |
| 04_control | while、if、break、continue 和普通赋值 | 正常运行 |
| 05_arrays | 一维数组 | 正常运行 |
| 06_struct | 结构体、赋值与成员 | 正常运行 |
| 11_numeric | int、float 和普通算术 | 正常运行 |
| 12_strings | 字符数组和字符串输入 | 正常运行 |
| 14_short_circuit | 逻辑运算和条件分支 | 正常运行 |
| 16_temporary_array | 结构体返回限制 | Parser 拒绝 |
| e01–e05 | 词法和基本语法错误 | 对应阶段拒绝 |
| e06–e10 | 名字、类型和格式错误 | Semantic 拒绝 |
| e11–e16 | 除零、越界、未初始化和步数限制 | Runtime 拒绝 |

```powershell
ctest --test-dir build/ninja --output-on-failure
python tools/test_integration.py --compiler build/ninja/integration_driver.exe
```

