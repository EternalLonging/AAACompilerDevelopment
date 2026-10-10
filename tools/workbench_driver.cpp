#include "minic/modules.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace {
std::filesystem::path workspace_root; // 本次临时项目目录，用来显示项目内的相对文件名。
std::string file_name(const std::string& name) {
    const auto path = std::filesystem::u8path(name);
    const auto relative = path.lexically_relative(workspace_root);
    if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..") return relative.generic_u8string();
    return path.filename().u8string();
}
// 将字符串写成 JSON，避免源码中的引号或换行破坏结果。
std::string quoted(const std::string& text) {
    static const char* hex = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
std::optional<std::string> read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad()) throw std::runtime_error("文件读取失败");
    return text;
}
// 诊断同时保留阶段、代码、原始文件与行号，供编辑器定位。
void diagnostics(std::ostream& out, const std::vector<minic::Diagnostic>& entries) {
    static const char* phases[] = {"预处理", "词法", "语法", "语义", "中间代码", "运行"};
    out << '[';
    bool first = true;
    for (const auto& d : entries) {
        if (!first) out << ',';
        first = false;
        out << "{\"phase\":" << quoted(phases[static_cast<unsigned>(d.phase)])
            << ",\"code\":" << quoted(d.code) << ",\"message\":" << quoted(d.message)
            << ",\"file\":" << quoted(file_name(d.range.file))
            << ",\"line\":" << d.line << ",\"column\":" << d.col << '}';
    }
    out << ']';
}
// 将语法树保存成父子编号表，界面负责布局，不从文本反推树结构。
void ast(std::ostream& out, const minic::ASTNode& node, int parent, unsigned& next) {
    const auto id = next++;
    if (id) out << ',';
    out << "{\"id\":" << id << ",\"parent\":" << parent << ",\"kind\":" << quoted(minic::ast_node_name(node.kind))
        << ",\"name\":" << quoted(node.name) << ",\"type\":" << quoted(minic::describe_type(node.type ? node.type : node.declared_type))
        << ",\"file\":" << quoted(file_name(node.range.file))
        << ",\"line\":" << node.range.begin.line << ",\"scope\":" << node.scope_id << '}';
    for (const auto& child : node.children) ast(out, *child, static_cast<int>(id), next);
}
std::string value_text(const minic::ConstantValue& value) {
    return std::visit([](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) return std::string("未解码");
        else { std::ostringstream out; out << item; return out.str(); }
    }, value);
}
// 记录真实四元式、常量池和执行统计，优化两侧分别执行同一份输入。
void ir(std::ostream& out, const minic::IRResult& result, const minic::SymbolTableData& symbols,
        const std::optional<minic::RunResult>& execution, const std::string& output) {
    out << "{\"ok\":" << (result.ok() ? "true" : "false") << ",\"diagnostics\":";
    diagnostics(out, result.diagnostics);
    std::ostringstream text; minic::print_ir(result.program, symbols, text);
    out << ",\"text\":" << quoted(text.str()) << ",\"quads\":[";
    bool first = true;
    const auto group = [&](const std::string& name, const std::vector<minic::Quadruple>& quads) {
        for (std::size_t i = 0; i < quads.size(); ++i) {
            if (!first) out << ',';
            first = false;
            const auto& q = quads[i];
            out << "{\"function\":" << quoted(name) << ",\"index\":" << i << ",\"op\":" << quoted(q.op)
                << ",\"arg1\":" << quoted(q.arg1) << ",\"arg2\":" << quoted(q.arg2) << ",\"result\":" << quoted(q.result) << '}';
        }
    };
    group("全局初始化", result.program.global_initializers);
    for (const auto& f : result.program.functions) group(symbols.symbols.at(f.symbol_id).name, f.quads);
    out << "],\"constants\":[";
    first = true;
    for (const auto& c : result.program.constants.entries) {
        if (!first) out << ',';
        first = false;
        out << "{\"id\":" << c.id << ",\"type\":" << quoted(minic::describe_type(c.type))
            << ",\"value\":" << quoted(value_text(c.value)) << '}';
    }
    out << "],\"run\":";
    if (!execution) out << "null";
    else {
        out << "{\"ok\":" << (execution->ok() ? "true" : "false") << ",\"output\":" << quoted(output)
            << ",\"steps\":" << execution->executed_steps << ",\"exit\":";
        if (execution->exit_code) out << *execution->exit_code; else out << "null";
        out << ",\"diagnostics\":"; diagnostics(out, execution->diagnostics); out << '}';
    }
    out << '}';
}

