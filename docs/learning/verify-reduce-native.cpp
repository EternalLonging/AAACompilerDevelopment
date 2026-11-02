#include "minic/lexer.hpp"
#include "minic/parser.hpp"
#include "minic/display.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

// 只输出语法树，不执行语义分析，供动画模型对照当前 C++ 实现。
std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '\\' || c == '"') out += '\\';
        if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out + '"';
}
void emit(const minic::ASTNode* n) {
    std::cout << "{\"kind\":" << quoted(minic::ast_node_name(n->kind))
              << ",\"name\":" << quoted(n->name) << ",\"children\":[";
    bool first = true;
    for (auto c : n->children) {
        if (!first) std::cout << ',';
        first = false; emit(c);
    }
    std::cout << "]}";
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    const std::string source((std::istreambuf_iterator<char>(input)), {});
    minic::TypeArena arena;
    minic::TypeArenaScope scope(arena);
    const auto tokens = minic::lex(source);
    auto parsed = minic::parse(tokens.tokens);
    if (!parsed.root) { std::cerr << "parse failed"; return 3; }
    emit(parsed.root);
}
