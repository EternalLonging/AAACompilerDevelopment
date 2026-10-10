#include "minic/modules.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
// 读取源码或头文件，找不到时返回空值。
std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad()) throw std::runtime_error("读取联调文件失败");
    return text;
}
const char* phase_name(minic::Phase phase) {
    switch (phase) {
    case minic::Phase::Preprocess: return "Preprocess";
    case minic::Phase::Lexer: return "Lexer";
    case minic::Phase::Parser: return "Parser";
    case minic::Phase::Semantic: return "Semantic";
    case minic::Phase::IR: return "IR";
    case minic::Phase::Runtime: return "Runtime";
    }
    return "Unknown";
}
// 测试标记只写到错误流，不混入被测 C 程序输出。
void diagnostics(const std::vector<minic::Diagnostic>& entries) {
    for (const auto& entry : entries) {
        std::cerr << "[fixture_phase=" << phase_name(entry.phase) << "]"
                  << "[fixture_code=" << entry.code << "] " << entry.range.file << ':'
                  << entry.line << ':' << entry.col << ' ' << entry.message << '\n';
    }
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
#else
int main(int argc, char* argv[]) {
#endif
    if (argc != 2) { std::cerr << "用法：integration_driver <源文件>\n"; return 2; }
    try {
#ifdef _WIN32
        // Windows 参数使用 UTF-16，避免中文目录被当前窄字符代码页破坏。
        const auto source_path = std::filesystem::absolute(argv[1]).lexically_normal();
#else
        const auto source_path = std::filesystem::absolute(std::filesystem::u8path(argv[1])).lexically_normal();
#endif
        const auto source = read_file(source_path);
        if (!source) throw std::runtime_error("找不到联调源文件");
        // 仅用于测试夹具的本地头文件。正式系统头文件搜索另行实现。
#ifdef MINIC_FIXTURE_PREPROCESS_ONLY
        const auto preprocessing = minic::preprocess(*source, source_path.u8string());
        diagnostics(preprocessing.diagnostics);
        if (!preprocessing.ok()) return 1;
        std::cout << preprocessing.source;
        return std::cout ? 0 : 2;
#else
        auto compilation = minic::compile_preprocessed(*source, source_path.u8string(), minic::CompileTarget::IR);
        diagnostics(compilation.diagnostics);
        if (!compilation.ok()) return 1;
        minic::RunOptions options;
        options.max_steps = 10000;
        options.max_call_depth = 128;
        const auto execution = minic::run(compilation.ir->program, compilation.semantic->symbols, std::cin, std::cout, options);
        diagnostics(execution.diagnostics);
        if (!execution.ok()) return 1;
        std::cerr << "[fixture_exit=" << *execution.exit_code << "]\n";
        return std::cout ? 0 : 2;
#endif
    } catch (const std::exception& error) {
        std::cerr << "[fixture_driver_error] " << error.what() << '\n'; return 2;
    }
}
