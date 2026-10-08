#pragma once
#include "minic/interface.hpp"

namespace minic {

// 根据已检查的语法树和符号表生成四元式，返回中间代码、常量池和诊断。
IRResult generate(const Program& program, const SymbolTableData& symbols);

}
