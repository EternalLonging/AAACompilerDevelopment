#include "minic/compiler.hpp"
#include "minic/ir.hpp"
#include "minic/lexer.hpp"
#include "minic/parser.hpp"
#include "minic/semantic.hpp"
#include <stdexcept>

namespace minic {
namespace {
CompilationResult compile_stages(const std::string& source, const std::string& filename,
                                 CompileTarget target) {
    if (target != CompileTarget::Tokens && target != CompileTarget::Parse &&
        target != CompileTarget::Check && target != CompileTarget::IR)
        throw std::invalid_argument("无效的编译目标");
    CompilationResult result;
    TypeArenaScope type_scope(result.types);
    result.target = target;
    const auto collect = [&result](const auto& stage) {
        result.diagnostics.insert(result.diagnostics.end(), stage.diagnostics.begin(), stage.diagnostics.end());
    };
    result.lexical = lex(source, filename);
    collect(*result.lexical);
    if (!result.lexical->ok() || target == CompileTarget::Tokens) return result;
    result.syntax = parse(result.lexical->tokens);
    collect(*result.syntax);
    if (!result.syntax->ok() || target == CompileTarget::Parse) return result;
    result.semantic = analyze(*result.syntax->root, &result.syntax->arena);
    collect(*result.semantic);
    if (!result.semantic->ok() || target == CompileTarget::Check) return result;
    result.ir = generate(*result.syntax->root, result.semantic->symbols);
    collect(*result.ir);
    return result;
}
}

CompilationResult compile(const std::string& source, const std::string& filename, CompileTarget target) {
    return compile_stages(source, filename, target);
}

}
