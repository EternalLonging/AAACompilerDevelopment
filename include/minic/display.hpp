#pragma once
#include "minic/interface.hpp"
#include <ostream>

namespace minic {

// 【文本展示；实现文件 src/display.cpp】
// 所有函数只读产物，只向调用者提供的流输出，不重新编译、不关闭流或修改文件。
// 文件导出可由主程序传入 ofstream；本组接口先规定文本格式，JSON 格式另行约定。
void print_tokens(const std::vector<Token>& tokens, std::ostream& output);
void print_ast(const Program& program, std::ostream& output);
void print_symbols(const SymbolTableData& symbols, std::ostream& output);
// 使用 symbols 把 %sID 解释成名字，但必须同时保留编号，避免同名变量显示混淆。
void print_ir(const IRProgram& program, const SymbolTableData& symbols,
              std::ostream& output);
// 按顺序输出阶段、级别、文件行列、中文消息和关联位置，不重复收集诊断。
void print_diagnostics(const std::vector<Diagnostic>& diagnostics,
                       std::ostream& output);

} // minic 命名空间
