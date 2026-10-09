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

struct Compiled {
    SemanticResult semantic;
    IRResult ir;
};

static Compiled compile_ast(Node ast) {
    auto semantic = analyze(*ast);
    if (!semantic.ok()) {
        print_diagnostics(semantic.diagnostics, std::cerr);
        throw std::runtime_error("测试 AST 应通过语义检查");
    }
    auto ir = generate(*ast, semantic.symbols);
    if (!ir.ok()) {
        print_diagnostics(ir.diagnostics, std::cerr);
        throw std::runtime_error("测试 AST 应生成四元式");
    }
    return {std::move(semantic), std::move(ir)};
}

static RunResult execute(const Compiled& compiled, std::string& output, const std::string& input = "", RunOptions options = {}) {
    std::istringstream source(input);
    std::ostringstream destination;
    auto result = run(compiled.ir.program, compiled.semantic.symbols, source, destination, options);
    output = destination.str();
    return result;
}

static void success(const RunResult& result, int exit) {
    if (!result.ok()) print_diagnostics(result.diagnostics, std::cerr);
    require(result.ok() && *result.exit_code == exit, "程序返回结果错误");
}

static void test_loop_and_io() {
    const auto compiled = compile_ast(loop_program());
    std::string output;
    const auto result = execute(compiled, output, "5");
    success(result, 0);
    require(output == "sum = 15\n" && result.executed_steps > 20, "for/scanf/printf 闭环失败");
    success(execute(compiled, output, "3"), 0);
    require(output == "sum = 6\n", "每次执行必须有独立存储");
    require(!execute(compiled, output, "bad").ok(), "scanf 输入失败应诊断");
}

static void test_recursion() {
    auto factorial = function("factorial", TypeKind::Int, block(
        tree(NodeType::If, "", binary("<=", id("n"), integer(1)), ret(integer(1))),
        ret(binary("*", id("n"), call("factorial", binary("-", id("n"), integer(1)))))), parameter("n", TypeKind::Int));
    auto compiled = compile_ast(program(std::move(factorial), function("main", TypeKind::Int, block(
        ret(call("factorial", integer(5)))))));
    std::string output;
    success(execute(compiled, output), 120);
    const auto limited = execute(compiled, output, "", {10000, 3});
    require(!limited.ok() && limited.diagnostics[0].message.find("调用深度") != std::string::npos,
            "递归调用深度必须受限制");
}

static void test_short_circuit_and_arguments() {
    auto combine = function("combine", TypeKind::Int, block(ret(binary("+", binary("*", id("a"), integer(10)), id("b")))),
                            parameter("a", TypeKind::Int), parameter("b", TypeKind::Int));
    auto compiled = compile_ast(program(std::move(combine), function("main", TypeKind::Int, block(
        variable("x", TypeKind::Int, integer(1)),
        statement(binary("&&", integer(0), assign("x", integer(100)))),
        statement(binary("||", integer(1), binary("/", integer(1), integer(0)))),
        ret(call("combine", id("x"), call("combine", assign("x", integer(2)), integer(3))))))));
    std::string output;
    success(execute(compiled, output), 33); // combine(1, combine(2, 3))。
    require(output.empty(), "短路与嵌套实参不能产生额外输出");
}

static void test_float_char_globals_and_void() {
    auto compiled = compile_ast(program(
        variable("g", TypeKind::Int, integer(4)),
        function("show", TypeKind::Void, block(
            statement(call("printf", text("\"%s %d %f %c %%\\n\""), text("\"ok\""), id("g"), id("f"), id("c"))),
            node(NodeType::Return)), parameter("f", TypeKind::Float), parameter("c", TypeKind::Char)),
        function("main", TypeKind::Int, block(
            variable("f", TypeKind::Float, integer(2)),
            variable("c", TypeKind::Char, node(NodeType::CharLiteral, "'A'")),
            statement(call("show", binary("+", id("f"), floating("0.5")), id("c"))),
            ret(integer(3))))));
    std::string output;
    success(execute(compiled, output), 3);
    require(output == "ok 4 2.500000 A %\n", "浮点提升、字符串和 void 调用错误");
    auto input_compiled = compile_ast(program(function("main", TypeKind::Int, block(
        variable("f", TypeKind::Float), variable("c", TypeKind::Char),
        statement(call("scanf", text("\"%f %c\""), tree(NodeType::UnaryOp, "&", id("f")), tree(NodeType::UnaryOp, "&", id("c")))),
        statement(call("printf", text("\"%f %c\""), id("f"), id("c"))), ret(integer(0))))));
    success(execute(input_compiled, output, "1.25 Z"), 0);
    require(output == "1.250000 Z", "scanf float/char 输入错误");
}

