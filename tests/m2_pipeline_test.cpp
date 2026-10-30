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
struct Compiled { SemanticResult semantic; IRResult ir; };
static Compiled compile_ast(Node ast) {
    auto semantic = analyze(*ast);
    if (!semantic.ok()) { print_diagnostics(semantic.diagnostics, std::cerr); throw std::runtime_error("正确 AST 应通过语义检查"); }
    auto ir = generate(*ast, semantic.symbols);
    if (!ir.ok()) { print_diagnostics(ir.diagnostics, std::cerr); throw std::runtime_error("正确 AST 应生成 IR"); }
    return {std::move(semantic), std::move(ir)};
}
static RunResult execute(const Compiled& compiled, std::string& text, const std::string& input = "") {
    std::istringstream source(input); std::ostringstream output;
    auto result = run(compiled.ir.program, compiled.semantic.symbols, source, output);
    text = output.str(); return result;
}
static void succeeds(const Compiled& compiled, int code, const std::string& expected = "", const std::string& input = "") {
    std::string text; const auto result = execute(compiled, text, input);
    if (!result.ok()) print_diagnostics(result.diagnostics, std::cerr);
    require(result.ok() && result.exit_code == code && text == expected, "执行结果或输出错误");
}
static Node main_program(Node body) { return program(function("main", TypeKind::Int, std::move(body))); }

static void arrays() {
    auto compiled = compile_ast(main_program(block(
        object("values", array(basic(TypeKind::Int), 3)),
        statement(set(index(id("values"), integer(0)), integer(2))),
        statement(set(index(id("values"), integer(1)), integer(3))),
        statement(set(index(id("values"), integer(2)), integer(4))),
        ret(binary("+", index(id("values"), integer(1)), index(id("values"), integer(2)))))));
    succeeds(compiled, 7);
    const auto scanf = compile_ast(main_program(block(object("a", array(basic(TypeKind::Int), 2)),
        statement(call("scanf", text("\"%d\""), tree(NodeType::UnaryOp, "&", index(id("a"), integer(1))))),
        ret(index(id("a"), integer(1))))));
    succeeds(scanf, 42, "", "42");
}

static void structures() {
    const auto compiled = compile_ast(aggregate_program());
    succeeds(compiled, 0, "S = 9\n");
    const auto& record = compiled.semantic.symbols.records[0];
    require(record.size == 8 && record.alignment == 4 && record.members[1].offset == 4, "结构体布局错误");
    const auto input = compile_ast(program(tree(NodeType::StructDef, "S", member("value", basic(TypeKind::Float))),
        function("main", TypeKind::Int, block(object("s", record_type("S")),
            statement(call("scanf", text("\"%f\""), tree(NodeType::UnaryOp, "&", field(id("s"), "value")))),
            statement(call("printf", text("\"%f\""), field(id("s"), "value"))), ret(integer(0))))));
    succeeds(input, 0, "1.500000", "1.5");
}

static void control_flow() {
    const auto nested = compile_ast(main_program(block(
        variable("i", TypeKind::Int, integer(0)), variable("sum", TypeKind::Int, integer(0)),
        tree(NodeType::While, "", binary("<", id("i"), integer(4)), block(
            statement(assign("i", binary("+", id("i"), integer(1)))),
            tree(NodeType::If, "", binary("==", id("i"), integer(2)), node(NodeType::Continue)),
            statement(assign("sum", binary("+", id("sum"), id("i")))),
            tree(NodeType::If, "", binary("==", id("i"), integer(3)), node(NodeType::Break)))),
        ret(id("sum")))));
    succeeds(nested, 4);
}

static void boundary_errors() {
    for (const int index_value : {-1, 2}) {
        auto compiled = compile_ast(main_program(block(object("a", array(basic(TypeKind::Int), 2)),
            statement(set(index(id("a"), integer(0)), integer(1))), ret(index(id("a"), integer(index_value))))));
        std::string output; const auto result = execute(compiled, output);
        require(!result.ok() && result.diagnostics[0].message.find("越界") != std::string::npos, "数组越界必须诊断");
    }
    auto uninitialized = compile_ast(main_program(block(object("a", array(basic(TypeKind::Int), 2)), ret(index(id("a"), integer(1))))));
    std::string output; require(!execute(uninitialized, output).ok(), "未初始化的数组元素不能自动置零");
}

static void folding_and_invalid_ir() {
    auto folded = compile_ast(main_program(block(ret(binary("+", integer(2), binary("*", integer(3), integer(4)))))));
    succeeds(folded, 14);
    require(folded.ir.program.functions[0].quads.size() == 1, "纯整型常量表达式应直接折叠到常量池");
    const auto floating_result = compile_ast(main_program(block(ret(binary("<", examples::floating("1.2"), examples::floating("1.8"))))));
    succeeds(floating_result, 1);
}

int main() {
    minic::TypeArena types;
    minic::TypeArenaScope type_scope(types);
    try {
        arrays(); structures(); control_flow(); boundary_errors(); folding_and_invalid_ir();
        std::cout << "M2 数组、结构体、控制流与常量折叠：5 组测试全部通过\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "测试失败：" << error.what() << '\n'; return 1; }
}
