#include "minic/symbol_table.hpp"

#include <iostream>

int main() {
    using namespace minic;
    DiagnosticEngine diagnostics(Phase::Semantic);
    SymbolTable table(diagnostics);
    if (!register_builtins(table)) return 1;

    auto integer = std::make_shared<TypeInfo>();
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
    std::cout << "前缀 pri 的查询结果：";
    for (const auto id : table.prefix_query("pri")) std::cout << table.symbol(id)->name << ' ';
    std::cout << '\n';

    for (const auto& error : diagnostics.diagnostics())
        std::cout << error.range.file << ':' << error.line << ':' << error.col
                  << " " << error.message << '\n';
    return 0;
}
