#include "../examples/ast_examples.hpp"
#include "minic/modules.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace minic;
using namespace minic::examples;
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static TypePtr type(TypeKind kind, bool unsigned_type = false) {
    auto result = make_type_info(); result->kind = kind; result->is_unsigned = unsigned_type; return result;
}
static TypePtr record(const std::string& name, TypeKind kind = TypeKind::Union) { auto result = make_type_info(); result->kind = kind; result->name = name; return result; }
static TypePtr signature(TypePtr result, std::vector<TypePtr> params = {}) { auto value = make_type_info(); value->kind = TypeKind::Function; value->base = std::move(result); value->params = std::move(params); return value; }
static Node unary(const std::string& op, Node value) { return tree(NodeType::UnaryOp, op, std::move(value)); }
static Node cast(TypePtr target, Node value) { auto result = tree(NodeType::Cast, "", std::move(value)); result->declared_type = std::move(target); return result; }
static Node external(Node value) { value->storage = StorageClass::Extern; return value; }
static Node invoke(Node target, Node arg) { return tree(NodeType::Call, "", std::move(target), std::move(arg)); }
static Node main_program(Node body) { return program(function("main", TypeKind::Int, std::move(body))); }
static void check(Node ast, int expected, const std::string& output = "", const std::string& input = "", const std::string& failure = "") {
    auto semantic = analyze(*ast);
    if (!semantic.ok()) { print_diagnostics(semantic.diagnostics, std::cerr); throw std::runtime_error("正确 AST 应通过语义检查"); }
    const auto ir = generate(*ast, semantic.symbols);
    if (!ir.ok()) { print_diagnostics(ir.diagnostics, std::cerr); throw std::runtime_error("正确 AST 应生成 IR"); }
    std::istringstream source(input); std::ostringstream target;
    const auto result = run(ir.program, semantic.symbols, source, target);
    if (failure.empty()) {
        if (!result.ok()) print_diagnostics(result.diagnostics, std::cerr);
        require(result.ok() && result.exit_code == expected && target.str() == output, "执行或输出错误");
    } else require(!result.ok() && result.diagnostics[0].message.find(failure) != std::string::npos, "应明确报告运行错误");
}
static void rejects(Node ast, const std::string& code) {
    const auto result = analyze(*ast); bool found = false;
    for (const auto& diagnostic : result.diagnostics) if (diagnostic.code == code) found = true;
    if (!found) print_diagnostics(result.diagnostics, std::cerr);
    require(!result.ok() && found, "应报告语义错误");
}

static void numeric_types() {
    auto body = block();
    body->children.push_back(object("s", type(TypeKind::Short), integer(1234)));
    body->children.push_back(object("l", type(TypeKind::Long), node(NodeType::IntLiteral, "2147483647L")));
    body->children.push_back(object("u", type(TypeKind::Int, true), node(NodeType::IntLiteral, "4294967295U")));
    body->children.push_back(statement(assign("u", integer(1), "+=")));
    body->children.push_back(object("d", type(TypeKind::Double), cast(type(TypeKind::Double), integer(1))));
    body->children.push_back(statement(assign("d", cast(type(TypeKind::Double), floating("0.5")), "+=")));
    body->children.push_back(statement(call("printf", text("\"%hd %ld %u %lf\""), id("s"), id("l"), id("u"), id("d"))));
    body->children.push_back(ret(integer(0)));
    check(main_program(std::move(body)), 0, "1234 2147483647 0 1.500000");
    check(main_program(block(object("u", type(TypeKind::Int, true), node(NodeType::IntLiteral, "4294967295U")),
        ret(binary("==", binary("*", id("u"), id("u")), integer(1))))), 1);
    check(main_program(block(object("s", type(TypeKind::Short), integer(32768)), ret(integer(0)))), 0, "", "", "范围");
    auto size = node(NodeType::Sizeof); size->declared_type = type(TypeKind::Double);
    check(main_program(block(ret(std::move(size)))), 8);
    rejects(main_program(block(ret(node(NodeType::IntLiteral, "4294967296U")))), "SEM_LITERAL");
}

