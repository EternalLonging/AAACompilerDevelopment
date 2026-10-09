#include "minic/modules.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void usage(std::ostream& output) {
    output << "用法：minic <tokens|parse|symbols|check|ir|run> [--raw] <源文件>\n"
           << "默认先展开宏和本地头文件；--raw 直接分析原始源码。\n";
}

// 读取源码或头文件；不存在时返回空值，读取失败时报告文件路径。
std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad()) throw std::runtime_error("读取文件失败：" + path.generic_u8string());
    return source;
}

// 参数已转换为 UTF-8，各命令共用同一套源码读取与编译流程。
int cli(const std::vector<std::string>& args) {
    if (args.size() == 2 && args[1] == "--help") { usage(std::cout); return 0; }
    const bool raw = args.size() == 4 && args[2] == "--raw";
    if ((args.size() != 3 && !raw) || (args.size() == 3 && args[2] == "--raw")) {
        usage(std::cerr); return 1;
    }
    const std::string& command = args[1];
    minic::CompileTarget target;
    if (command == "tokens") target = minic::CompileTarget::Tokens;
    else if (command == "parse") target = minic::CompileTarget::Parse;
    else if (command == "symbols" || command == "check") target = minic::CompileTarget::Check;
    else if (command == "ir" || command == "run") target = minic::CompileTarget::IR;
    else { usage(std::cerr); return 1; }
    try {
        const auto source_path = std::filesystem::absolute(std::filesystem::u8path(args.back())).lexically_normal();
        const auto source = read_file(source_path);
        if (!source) { std::cerr << "无法打开源文件：" << args.back() << '\n'; return 1; }
        // 相对 include 路径以包含它的文件所在目录为起点，不依赖终端目录。
        const minic::IncludePathResolver resolver = [](const std::string& header, const std::string& parent) {
            auto path = std::filesystem::u8path(header);
            if (!path.is_absolute()) path = std::filesystem::u8path(parent).parent_path() / path;
            return std::filesystem::absolute(path).lexically_normal().generic_u8string();
        };
        const minic::IncludeLoader loader = [](const std::string& path, const std::string&) {
            return read_file(std::filesystem::u8path(path));
        };
        const auto filename = source_path.generic_u8string();
        auto compilation = raw ? minic::compile(*source, filename, target) :
            minic::compile_preprocessed(*source, filename, target, loader, resolver);
        minic::print_diagnostics(compilation.diagnostics, std::cerr);
        // 检查失败时仍可查看已经生成的部分结果。
        if (command == "tokens" && compilation.lexical)
            minic::print_tokens(compilation.lexical->tokens, std::cout);
        else if (command == "parse" && compilation.syntax && compilation.syntax->root)
            minic::print_ast(*compilation.syntax->root, std::cout);
        else if (command == "symbols" && compilation.semantic)
            minic::print_symbols(compilation.semantic->symbols, std::cout);
        else if (command == "ir" && compilation.ir && compilation.semantic)
            minic::print_ir(compilation.ir->program, compilation.semantic->symbols, std::cout);
        if (!compilation.ok()) return 1;
        if (command == "check") std::cout << "语义检查通过\n";
        if (command == "run") {
            const auto execution = minic::run(compilation.ir->program, compilation.semantic->symbols, std::cin, std::cout);
            minic::print_diagnostics(execution.diagnostics, std::cerr);
            if (!execution.ok()) return 1;
            return static_cast<int>(*execution.exit_code);
        }
        return std::cout ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "编译器执行失败：" << error.what() << '\n';
        return 1;
    }
}

}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    // Windows 使用宽字符参数，中文和空格路径不会被当前代码页改变。
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.push_back(std::filesystem::path(argv[i]).u8string());
    return cli(args);
}
#else
int main(int argc, char* argv[]) {
    return cli(std::vector<std::string>(argv, argv + argc));
}
#endif
