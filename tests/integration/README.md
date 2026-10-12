# 真实源码联调夹具

共 32 个用例。输入、输出、退出码和失败阶段统一保存在 [manifest.json](manifest.json)。

| 用例 | 检查内容 | 预期 |
|---|---|---|
| 01_circle | 基本声明、浮点运算和输出 | 正常运行 |
| 02_input_sum | 输入、for 循环和复合赋值 | 正常运行 |
| 03_functions | 函数参数、递归和返回值 | 正常运行 |
| 04_control | while、do、continue 和 switch 贯穿 | 正常运行 |
| 05_arrays | 多维数组和嵌套初始化 | 正常运行 |
| 06_struct | 结构体按值传参、返回值与成员 | 正常运行 |
| 07_pointers | 拒绝已移除的 C 语言指针 | Parser 拒绝 |
| 08_callback | 拒绝已移除的 C 语言指针 | Parser 拒绝 |
| 09_static_extern | extern 复用全局对象与 static 局部保值 | 正常运行 |
| 10_typedef_goto | typedef 和 goto 标签 | 正常运行 |
| 11_numeric | 无符号回绕、long 和 double | 正常运行 |
| 12_strings | 字符数组、字符指针和限宽字符串输入 | 正常运行 |
| 14_short_circuit | 逻辑短路与三目表达式只执行选中分支 | 正常运行 |
| 15_union_sizeof | 拒绝已移除的 union | Parser 拒绝 |
| 16_temporary_array | 函数返回结构体后读取数组成员 | 正常运行 |
| e01_invalid_byte | 词法拒绝非法字符 | Lexer 拒绝 |
| e02_missing_semi | 语法拒绝缺少分号 | Parser 拒绝 |
| e03_for_declaration | 当前文法不接受 for 内声明 | Parser 拒绝 |
| e04_mixed_declaration | 当前文法不接受语句后声明 | Parser 拒绝 |
| e05_unbraced_if | 当前文法要求控制体花括号 | Parser 拒绝 |
| e06_undeclared | 语义拒绝未声明名字 | Semantic 拒绝 |
| e07_duplicate | 语义拒绝同层重复变量 | Semantic 拒绝 |
| e08_const_write | 语义拒绝修改 const | Semantic 拒绝 |
| e09_argument_count | 语义拒绝错误实参数量 | Semantic 拒绝 |
| e10_format | 语义拒绝格式与实参类型不符 | Semantic 拒绝 |
| e11_divide_zero | 执行时报告除零 | Runtime 拒绝 |
| e12_bounds | 执行时报告数组越界 | Runtime 拒绝 |
| e13_null | 拒绝已移除的 C 语言指针 | Parser 拒绝 |
| e14_uninitialized | 执行时报告未初始化读取 | Runtime 拒绝 |
| e16_step_limit | 无限循环受执行步数保护 | Runtime 拒绝 |
| e17_enum_removed | 拒绝已删除的语言功能 | Parser 拒绝 |
| e18_macro_removed | 拒绝已删除的语言功能 | Lexer 拒绝 |

```powershell
cmake -S . -B build/cmake
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
python tools/test_integration.py --compiler build/cmake/integration_driver.exe
```

工具使用正式前端，逐一核对输出、main 返回值及诊断；只统一 CRLF/LF。删除功能的旧程序保留为拒绝用例，不再作为成功示例。
