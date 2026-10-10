#include "minic/symbol_table.hpp"

#include <iostream>

int main() {
    minic::TypeArena types; // 手工类型及语法树借用的内存，保留到示例/测试结束。
    minic::TypeArenaScope type_scope(types);
    using namespace minic;
    DiagnosticEngine diagnostics(Phase::Semantic);
    SymbolTable table(diagnostics);
    if (!register_builtins(table)) return 1;

    auto integer = make_type_info();
    integer->kind = TypeKind::Int;
    SymbolEntry variable;
    variable.name = "count";
    variable.type = integer;
    variable.range = {"demo.c", {1, 5, 4}, {1, 10, 9}};
    const auto global = table.insert(variable).value();

    // 进入函数后，同名变量会遮蔽全局变量。
    table.enter_scope(ScopeKind::Function, {"demo.c", {2, 1, 11}, {6, 2, 70}});
    variable.range = {"demo.c", {3, 9, 30}, {3, 14, 35}};
    const auto local = table.insert(variable).value();
    std::cout << "全局 count 编号：" << global << '\n';
    std::cout << "函数内 count 编号：" << table.lookup("count").value() << '\n';
    if (table.lookup("count") != local) return 1;

    // 再次登记本层 count，用来演示重复声明报错。
    variable.range = {"demo.c", {4, 9, 47}, {4, 14, 52}};
    table.insert(variable);
    table.exit_scope();
    std::cout << "退出函数后的 count 编号：" << table.lookup("count").value() << '\n';
    std::cout << '\n';

    for (const auto& error : diagnostics.diagnostics())
        std::cout << error.range.file << ':' << error.line << ':' << error.col
                  << " " << error.message << '\n';
    return 0;
}