static void test_while_break_continue_and_shadow() {
    auto compiled = compile_ast(program(function("main", TypeKind::Int, block(
        variable("x", TypeKind::Int, integer(2)),
        block(variable("x", TypeKind::Int, integer(99)), statement(assign("x", integer(50)))),
        variable("i", TypeKind::Int, integer(0)), variable("sum", TypeKind::Int, integer(0)),
        tree(NodeType::While, "", binary("<", id("i"), integer(10)), block(
            statement(tree(NodeType::UnaryOp, "pre++", id("i"))),
            tree(NodeType::If, "", binary("==", id("i"), integer(2)), node(NodeType::Continue)),
            tree(NodeType::If, "", binary("==", id("i"), integer(5)), node(NodeType::Break)),
            statement(assign("sum", id("i"), "+=")))),
        ret(binary("+", id("sum"), id("x")))))));
    std::string output;
    success(execute(compiled, output), 10); // 1+3+4+外层 x=2。
}

static void semantic_failure(Node ast, const std::string& code) {
    const auto result = analyze(*ast);
    require(!result.ok(), "错误 AST 不应通过语义检查");
    bool found = false;
    for (const auto& diagnostic : result.diagnostics) if (diagnostic.code == code) found = true;
    if (!found) print_diagnostics(result.diagnostics, std::cerr);
    require(found, "应报告对应的语义错误代码");
}

static void test_semantic_errors() {
    semantic_failure(program(function("main", TypeKind::Int, block(ret(id("missing"))))), "SEM_UNDECLARED");
    semantic_failure(program(function("main", TypeKind::Int, block(variable("x", TypeKind::Int),
        variable("x", TypeKind::Int), ret(integer(0))))), "SYM_DECL_CONFLICT");
    semantic_failure(program(function("main", TypeKind::Int, block(ret(floating("1.2"))))), "SEM_RETURN");
    semantic_failure(program(function("main", TypeKind::Int, block(node(NodeType::Break), ret(integer(0))))), "SEM_LOOP");
    semantic_failure(program(function("main", TypeKind::Int, block(statement(call("printf", text("\"%f\""), integer(1))), ret(integer(0))))), "SEM_FORMAT");
    semantic_failure(program(function("main", TypeKind::Int, block())), "SEM_MISSING_RETURN");
    semantic_failure(program(function("main", TypeKind::Int, block(ret(node(NodeType::IntLiteral, "2147483648"))))), "SEM_LITERAL");
    semantic_failure(program(function("f", TypeKind::Int, block(variable("x", TypeKind::Int), ret(integer(0))), parameter("x", TypeKind::Int))), "SYM_DECL_CONFLICT");
    auto prototype = function("f", TypeKind::Int, block());
    prototype->kind = NodeType::FunctionDecl;
    prototype->children.clear();
    semantic_failure(program(std::move(prototype), function("main", TypeKind::Int, block(ret(call("f"))))), "SEM_UNDEFINED_FUNCTION");
    auto bad = program();
    bad->children.push_back(nullptr);
    semantic_failure(std::move(bad), "SEM_AST_SHAPE");
    semantic_failure(node(NodeType::Block), "SEM_ROOT");
    semantic_failure(program(function("main", TypeKind::Int, block(ret(node(NodeType::ArrayAccess))))), "SEM_AST_SHAPE");
    semantic_failure(program(variable("g", TypeKind::Int, call("printf", text("\"bad\"")))), "SEM_GLOBAL_INIT");
    auto constant = variable("x", TypeKind::Int, integer(1));
    auto const_type = std::make_shared<TypeInfo>(*constant->declared_type);
    const_type->is_const = true;
    constant->declared_type = const_type;
    semantic_failure(program(function("main", TypeKind::Int, block(std::move(constant),
        statement(assign("x", integer(2))), ret(integer(0))))), "SEM_LVALUE");
    semantic_failure(program(function("f", TypeKind::Int, block(ret(id("x"))), parameter("x", TypeKind::Int)),
        function("main", TypeKind::Int, block(ret(call("f"))))), "SEM_ARGUMENT_COUNT");
}

static void test_runtime_errors_and_limits() {
    std::string output;
    auto division = compile_ast(program(function("main", TypeKind::Int, block(
        statement(call("printf", text("\"before\""))), ret(binary("/", integer(1), integer(0)))))));
    const auto result = execute(division, output);
    require(!result.ok() && output == "before" && result.diagnostics[0].range.file == "ast-demo.c",
            "运行报错应保留已有输出和源码位置");
    auto overflow = compile_ast(program(function("main", TypeKind::Int, block(ret(binary("+", integer(2147483647), integer(1)))))));
    require(!execute(overflow, output).ok(), "整数溢出必须诊断");
    auto uninitialized = compile_ast(program(function("main", TypeKind::Int, block(variable("x", TypeKind::Int), ret(id("x"))))));
    require(!execute(uninitialized, output).ok(), "未初始化变量不能默认为零");
    auto repeat_local = compile_ast(program(function("main", TypeKind::Int, block(
        variable("i", TypeKind::Int, integer(0)),
        tree(NodeType::While, "", binary("<", id("i"), integer(2)), block(
            variable("x", TypeKind::Int),
            tree(NodeType::If, "", binary("==", id("i"), integer(0)), statement(assign("x", integer(7)))),
            statement(call("printf", text("\"%d\""), id("x"))),
            statement(tree(NodeType::UnaryOp, "pre++", id("i"))))), ret(integer(0))))));
    require(!execute(repeat_local, output).ok() && output == "7", "循环再次声明局部变量不能沿用旧值");
    const auto loop = compile_ast(program(function("main", TypeKind::Int, block(
        tree(NodeType::For, "", node(NodeType::Empty), node(NodeType::Empty), node(NodeType::Empty), block()), ret(integer(0))))));
    const auto limited = execute(loop, output, "", {40, 10});
    require(!limited.ok() && limited.executed_steps == 40, "无限循环必须在执行步数上限停止");
    auto explicit_cast = tree(NodeType::Cast, "", floating("3.9"));
    explicit_cast->declared_type = basic(TypeKind::Int);
    const auto cast = compile_ast(program(function("main", TypeKind::Int, block(ret(std::move(explicit_cast))))));
    success(execute(cast, output, "", {0, 0}), 3);
    auto float_comparison = compile_ast(program(function("main", TypeKind::Int, block(
        ret(binary("<", floating("1.2"), floating("1.8")))))));
    success(execute(float_comparison, output), 1);
    auto remainder = compile_ast(program(function("main", TypeKind::Int, block(
        ret(binary("%", integer(-7), integer(3)))))));
    success(execute(remainder, output), -1);
}

