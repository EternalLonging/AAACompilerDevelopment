#include "ast_examples.hpp"
#include "minic/modules.hpp"

#include <iostream>
#include <sstream>

int main() {
    minic::TypeArena types; // 手工类型及语法树借用的内存，保留到示例/测试结束。
    minic::TypeArenaScope type_scope(types);
    auto ast = minic::examples::loop_program();
    auto semantic = minic::analyze(*ast);
    minic::print_diagnostics(semantic.diagnostics, std::cerr);
    if (!semantic.ok()) return 1;
    auto ir = minic::generate(*ast, semantic.symbols);
    minic::print_diagnostics(ir.diagnostics, std::cerr);
    if (!ir.ok()) return 1;
    minic::print_symbols(semantic.symbols, std::cout);
    minic::print_ir(ir.program, semantic.symbols, std::cout);
    std::istringstream input("5");
    std::cout << "\n程序输入：5\n程序输出：\n";
    const auto execution = minic::run(ir.program, semantic.symbols, input, std::cout);
    minic::print_diagnostics(execution.diagnostics, std::cerr);
    if (!execution.ok()) return 1;
    std::cout << "退出码：" << *execution.exit_code << "，执行指令数：" << execution.executed_steps << '\n';
    return 0;
}
