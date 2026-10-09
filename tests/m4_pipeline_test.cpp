#include "../examples/ast_examples.hpp"
#include "minic/modules.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace minic;
using namespace minic::examples;
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static TypePtr type(TypeKind kind, bool unsigned_type = false) {
    auto result = std::make_shared<TypeInfo>(); result->kind = kind; result->is_unsigned = unsigned_type; return result;
}
static TypePtr ptr(TypePtr base) { auto result = std::make_shared<TypeInfo>(); result->kind = TypeKind::Pointer; result->base = std::move(base); return result; }
static TypePtr record(const std::string& name, TypeKind kind = TypeKind::Union) { auto result = std::make_shared<TypeInfo>(); result->kind = kind; result->name = name; return result; }
static TypePtr signature(TypePtr result, std::vector<TypePtr> params = {}) { auto value = std::make_shared<TypeInfo>(); value->kind = TypeKind::Function; value->base = std::move(result); value->params = std::move(params); return value; }
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

static void function_pointers() {
    const auto fn = signature(type(TypeKind::Int), {type(TypeKind::Int)});
    auto ast = program(function("twice", TypeKind::Int, block(ret(binary("*", id("x"), integer(2)))), parameter("x", TypeKind::Int)));
    auto callback = node(NodeType::ParamDecl, "fn"); callback->declared_type = ptr(fn);
    ast->children.push_back(function("apply", TypeKind::Int, block(ret(invoke(id("fn"), integer(4)))), std::move(callback)));
    auto body = block(object("fn", ptr(fn), id("twice")), ret(binary("+", invoke(unary("*", id("fn")), integer(3)), call("apply", id("fn")))));
    ast->children.push_back(function("main", TypeKind::Int, std::move(body))); check(std::move(ast), 14);
    auto null_fn = main_program(block(object("fn", ptr(fn), integer(0)), ret(invoke(id("fn"), integer(2)))));
    check(std::move(null_fn), 0, "", "", "空函数指针");
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
}

static void errors_and_boundaries() {
    const auto fn = signature(type(TypeKind::Int), {type(TypeKind::Int)});
    auto bad = program(function("f", TypeKind::Int, block(ret(integer(0))), parameter("x", TypeKind::Float)));
    bad->children.push_back(function("main", TypeKind::Int, block(object("p", ptr(fn), id("f")), ret(integer(0)))));
    rejects(std::move(bad), "SEM_INIT_TYPE");
    auto io = block(object("short", type(TypeKind::Short)), object("long", type(TypeKind::Long)), object("d", type(TypeKind::Double)),
        object("u", type(TypeKind::Int, true)),
        statement(call("scanf", text("\"%hd %ld %lf %x\""), unary("&", id("short")), unary("&", id("long")), unary("&", id("d")), unary("&", id("u")))),
        statement(call("printf", text("\"%hd %ld %lf %x\""), id("short"), id("long"), id("d"), id("u"))), ret(integer(0)));
    check(main_program(std::move(io)), 0, "123 200000 1.250000 ff", "123 200000 1.25 ff");
    auto text_error = block(object("buffer", array(type(TypeKind::Char), 2), text("\"ab\"")), statement(call("printf", text("\"%s\""), id("buffer"))), ret(integer(0)));
    check(main_program(std::move(text_error)), 0, "", "", "终止零");
    auto void_pointer = block(variable("x", TypeKind::Int, integer(5)), object("v", ptr(type(TypeKind::Void)), unary("&", id("x"))),
        object("p", ptr(type(TypeKind::Int)), id("v")), ret(unary("*", id("p"))));
    check(main_program(std::move(void_pointer)), 5);
    auto representation = block(variable("x", TypeKind::Int, node(NodeType::IntLiteral, "0x12345678")),
        object("p", ptr(type(TypeKind::Char, true)), cast(ptr(type(TypeKind::Char, true)), unary("&", id("x")))),
        statement(set(index(id("p"), integer(0)), integer(1))), ret(binary("==", id("x"), node(NodeType::IntLiteral, "0x12345601"))));
    check(main_program(std::move(representation)), 1);
    auto pointer_union = program(tree(NodeType::UnionDef, "U", member("p", ptr(type(TypeKind::Int))), member("n", type(TypeKind::Int))));
    pointer_union->children.push_back(function("main", TypeKind::Int, block(variable("x", TypeKind::Int, integer(7)), object("u", record("U"), list(unary("&", id("x")))),
        ret(field(id("u"), "n")))));
    check(std::move(pointer_union), 0, "", "", "指针表示");
}

static void strings() {
    auto body = block();
    body->children.push_back(object("text", array(type(TypeKind::Char), 6), text("\"hello\"")));
    body->children.push_back(object("p", ptr(type(TypeKind::Char)), id("text")));
    body->children.push_back(statement(set(index(id("p"), integer(0)), node(NodeType::CharLiteral, "'H'"))));
    body->children.push_back(statement(call("printf", text("\"%s %s\""), id("p"), binary("+", text("\"world\""), integer(1)))));
    body->children.push_back(ret(integer(0)));
    check(main_program(std::move(body)), 0, "Hello orld");
    auto input = block(object("buffer", array(type(TypeKind::Char), 5)),
        statement(call("scanf", text("\"%4s\""), id("buffer"))), statement(call("printf", text("\"%s\""), id("buffer"))), ret(integer(0)));
    check(main_program(std::move(input)), 0, "abcd", "abcdef");
    auto overflow = block(object("buffer", array(type(TypeKind::Char), 3)), statement(call("scanf", text("\"%s\""), id("buffer"))), ret(integer(0)));
    check(main_program(std::move(overflow)), 0, "", "abcd", "容量");
    rejects(main_program(block(object("buffer", array(type(TypeKind::Char), 3)),
        statement(call("scanf", text("\"%0s\""), id("buffer"))), ret(integer(0)))), "SEM_FORMAT");
    auto read_only = block(object("p", ptr(type(TypeKind::Char)), text("\"abc\"")), statement(set(index(id("p"), integer(0)), node(NodeType::CharLiteral, "'X'"))), ret(integer(0)));
    check(main_program(std::move(read_only)), 0, "", "", "字面量");
    auto global_string = program(object("message", ptr(type(TypeKind::Char)), text("\"global\"")));
    global_string->children.push_back(function("main", TypeKind::Int, block(
        statement(call("printf", text("\"%s\""), id("message"))), ret(integer(0)))));
    check(std::move(global_string), 0, "global");
}

int main() {
    try { numeric_types(); unions(); function_pointers(); external_objects(); strings(); aggregate_calls(); errors_and_boundaries(); std::cout << "扩展数值、联合体、函数指针、外部声明与字符串：7 组测试全部通过\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "测试失败：" << error.what() << '\n'; return 1; }
}