static void test_invalid_ir_and_generator() {
    auto compiled = compile_ast(loop_program());
    std::string output;
    const auto original = compiled.ir.program;
    compiled.ir.program.functions[0].quads.push_back({"unknown", "-", "-", "-"});
    compiled.ir.program.functions[0].locations.push_back({});
    auto result = execute(compiled, output, "5");
    require(!result.ok() && result.executed_steps == 0 && output.empty(), "无效 IR 应在任何执行前拒绝");
    compiled.ir.program = original;
    compiled.ir.program.functions[0].quads.push_back({"jmp", "-", "-", "MISSING"});
    compiled.ir.program.functions[0].locations.push_back({});
    require(!execute(compiled, output).ok(), "未知跳转标号应拒绝");
    compiled.ir.program = original;
    compiled.ir.program.entry_function = invalid_id;
    require(!execute(compiled, output).ok(), "入口无效应拒绝");
    compiled.ir.program = original;
    compiled.ir.program.functions[0].locations.pop_back();
    require(!execute(compiled, output).ok(), "位置与指令数量不一致应拒绝");
    compiled.ir.program = original;
    compiled.ir.program.functions[0].quads[0].result = "%s99999";
    require(!execute(compiled, output).ok(), "非法符号引用不能越界");
    auto ast = loop_program();
    require(!generate(*ast, {}).ok(), "未做语义分析的树不能生成 IR");
    auto identifier_ast = program(function("main", TypeKind::Int, block(variable("x", TypeKind::Int, integer(1)), ret(id("x")))));
    auto identifier_semantic = analyze(*identifier_ast);
    identifier_ast->children[0]->children.back()->children.back()->children[0]->symbol_id = invalid_id;
    require(!generate(*identifier_ast, identifier_semantic.symbols).ok(), "缺少符号绑定应拒绝");
}

static void test_display_and_result() {
    auto ast = loop_program();
    auto semantic = analyze(*ast);
    auto ir = generate(*ast, semantic.symbols);
    std::ostringstream output;
    print_ast(*ast, output);
    print_symbols(semantic.symbols, output);
    print_ir(ir.program, semantic.symbols, output);
    print_tokens({Token{}}, output);
    DiagnosticEngine diagnostics(Phase::Semantic);
    diagnostics.report(Level::Error, ast->range, "示例错误", "TEST");
    print_diagnostics(diagnostics.diagnostics(), output);
    require(output.str().find("%s") != std::string::npos && output.str().find("%c") != std::string::npos &&
            output.str().find("END_OF_FILE") != std::string::npos && output.str().find("示例错误") != std::string::npos,
            "展示必须包含身份、种别和中文诊断");
    require(output.str().find("sum = %d\\n") != std::string::npos, "常量池展示应转义换行");
    CompilationResult result;
    require(!result.ok(), "未执行的编译结果不能成功");
    result.target = CompileTarget::Tokens;
    result.lexical = LexResult{};
    require(result.ok(), "只请求词法时正常结果应成功");
    result.semantic = SemanticResult{};
    require(!result.ok(), "超出请求目标的阶段结果不能成功");
    result.semantic.reset();
    result.target = CompileTarget::IR;
    result.syntax = ParseResult{};
    result.syntax->root = std::move(ast);
    result.semantic = std::move(semantic);
    result.ir = std::move(ir);
    require(result.ok(), "各请求阶段成功才算完整成功");
    result.diagnostics = diagnostics.take_diagnostics();
    require(!result.ok(), "汇总诊断含错误不能成功");
}

int main() {
    try {
        test_loop_and_io();
        test_recursion();
        test_short_circuit_and_arguments();
        test_float_char_globals_and_void();
        test_while_break_continue_and_shadow();
        test_semantic_errors();
        test_runtime_errors_and_limits();
        test_invalid_ir_and_generator();
        test_display_and_result();
        std::cout << "M1 语义、四元式、解释执行与展示：9 组测试全部通过\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "测试失败：" << error.what() << '\n';
        return 1;
    }
}