static void unions() {
    auto ast = program(tree(NodeType::UnionDef, "Data", member("n", type(TypeKind::Int)), member("bytes", array(type(TypeKind::Char, true), 4))));
    auto body = block();
    body->children.push_back(object("data", record("Data"), list(node(NodeType::IntLiteral, "0x12345678"))));
    body->children.push_back(object("copy", record("Data")));
    body->children.push_back(statement(set(id("copy"), id("data"))));
    body->children.push_back(statement(set(index(field(id("data"), "bytes"), integer(0)), integer(0))));
    body->children.push_back(ret(cast(type(TypeKind::Int), index(field(id("copy"), "bytes"), integer(0)))));
    ast->children.push_back(function("main", TypeKind::Int, std::move(body)));
    check(std::move(ast), 120);
    auto float_union = program(tree(NodeType::UnionDef, "U", member("f", type(TypeKind::Float)), member("n", type(TypeKind::Int))));
    auto use = block(object("u", record("U"), list(floating("1.0"))), ret(binary("==", field(id("u"), "n"), node(NodeType::IntLiteral, "1065353216"))));
    float_union->children.push_back(function("main", TypeKind::Int, std::move(use))); check(std::move(float_union), 1);
}

static void external_objects() {
    auto ast = program(external(variable("x", TypeKind::Int)), function("get", TypeKind::Int, block(ret(id("x")))));
    ast->children.push_back(variable("x", TypeKind::Int, integer(7)));
    ast->children.push_back(variable("x", TypeKind::Int));
    auto body = block(external(variable("x", TypeKind::Int)), statement(assign("x", integer(8))), ret(call("get")));
    ast->children.push_back(function("main", TypeKind::Int, std::move(body))); check(std::move(ast), 8);
    rejects(program(external(variable("missing", TypeKind::Int)), function("main", TypeKind::Int, block(ret(id("missing"))))), "SEM_UNDEFINED_OBJECT");
    rejects(program(variable("x", TypeKind::Int, integer(1)), variable("x", TypeKind::Int, integer(2))), "SYM_DECL_CONFLICT");
}

static void aggregate_calls() {
    auto ast = program(tree(NodeType::StructDef, "Pair", member("x", type(TypeKind::Int)), member("y", type(TypeKind::Int))));
    auto arg = node(NodeType::ParamDecl, "p"); arg->declared_type = record("Pair", TypeKind::Struct);
    auto fn = function("change", TypeKind::Int, block(statement(set(field(id("p"), "x"), integer(9))), ret(id("p"))), std::move(arg));
    fn->declared_type = signature(record("Pair", TypeKind::Struct), {record("Pair", TypeKind::Struct)});
    ast->children.push_back(std::move(fn));
    auto body = block(object("p", record("Pair", TypeKind::Struct), list(integer(1), integer(2))),
        object("copy", record("Pair", TypeKind::Struct), call("change", id("p"))),
        ret(binary("+", field(id("p"), "x"), field(call("change", id("copy")), "x"))));
    ast->children.push_back(function("main", TypeKind::Int, std::move(body))); check(std::move(ast), 10);
    auto arrays = program(tree(NodeType::StructDef, "Box", member("values", array(type(TypeKind::Int), 2))));
    auto create = function("make", TypeKind::Int, block(object("box", record("Box", TypeKind::Struct), list(list(integer(4), integer(8)))), ret(id("box"))));
    create->declared_type = signature(record("Box", TypeKind::Struct)); arrays->children.push_back(std::move(create));
    arrays->children.push_back(function("main", TypeKind::Int, block(ret(index(field(call("make"), "values"), integer(1))))));
    check(std::move(arrays), 8);
    auto qualified = make_type_info(); qualified->kind = TypeKind::Int; qualified->is_const = true;
    auto prototype = node(NodeType::FunctionDecl, "f"); prototype->declared_type = signature(type(TypeKind::Int), {type(TypeKind::Int)});
    prototype->children.push_back(parameter("", TypeKind::Int));
    auto arg_const = node(NodeType::ParamDecl, "x"); arg_const->declared_type = qualified;
    auto declarations = program(std::move(prototype), function("f", TypeKind::Int, block(ret(id("x"))), std::move(arg_const)),
        function("main", TypeKind::Int, block(ret(call("f", integer(9))))));
    check(std::move(declarations), 9);
    auto bad_parameter = node(NodeType::ParamDecl, "x"); bad_parameter->declared_type = qualified;
    rejects(program(function("f", TypeKind::Int, block(statement(assign("x", integer(1))), ret(id("x"))), std::move(bad_parameter))), "SEM_LVALUE");
    auto array_error = program(tree(NodeType::StructDef, "Box", member("values", array(type(TypeKind::Int), 2))));
    auto get = function("make", TypeKind::Int, block(object("box", record("Box", TypeKind::Struct), list(list(integer(4), integer(8)))), ret(id("box"))));
    get->declared_type = signature(record("Box", TypeKind::Struct)); array_error->children.push_back(std::move(get));
    array_error->children.push_back(function("main", TypeKind::Int, block(ret(index(field(call("make"), "values"), integer(2))))));
    check(std::move(array_error), 0, "", "", "越界");
}

