# 真实源码联调夹具

共 46 个用例。输入、输出、退出码和失败阶段见 [manifest.json](manifest.json)。

| 用例 | 检查内容 | 预期 |
|---|---|---|
| 01_circle | 基本声明、浮点运算和输出 | 正常运行 |
| 02_input_sum | 输入、for 循环和普通赋值 | 正常运行 |
| 03_functions | 函数参数、递归和返回值 | 正常运行 |
| 04_control | while、if、break、continue 和普通赋值 | 正常运行 |
| 05_arrays | 一维数组和初始化 | 正常运行 |
| 06_struct | 简单结构体、赋值与成员 | 正常运行 |
| 07_pointers | 拒绝已移除的 C 语言指针 | Parser 拒绝 |
| 08_callback | 拒绝已移除的 C 语言指针 | Parser 拒绝 |
| 09_static_extern | 拒绝已移除的声明或类型别名 | Parser 拒绝 |
| 10_typedef_goto | 拒绝已移除的声明或类型别名 | Parser 拒绝 |
| 11_numeric | int、float 和普通算术 | 正常运行 |
| 12_strings | 字符数组和字符串输入 | 正常运行 |
| 14_short_circuit | 逻辑运算和条件分支 | 正常运行 |
| 15_union_sizeof | 拒绝已移除的 union | Parser 拒绝 |
| 16_temporary_array | 拒绝结构体按值返回 | Parser 拒绝 |
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
| e19_do_removed | 拒绝已删除的教学语言功能 | Parser 拒绝 |
| e20_switch_removed | 拒绝已删除的教学语言功能 | Parser 拒绝 |
| e21_goto_removed | 拒绝已删除的教学语言功能 | Parser 拒绝 |
| e22_label_removed | 拒绝已删除的教学语言功能 | Parser 拒绝 |
| e23_auto_removed | 拒绝已删除的教学语言功能 | Parser 拒绝 |
| e24_register_removed | 拒绝已删除的教学语言功能 | Parser 拒绝 |
| e25_static_removed | 拒绝已删除的教学语言功能 | Parser 拒绝 |
| e26_case_removed | 拒绝已删除的教学语言功能 | Parser 拒绝 |
| e27_default_removed | 拒绝已删除的教学语言功能 | Parser 拒绝 |
| e28_extern_removed | 拒绝已删除的教学语言功能 | Parser 拒绝 |
| e29_extended_type_removed | 拒绝 MiniC 范围外的基础类型 | Parser 拒绝 |
| e30_multidimensional_array | 拒绝多维数组 | Parser 拒绝 |
| e31_function_prototype_removed | 拒绝函数声明和原型 | Parser 拒绝 |
| e32_compound_assignment_removed | 拒绝复合赋值 | Parser 拒绝 |

```powershell
ctest --test-dir build/cmake --output-on-failure
python tools/test_integration.py --compiler build/cmake/integration_driver.exe
```

使用正式前端逐一核对输出、返回值和诊断；删除功能的旧源码作为拒绝用例保留。
