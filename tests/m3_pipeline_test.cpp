#include "../examples/ast_examples.hpp"
#include "minic/modules.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace minic;
using namespace minic::examples;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static Node unary(const std::string& op, Node value) { return tree(NodeType::UnaryOp, op, std::move(value)); }
static Node main_program(Node body) { return program(function("main", TypeKind::Int, std::move(body))); }
struct Compiled { SemanticResult semantic; IRResult ir; };
static Compiled compile_ast(Node ast) {
    auto semantic = analyze(*ast);
    if (!semantic.ok()) { print_diagnostics(semantic.diagnostics, std::cerr); throw std::runtime_error("正确 AST 应通过语义检查"); }
    auto ir = generate(*ast, semantic.symbols);
    if (!ir.ok()) { print_diagnostics(ir.diagnostics, std::cerr); throw std::runtime_error("正确 AST 应生成 IR"); }
    return {std::move(semantic), std::move(ir)};
}
static RunResult execute(const Compiled& compiled, const std::string& input = "", RunOptions options = {}) {
    std::istringstream source(input); std::ostringstream output;
    return run(compiled.ir.program, compiled.semantic.symbols, source, output, options);
}
static void succeeds(Node ast, int code, const std::string& input = "") {
    const auto result = execute(compile_ast(std::move(ast)), input);
    if (!result.ok()) print_diagnostics(result.diagnostics, std::cerr);
    require(result.ok() && result.exit_code == code, "执行结果错误");
}
static void fails(Node ast, const std::string& message) {
    const auto result = execute(compile_ast(std::move(ast)));
    require(!result.ok() && result.diagnostics[0].message.find(message) != std::string::npos, "运行错误应明确诊断");
}
static void rejects(Node ast, const std::string& code) {
    const auto result = analyze(*ast); bool found = false;
    for (const auto& diagnostic : result.diagnostics) if (diagnostic.code == code) found = true;
    if (!found) print_diagnostics(result.diagnostics, std::cerr);
    require(!result.ok() && found, "错误 AST 应有指定诊断");
}

static void integer_operators() {
    succeeds(main_program(block(ret(binary("&", unary("~", integer(0)), integer(15))))), 15);
    succeeds(main_program(block(variable("x", TypeKind::Int, integer(-7)), ret(binary(">>", id("x"), integer(1))))), -4);
    fails(main_program(block(variable("x", TypeKind::Int, integer(1)), ret(binary("<<", id("x"), integer(32))))), "移位");
    fails(main_program(block(variable("x", TypeKind::Int, integer(2147483647)), ret(binary("<<", id("x"), integer(1))))), "范围");
    rejects(main_program(block(ret(binary("&", floating("1.5"), integer(1))))), "SEM_OPERANDS");
}

int main() {
    minic::TypeArena types; // 手工类型及语法树借用的内存，保留到示例/测试结束。
    minic::TypeArenaScope type_scope(types);
    try {
        integer_operators();
        std::cout << "位运算与整数边界测试全部通过\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "测试失败：" << error.what() << '\n'; return 1; }
}