static void errors_and_boundaries() {
    auto io = block(object("short", type(TypeKind::Short)), object("long", type(TypeKind::Long)), object("d", type(TypeKind::Double)),
        object("u", type(TypeKind::Int, true)),
        statement(call("scanf", text("\"%hd %ld %lf %x\""), unary("&", id("short")), unary("&", id("long")), unary("&", id("d")), unary("&", id("u")))),
        statement(call("printf", text("\"%hd %ld %lf %x\""), id("short"), id("long"), id("d"), id("u"))), ret(integer(0)));
    check(main_program(std::move(io)), 0, "123 200000 1.250000 ff", "123 200000 1.25 ff");
    auto text_error = block(object("buffer", array(type(TypeKind::Char), 2), text("\"ab\"")), statement(call("printf", text("\"%s\""), id("buffer"))), ret(integer(0)));
    check(main_program(std::move(text_error)), 0, "", "", "终止零");

}

static void strings() {
    auto body = block();
    body->children.push_back(object("text", array(type(TypeKind::Char), 6), text("\"hello\"")));
    body->children.push_back(statement(set(index(id("text"), integer(0)), node(NodeType::CharLiteral, "'H'"))));
    body->children.push_back(statement(call("printf", text("\"%s %s\""), id("text"), text("\"world\""))));
    body->children.push_back(ret(integer(0)));
    check(main_program(std::move(body)), 0, "Hello world");
    auto input = block(object("buffer", array(type(TypeKind::Char), 5)),
        statement(call("scanf", text("\"%4s\""), id("buffer"))), statement(call("printf", text("\"%s\""), id("buffer"))), ret(integer(0)));
    check(main_program(std::move(input)), 0, "abcd", "abcdef");
    auto overflow = block(object("buffer", array(type(TypeKind::Char), 3)), statement(call("scanf", text("\"%s\""), id("buffer"))), ret(integer(0)));
    check(main_program(std::move(overflow)), 0, "", "abcd", "容量");
    rejects(main_program(block(object("buffer", array(type(TypeKind::Char), 3)),
        statement(call("scanf", text("\"%0s\""), id("buffer"))), ret(integer(0)))), "SEM_FORMAT");

}

int main() {
    minic::TypeArena types; // 手工类型及语法树借用的内存，保留到示例/测试结束。
    minic::TypeArenaScope type_scope(types);
    try { numeric_types(); unions(); external_objects(); strings(); aggregate_calls(); errors_and_boundaries(); std::cout << "扩展数值、联合体、外部声明、聚合调用与字符串：6 组测试全部通过\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "测试失败：" << error.what() << '\n'; return 1; }
}