int driver(const std::vector<std::string>& args) {
    if (args.size() != 5 && args.size() != 6) throw std::runtime_error("用法：workbench_driver inspect|run 源文件 输入文件 光标字节位置 [项目目录]");
    const auto file = std::filesystem::absolute(std::filesystem::u8path(args[2])).lexically_normal();
    workspace_root = args.size() == 6 ? std::filesystem::absolute(std::filesystem::u8path(args[5])).lexically_normal() : file.parent_path();
    const auto relative_source = file.lexically_relative(workspace_root);
    if (relative_source.empty() || relative_source.is_absolute() || *relative_source.begin() == "..") throw std::runtime_error("源文件必须位于工作台项目内");
    const auto source = read(file);
    if (!source) throw std::runtime_error("找不到编辑器源码");
    if (args[1] != "inspect" && args[1] != "run") throw std::runtime_error("未知工作台操作");
    auto compiled = minic::compile_preprocessed(*source, file.generic_u8string(), minic::CompileTarget::Check);
    std::cout << "{\"ok\":" << (compiled.ok() ? "true" : "false") << ",\"diagnostics\":";
    diagnostics(std::cout, compiled.diagnostics);
    std::cout << ",\"preprocessed\":" << quoted(compiled.preprocessing ? compiled.preprocessing->source : "") << ",\"tokens\":[";
    if (compiled.lexical) {
        bool first = true;
        for (const auto& t : compiled.lexical->tokens) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"kind\":" << quoted(minic::token_type_name(t.type)) << ",\"text\":" << quoted(t.lexeme)
                      << ",\"line\":" << t.line << ",\"file\":" << quoted(file_name(t.range.file)) << '}';
        }
    }
    std::cout << "],\"ast\":[";
    if (compiled.syntax && compiled.syntax->root) { unsigned next = 0; ast(std::cout, *compiled.syntax->root, -1, next); }
    std::cout << "],\"symbols\":[";
    if (compiled.semantic) {
        bool first = true;
        for (const auto& s : compiled.semantic->symbols.symbols) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"id\":" << s.id << ",\"name\":" << quoted(s.name) << ",\"type\":" << quoted(minic::describe_type(s.type))
                      << ",\"scope\":" << s.scope << ",\"line\":" << s.line << '}';
        }
    }
    std::cout << ']';
    if (compiled.ok()) {
        minic::TypeArenaScope type_scope(compiled.types);
        const auto& symbols = compiled.semantic->symbols;
        auto baseline = minic::generate(*compiled.syntax->root, symbols, {false});
        auto optimized = minic::generate(*compiled.syntax->root, symbols, {true});
        std::optional<minic::RunResult> before, after;
        std::ostringstream before_out, after_out;
        if (args[1] == "run" && baseline.ok() && optimized.ok()) {
            const auto input = read(std::filesystem::u8path(args[3])).value_or("");
            std::istringstream in1(input), in2(input);
            minic::RunOptions options; options.max_steps = 100000; options.max_call_depth = 128;
            before = minic::run(baseline.program, symbols, in1, before_out, options);
            after = minic::run(optimized.program, symbols, in2, after_out, options);
        }
        std::cout << ",\"baseline\":"; ir(std::cout, baseline, symbols, before, before_out.str());
        std::cout << ",\"optimized\":"; ir(std::cout, optimized, symbols, after, after_out.str());
        std::cout << ",\"equivalent\":";
        if (!before || !after || !before->ok() || !after->ok()) std::cout << "null";
        else std::cout << ((before_out.str() == after_out.str() && before->exit_code == after->exit_code) ? "true" : "false");
    }
    std::cout << '}'; return 0;
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
#else
int main(int argc, char* argv[]) {
#endif
    try {
        std::vector<std::string> args;
        for (int i = 0; i < argc; ++i) args.push_back(std::filesystem::path(argv[i]).u8string());
        return driver(args);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 2;
    }
}
