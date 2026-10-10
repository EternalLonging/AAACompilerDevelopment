#include "../examples/ast_examples.hpp"
#include "minic/compiler.hpp"
#include "minic/lexer.hpp"
#include "minic/parser.hpp"

#include <iostream>
#include <stdexcept>

// 这些替身仅验证总控调用顺序，不属于正式词法/语法实现。
namespace {
int lexer_calls = 0;
int parser_calls = 0;
std::string mode;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
minic::Diagnostic failure(minic::Phase phase) {
    minic::Diagnostic result;
    result.phase = phase;
    result.level = minic::Level::Error;
    result.message = "测试阶段失败";
    return result;
}
}

namespace minic {
LexResult lex(const std::string& source, const std::string& filename) {
    ++lexer_calls;
    mode = source;
    LexResult result;
    Token eof;
    eof.range.file = filename;
    result.tokens.push_back(eof);
    if (mode == "lexer_failure") result.diagnostics.push_back(failure(Phase::Lexer));
    return result;
}
ParseResult parse(const std::vector<Token>& tokens) {
    ++parser_calls;
    if (tokens.empty()) throw std::runtime_error("总控没有交接 tokens");
    ParseResult result;
    if (mode == "parser_failure") result.diagnostics.push_back(failure(Phase::Parser));
    else if (mode == "semantic_failure")
        result.root = examples::program(examples::function("main", TypeKind::Int,
                         examples::block(examples::ret(examples::id("missing")))));
    else result.root = examples::loop_program();
    return result;
}
}

int main() {
    minic::TypeArena types; // 手工类型及语法树借用的内存，保留到示例/测试结束。
    minic::TypeArenaScope type_scope(types);
    try {
        using namespace minic;
        for (const auto target : {CompileTarget::Tokens, CompileTarget::Parse, CompileTarget::Check, CompileTarget::IR}) {
            lexer_calls = parser_calls = 0;
            const auto result = compile("success", "flow.c", target);
            require(result.ok() && lexer_calls == 1, "正常流程应成功且只调用词法一次");
            require(parser_calls == (target == CompileTarget::Tokens ? 0 : 1), "总控应在请求阶段停止");
        }
        lexer_calls = parser_calls = 0;
        const auto lexer_failure = compile("lexer_failure");
        require(!lexer_failure.ok() && lexer_failure.lexical && !lexer_failure.syntax &&
                parser_calls == 0 && lexer_failure.diagnostics.size() == 1, "词法失败应阻止语法且仅汇总一次诊断");
        const auto parser_failure = compile("parser_failure");
        require(!parser_failure.ok() && parser_failure.syntax && !parser_failure.semantic &&
                !parser_failure.ir && parser_failure.diagnostics.size() == 1, "语法失败应阻止语义");
        const auto semantic_failure = compile("semantic_failure");
        require(!semantic_failure.ok() && semantic_failure.semantic && !semantic_failure.ir &&
                !semantic_failure.diagnostics.empty(), "语义失败应阻止 IR");
        bool rejected = false;
        try { compile("success", "flow.c", static_cast<CompileTarget>(100)); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "无效目标必须拒绝");
        lexer_calls = parser_calls = 0;
        const auto pp_failure = compile_preprocessed("#error stop", "main.c");
        require(!pp_failure.ok() && pp_failure.preprocessing && !pp_failure.lexical && lexer_calls == 0, "预处理失败应阻止词法");
        const auto pp_success = compile_preprocessed("#define VALUE success\nVALUE", "main.c", CompileTarget::IR);
        require(pp_success.ok() && pp_success.preprocessing && mode.find("success") != std::string::npos, "展开结果应交给词法");
        const auto pp_header = compile_preprocessed("#include \"test.h\"", "main.c", CompileTarget::Tokens,
            [](const std::string&, const std::string&) -> std::optional<std::string> { return "success"; });
        require(pp_header.ok() && pp_header.lexical->tokens[0].range.file == "test.h" &&
                pp_header.lexical->tokens[0].line == 1, "头文件单词应映射到原文件");
        std::cout << "编译总控：阶段停止、错误传播和结果归属检查全部通过\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "测试失败：" << error.what() << '\n';
        return 1;
    }
}
