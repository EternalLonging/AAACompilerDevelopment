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
static Node case_node(int value, Node body) { return tree(NodeType::Case, "", integer(value), std::move(body)); }

static void arrays() {
    const auto compiled = compile_ast(main_program(block(
        object("matrix", array(array(basic(TypeKind::Int), 3), 2), list(list(integer(1), integer(2)), list(integer(4)))),
        statement(set(index(index(id("matrix"), integer(1)), integer(2)), integer(7))),
        ret(binary("+", index(index(id("matrix"), integer(0)), integer(1)), index(index(id("matrix"), integer(1)), integer(2)))))));
    succeeds(compiled, 9);
    succeeds(compile_ast(main_program(block(object("a", array(basic(TypeKind::Int), std::nullopt), list(integer(2), integer(5))),
        ret(index(id("a"), integer(1)))))), 5);
    succeeds(compile_ast(program(object("a", array(basic(TypeKind::Int), 2)),
        function("main", TypeKind::Int, block(ret(index(id("a"), integer(0))))))), 0);
    const auto scanf = compile_ast(main_program(block(object("a", array(basic(TypeKind::Int), 2)),
        statement(call("scanf", text("\"%d\""), tree(NodeType::UnaryOp, "&", index(id("a"), integer(1))))),
        ret(index(id("a"), integer(1))))));
    succeeds(scanf, 42, "", "42");
}

static void structures() {
    const auto compiled = compile_ast(aggregate_program());
    succeeds(compiled, 0, "S = 9\n");
    const auto& record = compiled.semantic.symbols.records[0];
    require(record.size == 8 && record.alignment == 4 && record.members[1].offset == 4,
            "结构体必须正确插入填充，不能直接累计成员大小");
    const auto nested = compile_ast(program(
        tree(NodeType::StructDef, "Pair", member("n", basic(TypeKind::Int))),
        tree(NodeType::StructDef, "Box", member("pairs", array(record_type("Pair"), 2))),
        function("main", TypeKind::Int, block(
            object("box", record_type("Box"), list(list(list(integer(3)), list(integer(8))))),
            object("other", record_type("Box")),
            statement(set(id("other"), id("box"))),
            statement(set(field(index(field(id("box"), "pairs"), integer(1)), "n"), integer(100))),
            ret(field(index(field(id("other"), "pairs"), integer(1)), "n"))))));
    succeeds(nested, 8); // 结构体赋值必须复制值，不能共享同一块存储。
    const auto input = compile_ast(program(tree(NodeType::StructDef, "S", member("value", basic(TypeKind::Float))),
        function("main", TypeKind::Int, block(object("s", record_type("S")),
            statement(call("scanf", text("\"%f\""), tree(NodeType::UnaryOp, "&", field(id("s"), "value")))),
            statement(call("printf", text("\"%f\""), field(id("s"), "value"))), ret(integer(0))))));
    succeeds(input, 0, "1.500000", "1.5");
}

static void initialization_and_sizeof() {
    auto size = node(NodeType::Sizeof); size->declared_type = array(basic(TypeKind::Int), 3);
    succeeds(compile_ast(main_program(block(object("s", array(basic(TypeKind::Char), std::nullopt), text("\"ab\"")),
        ret(binary("+", std::move(size), index(id("s"), integer(2))))))), 12);
    succeeds(compile_ast(main_program(block(variable("x", TypeKind::Int, integer(1)),
        ret(binary("+", tree(NodeType::Sizeof, "", tree(NodeType::UnaryOp, "post++", id("x"))), id("x")))))), 5);
    const auto text_array = compile_ast(main_program(block(object("s", array(basic(TypeKind::Char), 2), text("\"ab\"")),
        ret(index(id("s"), integer(1))))));
    succeeds(text_array, 98);
    succeeds(compile_ast(main_program(block(object("a", array(basic(TypeKind::Int), 2), list(integer(7))),
        ret(index(id("a"), integer(1)))))), 0);
}

static void control_flow() {
    const auto compiled = compile_ast(main_program(block(variable("x", TypeKind::Int, integer(0)),
        tree(NodeType::DoWhile, "", block(statement(tree(NodeType::UnaryOp, "pre++", id("x"))),
            tree(NodeType::If, "", binary("<", id("x"), integer(3)), node(NodeType::Continue))),
            binary("<", id("x"), integer(3))),
        tree(NodeType::Switch, "", id("x"), block(
            case_node(1, statement(assign("x", integer(10)))),
            case_node(3, statement(assign("x", integer(20)))),
            case_node(4, block(statement(assign("x", integer(2), "+=")), node(NodeType::Break))),
            tree(NodeType::Default, "", statement(assign("x", integer(99)))))), ret(id("x")))));
    succeeds(compiled, 22); // case 3 落入 case 4，break 退出 switch。
    const auto nested = compile_ast(main_program(block(variable("i", TypeKind::Int, integer(0)), variable("sum", TypeKind::Int, integer(0)),
        tree(NodeType::While, "", binary("<", id("i"), integer(3)), block(
            statement(tree(NodeType::UnaryOp, "pre++", id("i"))),
            tree(NodeType::Switch, "", id("i"), block(case_node(2, node(NodeType::Continue)),
                tree(NodeType::Default, "", node(NodeType::Break)))),
            statement(assign("sum", id("i"), "+=")))), ret(id("sum")))));
    succeeds(nested, 4); // switch 中的 continue 必须跳到外层循环。
    succeeds(compile_ast(main_program(block(ret(tree(NodeType::Conditional, "", integer(0),
        binary("/", integer(1), integer(0)), integer(7)))))), 7);
}

