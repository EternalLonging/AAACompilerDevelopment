#pragma once
#include "minic/interface.hpp"
#include <ostream>

namespace minic {

// 返回单词、节点和类型的可读名字，供文本及图形界面共用。
std::string token_type_name(TokenType type);
std::string ast_node_name(NodeType type);
std::string describe_type(const TypePtr& type);

// 打印单词表。
void print_tokens(const std::vector<Token>& tokens, std::ostream& output);
// 打印抽象语法树。
void print_ast(const Program& program, std::ostream& output);
// 打印符号、标签记录和作用域信息。
void print_symbols(const SymbolTableData& symbols, std::ostream& output);
// 打印四元式和常量池，保留符号与常量编号。
void print_ir(const IRProgram& program, const SymbolTableData& symbols,
              std::ostream& output);
// 打印错误和警告，包括阶段、位置和提示。
void print_diagnostics(const std::vector<Diagnostic>& diagnostics,
                       std::ostream& output);

}
