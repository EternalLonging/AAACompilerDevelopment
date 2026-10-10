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
static TypePtr pointer(TypePtr base) {
    auto type = make_type_info(); type->kind = TypeKind::Pointer; type->base = std::move(base); return type;
}
static TypePtr named(const std::string& name, TypeKind kind = TypeKind::Named) {
    auto type = make_type_info(); type->kind = kind; type->name = name; return type;
}
static Node unary(const std::string& op, Node value) { return tree(NodeType::UnaryOp, op, std::move(value)); }
static Node alias(const std::string& name, TypePtr type) {
    auto value = node(NodeType::TypedefDecl, name); value->declared_type = std::move(type); return value;
}
static Node typed_parameter(const std::string& name, TypePtr type) {
    auto value = node(NodeType::ParamDecl, name); value->declared_type = std::move(type); return value;
}
template <typename... Parameters>
static Node typed_function(const std::string& name, TypePtr result, Node body, Parameters... parameters) {
    auto value = function(name, TypeKind::Int, std::move(body), std::move(parameters)...);
    auto type = make_type_info(*value->declared_type); type->base = std::move(result); value->declared_type = type;
    return value;
}
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

static void objects_and_pointers() {
    succeeds(main_program(block(variable("x", TypeKind::Int, integer(1)),
        object("p", pointer(basic(TypeKind::Int)), list(unary("&", id("x")))),
        statement(set(unary("*", id("p")), integer(5))), ret(id("x")))), 5);
    succeeds(main_program(block(variable("x", TypeKind::Int),
        object("p", pointer(basic(TypeKind::Int)), unary("&", id("x"))),
        statement(call("scanf", text("\"%d\""), id("p"))), ret(id("x")))), 42, "42");
    succeeds(program(variable("global", TypeKind::Int, integer(4)),
        object("p", pointer(basic(TypeKind::Int)), unary("&", id("global"))),
        function("main", TypeKind::Int, block(statement(set(unary("*", id("p")), integer(6))), ret(id("global"))))), 6);
    succeeds(main_program(block(object("p", pointer(basic(TypeKind::Int)), integer(0)),
        ret(binary("+", unary("!", id("p")), binary("==", id("p"), integer(0)))))), 2);
    succeeds(main_program(block(variable("x", TypeKind::Int, integer(3)),
        object("p", pointer(basic(TypeKind::Int)), tree(NodeType::Conditional, "", integer(1), unary("&", id("x")), integer(0))),
        ret(unary("*", id("p"))))), 3);
}

static void pointer_calls_and_lifetimes() {
    succeeds(program(typed_function("identity", pointer(basic(TypeKind::Int)), block(ret(id("p"))),
            typed_parameter("p", pointer(basic(TypeKind::Int)))),
        function("main", TypeKind::Int, block(variable("x", TypeKind::Int, integer(8)),
            object("p", pointer(basic(TypeKind::Int)), call("identity", unary("&", id("x")))),
            ret(unary("*", id("p")))))), 8);
    fails(program(typed_function("bad", pointer(basic(TypeKind::Int)), block(variable("x", TypeKind::Int, integer(9)), ret(unary("&", id("x"))))),
        function("main", TypeKind::Int, block(ret(unary("*", call("bad")))))), "已经结束");
    fails(main_program(block(object("p", pointer(basic(TypeKind::Int)), integer(0)), ret(unary("*", id("p"))))), "空指针");
    fails(main_program(block(object("p", pointer(basic(TypeKind::Int))), ret(unary("*", id("p"))))), "未初始化");
}

static void linked_objects() {
    succeeds(program(tree(NodeType::StructDef, "Link", member("value", basic(TypeKind::Int)), member("next", pointer(record_type("Link")))),
        function("main", TypeKind::Int, block(
            object("tail", record_type("Link"), list(integer(7), integer(0))),
            object("head", record_type("Link"), list(integer(3), unary("&", id("tail")))),
            object("p", pointer(record_type("Link")), unary("&", id("head"))),
            variable("sum", TypeKind::Int, integer(0)),
            tree(NodeType::While, "", id("p"), block(
                statement(assign("sum", field(id("p"), "value"), "+=")),
                statement(assign("p", field(id("p"), "next"))))), ret(id("sum"))))), 10);
    succeeds(program(tree(NodeType::StructDef, "S", member("p", pointer(basic(TypeKind::Int)))),
        function("main", TypeKind::Int, block(object("s", record_type("S"), list()),
            ret(unary("!", field(id("s"), "p")))))), 1);
}