static void boundary_errors() {
    for (const int index_value : {-1, 2}) {
        auto compiled = compile_ast(main_program(block(object("a", array(basic(TypeKind::Int), 2), list(integer(1))),
            ret(index(id("a"), integer(index_value))))));
        std::string output; const auto result = execute(compiled, output);
        require(!result.ok() && result.diagnostics[0].message.find("越界") != std::string::npos, "数组越界必须诊断");
    }
    auto uninitialized = compile_ast(main_program(block(object("a", array(basic(TypeKind::Int), 2)), ret(index(id("a"), integer(1))))));
    std::string output; require(!execute(uninitialized, output).ok(), "未初始化的数组元素不能自动置零");
    auto bad_member = compile_ast(program(tree(NodeType::StructDef, "S", member("x", basic(TypeKind::Int))),
        function("main", TypeKind::Int, block(object("s", record_type("S")), ret(field(id("s"), "x"))))));
    require(!execute(bad_member, output).ok(), "未初始化的结构体成员不能自动置零");
}

static void rejects(Node ast, const std::string& code) {
    const auto semantic = analyze(*ast); bool found = false;
    for (const auto& error : semantic.diagnostics) if (error.code == code) found = true;
    if (!found) print_diagnostics(semantic.diagnostics, std::cerr);
    require(!semantic.ok() && found, "应明确拒绝错误声明或控制流");
}
static void semantic_errors() {
    rejects(main_program(block(object("a", array(basic(TypeKind::Int), 0)), ret(integer(0)))), "SEM_DECL_TYPE");
    rejects(main_program(block(object("a", array(basic(TypeKind::Int), 1), list(integer(1), integer(2))), ret(integer(0)))), "SEM_INIT_COUNT");
    rejects(program(tree(NodeType::StructDef, "S", member("x", basic(TypeKind::Int)), member("x", basic(TypeKind::Int)))), "SYM_RECORD_INVALID");
    rejects(program(tree(NodeType::StructDef, "S", member("s", record_type("S")))), "SEM_LAYOUT");
    rejects(main_program(block(tree(NodeType::Switch, "", integer(0), block(case_node(1, node(NodeType::Empty)), case_node(1, node(NodeType::Empty)))), ret(integer(0)))), "SEM_SWITCH_DUPLICATE");
    rejects(main_program(block(tree(NodeType::Switch, "", floating("1.5"), block()), ret(integer(0)))), "SEM_SWITCH");
    rejects(main_program(block(case_node(1, node(NodeType::Empty)), ret(integer(0)))), "SEM_SWITCH");
    rejects(main_program(block(object("a", array(basic(TypeKind::Int), 2)), ret(index(id("a"), floating("1.5"))))), "SEM_INDEX");
    auto constant = make_type_info(); constant->kind = TypeKind::Int; constant->is_const = true;
    rejects(program(tree(NodeType::StructDef, "S", member("x", constant)),
        function("main", TypeKind::Int, block(object("a", record_type("S"), list(integer(1))),
            object("b", record_type("S"), list(integer(2))), statement(set(id("a"), id("b"))), ret(integer(0))))), "SEM_LVALUE");
}

static void folding_and_invalid_ir() {
    auto folded = compile_ast(main_program(block(ret(binary("+", integer(2), binary("*", integer(3), integer(4)))))));
    succeeds(folded, 14);
    require(folded.ir.program.functions[0].quads.size() == 1, "纯整型常量表达式应直接折叠到常量池");
    const auto floating = compile_ast(main_program(block(ret(binary("<", examples::floating("1.2"), examples::floating("1.8"))))));
    succeeds(floating, 1);
    require(floating.ir.program.functions[0].quads.size() == 1, "浮点常量比较也应折叠，不能先转 int 再比较");
    auto compiled = compile_ast(aggregate_program());
    bool changed = false;
    for (auto& quad : compiled.ir.program.functions[0].quads) {
        if (quad.op == "memberaddr") { quad.arg2 = "999999"; changed = true; break; }
    }
    require(changed, "测试应包含成员寻址指令");
    std::string output; const auto result = execute(compiled, output);
    require(!result.ok() && result.executed_steps == 0 && output.empty(), "坏成员偏移应在执行前拒绝");
}

int main() {
    minic::TypeArena types; // 手工类型及语法树借用的内存，保留到示例/测试结束。
    minic::TypeArenaScope type_scope(types);
    try {
        arrays(); structures(); initialization_and_sizeof(); control_flow();
        boundary_errors(); semantic_errors(); folding_and_invalid_ir();
        std::cout << "M2 数组、结构体、控制流与常量折叠：7 组测试全部通过\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "测试失败：" << error.what() << '\n'; return 1; }
}
