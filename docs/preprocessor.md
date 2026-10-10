# 宏预处理

`preprocess(source, filename)` 返回展开后的源码、原始行号映射和诊断。`compile_preprocessed(source, filename, target)` 先预处理，再执行请求的编译阶段；预处理失败时不进入词法分析。

支持对象宏、函数宏、递归展开保护、字符串化 `#`、拼接 `##`、宏参数重扫描、续行，以及 `#if`、`#ifdef`、`#ifndef`、`#elif`、`#else`、`#endif`、`#undef` 和 `#error`。宏表每次调用独立，不在项目文件间共享。

活动分支里的 `#include` 一律拒绝，不再提供头文件读取器或路径解析器。不活动的条件分支被跳过。需要的宏和声明直接写在当前 `.c` 文件；内建 printf/scanf 无需 stdio.h。

`PreprocessResult` 保存 `source`、`lines`、`diagnostics`，通过 `ok()` 判断是否有错误。行映射保留原文件和行号；展开后的 Token 列号映射为 1、字节偏移为 0，不能把它们当成原文中精确的字符范围。使用 `tokens --raw` 查看原始列号。

预处理是教学实现，不提供完整 C 预处理、系统头文件、标准库或跨文件链接。当前范围见 [language-scope.md](language-scope.md)。