static void qualifiers() {
    auto constant = make_type_info(); constant->kind = TypeKind::Int; constant->is_const = true;
    succeeds(main_program(block(variable("x", TypeKind::Int, integer(9)),
        object("p", pointer(constant), unary("&", id("x"))), ret(unary("*", id("p"))))), 9);
    rejects(main_program(block(object("x", constant, integer(9)),
        object("p", pointer(basic(TypeKind::Int)), unary("&", id("x"))), ret(integer(0)))), "SEM_INIT_TYPE");
    rejects(main_program(block(variable("x", TypeKind::Int, integer(9)), object("p", pointer(constant), unary("&", id("x"))),
        statement(set(unary("*", id("p")), integer(1))), ret(integer(0)))), "SEM_LVALUE");
    rejects(main_program(block(variable("x", TypeKind::Int, integer(9)), object("p", pointer(constant), unary("&", id("x"))),
        statement(call("scanf", text("\"%d\""), id("p"))), ret(integer(0)))), "SEM_FORMAT");
    rejects(main_program(block(object("p", pointer(basic(TypeKind::Int)), integer(1)), ret(integer(0)))), "SEM_INIT_TYPE");
}

static void array_pointers() {
    succeeds(program(function("sum", TypeKind::Int, block(variable("total", TypeKind::Int, integer(0)),
            tree(NodeType::For, "", variable("i", TypeKind::Int, integer(0)), binary("<", id("i"), integer(3)), unary("post++", id("i")),
                statement(assign("total", index(id("values"), id("i")), "+="))), ret(id("total"))),
            typed_parameter("values", array(basic(TypeKind::Int), std::nullopt))),
        function("main", TypeKind::Int, block(object("values", array(basic(TypeKind::Int), 3), list(integer(2), integer(3), integer(4))),
            ret(call("sum", id("values")))))), 9);
    succeeds(main_program(block(object("a", array(basic(TypeKind::Int), 3), list(integer(2), integer(3), integer(4))),
        object("p", pointer(basic(TypeKind::Int)), id("a")),
        statement(unary("post++", id("p"))), statement(assign("p", integer(1), "+=")),
        ret(binary("+", unary("*", id("p")), binary("-", binary("+", id("a"), integer(3)), id("a")))))), 7);
    succeeds(main_program(block(object("a", array(basic(TypeKind::Int), 2), list(integer(4), integer(5))),
        ret(unary("*", binary("+", integer(1), id("a")))))), 5);
    succeeds(main_program(block(object("matrix", array(array(basic(TypeKind::Int), 2), 2), list(list(integer(1), integer(2)), list(integer(3), integer(4)))),
        object("row", pointer(array(basic(TypeKind::Int), 2)), id("matrix")), ret(index(index(id("row"), integer(1)), integer(0))))), 3);
    fails(main_program(block(object("a", array(basic(TypeKind::Int), 2), list(integer(1))),
        ret(unary("*", binary("+", id("a"), integer(2)))))), "尾后指针");
    fails(main_program(block(object("a", array(basic(TypeKind::Int), 2), list(integer(1))),
        ret(unary("*", binary("+", id("a"), integer(3)))))), "越界");
    fails(main_program(block(variable("x", TypeKind::Int, integer(1)), variable("y", TypeKind::Int, integer(2)),
        ret(binary("-", unary("&", id("x")), unary("&", id("y")))))), "同一个数组");
}

static void aliases_and_enums() {
    succeeds(program(alias("Number", basic(TypeKind::Int)), alias("Row", array(named("Number"), 2)),
        function("main", TypeKind::Int, block(object("row", named("Row"), list(integer(2), integer(5))),
            block(alias("Number", basic(TypeKind::Char)), object("c", named("Number"), node(NodeType::CharLiteral, "'A'"))),
            ret(index(id("row"), integer(1)))))), 5);
    succeeds(program(tree(NodeType::EnumDef, "Choice", node(NodeType::EnumMember, "First"),
            tree(NodeType::EnumMember, "Second", binary("+", id("First"), integer(5))), node(NodeType::EnumMember, "Third")),
        function("main", TypeKind::Int, block(object("choice", named("Choice", TypeKind::Enum), id("Third")),
            tree(NodeType::Switch, "", id("choice"), block(tree(NodeType::Case, "", id("Third"), ret(id("Second"))))), ret(integer(0))))), 5);
    rejects(main_program(block(object("x", named("Missing")), ret(integer(0)))), "SEM_TYPEDEF");
    rejects(program(tree(NodeType::EnumDef, "E", tree(NodeType::EnumMember, "A", integer(2147483647)), node(NodeType::EnumMember, "B"))), "SEM_ENUM");
}

