// 完整流程示例；同学交付 lexer.cpp 和 parser.cpp 后即可链接运行。
// 检查命令：g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I include -fsyntax-only examples/compile_and_run.cpp
#include "minic/modules.hpp"

#include <iostream>
#include <string>

int main() {
    // 主程序提供源码文本；模块接口不负责从磁盘读取源文件。
    const std::string source = R"(
int main()
{
    float r;
    float c;
    r = 1;
    c = 2 * 3.14159 * r;
    printf("c = %f\n", c);
    return 0;
}
)";

    // 按顺序执行所有编译阶段，保留 AST、符号表和 IR 到程序执行结束。
    auto compilation = minic::compile(source, "circle.c", minic::CompileTarget::IR);
    minic::print_diagnostics(compilation.diagnostics, std::cerr);
    if (!compilation.ok()) return 1;

    // ok() 为 true 且目标为 IR 时，以下四个阶段结果必须都存在。
    minic::print_tokens(compilation.lexical->tokens, std::cout);
    minic::print_ast(*compilation.syntax->root, std::cout);
    minic::print_symbols(compilation.semantic->symbols, std::cout);
    minic::print_ir(compilation.ir->program, compilation.semantic->symbols, std::cout);

    // 输入输出流由主程序提供；可换成字符串流做功能测试，或文件流保存输出。
    const auto execution = minic::run(compilation.ir->program,
                                      compilation.semantic->symbols,
                                      std::cin, std::cout);
    minic::print_diagnostics(execution.diagnostics, std::cerr);
    if (!execution.ok()) return 1;
    return static_cast<int>(*execution.exit_code);
}
