# 真实 C 源码联调夹具

> 2026-10-10 范围更新：volatile、补全、C 指针及头文件包含已删除；for 内声明和同块执行后声明被拒绝。以下早期示例与里程碑记录供历史参考；当前以 [语言范围](../../docs/language-scope.md) 和测试清单为准。宏夹具已改为同文件声明，指针夹具用于验证拒绝。

共 32 个用例：16 个正常程序，16 个错误与边界用例。输入、预期输出、返回值及失败阶段统一存于 [manifest.json](manifest.json)。AST 对照和交接细节见 [integration-handoff.md](../../docs/integration-handoff.md)。

| 正常程序 | 检查内容 |
|---|---|
| 01_circle | 基本类型、浮点运算和输出 |
| 02_input_sum | 输入与 for 累加，输入 5，输出 sum = 15 |
| 03_functions | 函数参数、递归和返回 |
| 04_control | while/do、continue、switch 贯穿 |
| 05_arrays | 多维数组与嵌套初始化 |
| 06_struct | 结构体参数值拷贝及返回 |
| 07_pointers | 数组退化、偏移及解引用写入 |
| 08_callback | 函数指针与回调 |
| 09_static_extern | static 局部保值与 extern 复用 |
| 10_typedef_enum | typedef、enum、goto |
| 11_numeric | unsigned 回绕、long、double |
| 12_strings | 字符指针和限宽字符串输入 |
| 13_macros | 包含守卫、条件宏、字符串化、拼接 |
| 14_short_circuit | 逻辑与三目短路 |
| 15_union_sizeof | union 拷贝与 sizeof |
| 16_temporary_array | 返回结构体的数组成员读取 |

| 错误阶段 | 用例 |
|---|---|
| Preprocess | e15：error 指令 |
| Lexer | e01：非法字符 |
| Parser | e02–e05：缺分号、for 声明、混合声明、控制体缺花括号 |
| Semantic | e06–e10：未声明、重复声明、const 修改、参数数量、格式类型 |
| Runtime | e11–e14、e16：除零、越界、空指针、未初始化、步数上限 |

样例中的变量声明都放在各块的语句前，for 使用已声明变量，控制体带花括号。错误样例 e03–e05 故意违反这些约定；它们在其他 C 方言中可能合法，当前期待 Parser 拒绝。Runtime 错误样例由项目运行器明确诊断，不能交给宿主 GCC 执行来判断。

## 现在可以运行

```powershell
cmake -S . -B build/cmake
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure

# 核对夹具、实际预处理结果及词法规则。
python tools/test_integration.py --preprocessor build/cmake/integration_preprocess_driver.exe

# 可选：核实正常样例的预期输出，需要 GCC 的 C 编译环境。
python tools/test_integration.py --reference-gcc 'D:/G++/MinGW/bin/g++.exe'
```

Linux/macOS 去掉驱动路径的 `.exe`；多配置生成器在路径中加 Debug/Release。Python 需 3.10+。CMake 现有测试之外新增 integration_preparation；它只代表准备检查通过。

## 真实源码联调

src/lexer.cpp 和 src/parser.cpp 已实现。integration_driver 链接正式前端，source_integration 已验证全部 32 个用例。

```powershell
python tools/test_integration.py --compiler build/cmake/integration_driver.exe
```

工具逐一比较程序输出、main 返回值、失败阶段及已固定诊断。只统一 CRLF/LF，不去掉空白和尾部换行；失败退出非零。输入通过管道传入，无需人工逐个输入。

16 个正常程序原已用 GCC 核实预期结果；现在全部 32 个用例也已通过本项目正式源码编译执行和错误阶段验证。
