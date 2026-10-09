#include "minic/compiler.hpp"
#include "minic/lexer.hpp"
#include "minic/parser.hpp"
#include "minic/semantic.hpp"
#include "minic/ir.hpp"
#include <algorithm>
#include <stdexcept>

namespace minic {
namespace {
void remap(SourceRange& range, const PreprocessResult& preprocessing) {
    if (preprocessing.lines.empty()) return;
    const auto position = [&](std::uint32_t line) {
        auto origin = preprocessing.lines[std::min<std::size_t>(line ? line - 1 : 0, preprocessing.lines.size() - 1)];
        if (line > preprocessing.lines.size()) ++origin.line;
        return origin;
    };
    const auto begin = position(range.begin.line), end = position(range.end.line);
    range.file = begin.file;
    range.begin = {begin.line, 1, 0};
    range.end = {end.file == begin.file ? end.line : begin.line, 1, 0};
}

CompilationResult compile_stages(const std::string& source, const std::string& filename,
                                 CompileTarget target, const PreprocessResult* preprocessing) {

    if (target != CompileTarget::Tokens && target != CompileTarget::Parse &&
        target != CompileTarget::Check && target != CompileTarget::IR)
        throw std::invalid_argument("无效的编译目标");
    CompilationResult result;
    result.target = target;
    const auto collect = [&result](const auto& stage) {
        result.diagnostics.insert(result.diagnostics.end(), stage.diagnostics.begin(), stage.diagnostics.end());
    };
    result.lexical = lex(source, filename);
    if (preprocessing) {
        for (auto& token : result.lexical->tokens) {
            remap(token.range, *preprocessing); token.line = token.range.begin.line; token.col = 1;
        }
        for (auto& diagnostic : result.lexical->diagnostics) {
            remap(diagnostic.range, *preprocessing); diagnostic.line = diagnostic.range.begin.line; diagnostic.col = 1;
            for (auto& related : diagnostic.related) remap(related, *preprocessing);
        }
    }
    collect(*result.lexical);
    if (!result.lexical->ok() || target == CompileTarget::Tokens) return result;
    result.syntax = parse(result.lexical->tokens);
    collect(*result.syntax);
    if (!result.syntax->ok() || target == CompileTarget::Parse) return result;
    result.semantic = analyze(*result.syntax->root);
    collect(*result.semantic);
    if (!result.semantic->ok() || target == CompileTarget::Check) return result;
    result.ir = generate(*result.syntax->root, result.semantic->symbols);
    collect(*result.ir);
    return result;
}
}

CompilationResult compile(const std::string& source, const std::string& filename, CompileTarget target) {
    return compile_stages(source, filename, target, nullptr);
}

CompilationResult compile_preprocessed(const std::string& source, const std::string& filename,
                                      CompileTarget target, const IncludeLoader& loader,
                                      const IncludePathResolver& resolver) {
    if (target != CompileTarget::Tokens && target != CompileTarget::Parse &&
        target != CompileTarget::Check && target != CompileTarget::IR)
        throw std::invalid_argument("无效的编译目标");
    auto preprocessing = preprocess(source, filename, loader, resolver);
    CompilationResult result;
    result.target = target;
    if (preprocessing.ok()) result = compile_stages(preprocessing.source, filename, target, &preprocessing);
    result.diagnostics.insert(result.diagnostics.begin(), preprocessing.diagnostics.begin(), preprocessing.diagnostics.end());
    result.preprocessing = std::move(preprocessing);
    return result;
}

}
