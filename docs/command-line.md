# 命令行使用

```powershell
build/cmake/minic.exe <tokens|parse|symbols|check|ir|run> [--raw] <源文件>
```

命令依次显示单词表、语法树、符号表、语义检查结果、四元式，或解释运行程序。默认先展开同文件宏；`--raw` 直接分析原文，不展开宏。编译失败时停止后续阶段，诊断写入标准错误。运行成功返回 main 的返回值，编译或执行失败返回 1。

```c
#define N 5
int main(void) {
    printf("value = %d\n", N * 2);
    return 0;
}
```

这个程序输出 `value = 10`。不需要头文件：printf/scanf 已内建登记。`#include` 会在预处理阶段拒绝。

支持中文和空格文件路径；输入由标准输入提供，程序输出写到标准输出。各阶段原始位置与宏展开位置的区别见 [preprocessor.md](preprocessor.md)，其余语言限制见 [language-scope.md](language-scope.md)。