static void integer_operators() {
    succeeds(main_program(block(variable("x", TypeKind::Int, integer(12)), statement(assign("x", integer(2), ">>=")),
        statement(assign("x", integer(4), "|=")), statement(assign("x", integer(3), "^=")),
        ret(binary(",", assign("x", integer(1), "<<="), id("x"))))), 8);
    succeeds(main_program(block(ret(binary("&", unary("~", integer(0)), integer(15))))), 15);
    succeeds(main_program(block(variable("x", TypeKind::Int, integer(-7)), ret(binary(">>", id("x"), integer(1))))), -4);
    fails(main_program(block(variable("x", TypeKind::Int, integer(1)), ret(binary("<<", id("x"), integer(32))))), "移位");
    fails(main_program(block(variable("x", TypeKind::Int, integer(2147483647)), ret(binary("<<", id("x"), integer(1))))), "范围");
    rejects(main_program(block(ret(binary("&", floating("1.5"), integer(1))))), "SEM_OPERANDS");
}

static void static_storage() {
    auto counter = variable("count", TypeKind::Int, integer(0)); counter->storage = StorageClass::Static;
    succeeds(program(function("next", TypeKind::Int, block(std::move(counter), ret(unary("pre++", id("count"))))),
        function("main", TypeKind::Int, block(variable("first", TypeKind::Int, call("next")), ret(binary("+", id("first"), call("next")))))), 3);
    auto saved = variable("value", TypeKind::Int, integer(7)); saved->storage = StorageClass::Static;
    succeeds(program(typed_function("saved", pointer(basic(TypeKind::Int)), block(std::move(saved), ret(unary("&", id("value"))))),
        function("main", TypeKind::Int, block(ret(unary("*", call("saved")))))), 7);
    auto dynamic = variable("value", TypeKind::Int, call("make")); dynamic->storage = StorageClass::Static;
    rejects(program(function("make", TypeKind::Int, block(ret(integer(7)))),
        function("main", TypeKind::Int, block(std::move(dynamic), ret(integer(0))))), "SEM_GLOBAL_INIT");
}

static void labels_and_gotos() {
    succeeds(main_program(block(variable("x", TypeKind::Int, integer(0)), node(NodeType::Goto, "start"),
        statement(assign("x", integer(99))), tree(NodeType::Label, "start", statement(assign("x", integer(1), "+="))),
        tree(NodeType::If, "", binary("<", id("x"), integer(3)), node(NodeType::Goto, "start")), ret(id("x")))), 3);
    rejects(main_program(block(node(NodeType::Goto, "missing"), ret(integer(0)))), "SEM_GOTO");
    rejects(main_program(block(tree(NodeType::Label, "same", node(NodeType::Empty)), tree(NodeType::Label, "same", ret(integer(0))))), "SEM_LABEL");
    fails(main_program(block(node(NodeType::Goto, "end"), variable("x", TypeKind::Int, integer(5)),
        tree(NodeType::Label, "end", ret(id("x"))))), "未初始化");
    const auto compiled = compile_ast(main_program(block(tree(NodeType::Label, "again", node(NodeType::Goto, "again")), ret(integer(0)))));
    RunOptions options; options.max_steps = 20;
    const auto result = execute(compiled, "", options);
    require(!result.ok() && result.executed_steps == 20, "无限 goto 必须受执行上限约束");
}

int main() {
    minic::TypeArena types; // 手工类型及语法树借用的内存，保留到示例/测试结束。
    minic::TypeArenaScope type_scope(types);
    try {
        objects_and_pointers(); pointer_calls_and_lifetimes(); linked_objects(); qualifiers(); array_pointers(); aliases_and_enums(); labels_and_gotos(); integer_operators(); static_storage();
        std::cout << "指针、类型别名、枚举、标签、位运算和静态存储：9 组测试全部通过\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "测试失败：" << error.what() << '\n'; return 1; }
}
