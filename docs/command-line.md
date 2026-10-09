# 命令行使用

命令行默认先处理宏、条件指令和本地头文件，再执行指定编译阶段。

```powershell
# 在项目根目录使用已经构建的程序
.\build\cmake\minic.exe run examples/frontend_demo.c
.\build\cmake\minic.exe run tests/integration/programs/13_macros.c
# 依次查看展开后的 Token、AST、符号表和四元式
.\build\cmake\minic.exe tokens test.c
.\build\cmake\minic.exe parse test.c
.\build\cmake\minic.exe symbols test.c
.\build\cmake\minic.exe check test.c
.\build\cmake\minic.exe ir test.c
```

PowerShell 不会自动在当前目录或项目构建目录寻找 `minic`。程序不在 PATH 时，使用上面的相对路径，或 `& "完整路径/minic.exe" run "完整路径/test.c"`。源码扩展名不决定支持的语言，当前支持范围见 [frontend.md](frontend.md)，不支持 C++ 的 vector、iostream、cin/cout 等功能。

## 宏与本地头文件

```c
#include "config.h"
#define DOUBLE(x) ((x)+(x))

int main(void) {
    printf("value = %d\n", DOUBLE(N));
    return 0;
}
```

在源码旁放置 `config.h`，内容为 `#define N 5`，即可输出 `value = 10`。也可使用子目录，例如 `#include "inc/config.h"`；嵌套头文件中的相对路径以该头文件自己的目录为起点。换一个终端工作目录不会改变头文件查找结果。

本地读取支持绝对路径、中文、空格、嵌套路径和包含守卫。引号与尖括号目前采用相同的本地路径规则；不搜索系统目录或 PATH，也未提供 `-I` 搜索目录选项。`#include <stdio.h>` 不会自动载入标准库；printf/scanf 仍由项目内建提供，演示源码可以直接调用它们。

找不到头文件或预处理失败时，命令行报告中文错误并返回 1，不继续编译或执行。头文件内部的编译错误显示已解析的完整路径和原始行号。无限递归包含超过 64 层会报错；包含守卫仍可用于合法的循环引用。宏种类与其他限制见 [preprocessor.md](preprocessor.md)。

## 原始源码模式

```powershell
.\build\cmake\minic.exe tokens --raw test.c
.\build\cmake\minic.exe run --raw test.c
```

`--raw` 必须放在命令和文件名之间。该模式直接调用原 `compile`，不展开宏或头文件，Token 保留原始列号和字节偏移。默认预处理模式只保证原始文件与行号，列号为 1、偏移为 0；它展示的是宏展开后进入词法和语法的内容。

两种模式成功时保持原展示格式与 main 返回值约定；语义、IR、解释器和公共 AST 合同不变。Windows 命令行使用 UTF-16 参数读取文件，并以 UTF-8 输出文件路径和中文诊断。

## 验证

```powershell
python tools/test_cli_preprocessing.py build/cmake/minic.exe
ctest --test-dir build/cmake --output-on-failure
```

新增 18 项正式命令行检查，覆盖六个编译命令、宏、相对/绝对/中文头文件、嵌套目录、同名头文件、守卫、诊断路径、失败停止、递归上限、原始源码模式和原有宏夹具。工程现在共 16 项 CTest，原有源码联调清单及预期未修改。
