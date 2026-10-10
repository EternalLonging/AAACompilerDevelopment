#pragma once
#include "minic/interface.hpp"

namespace minic {

struct IRGenerationOptions {
    bool constant_folding = true; // 是否把安全的纯常量运算预先算成常量。
};

// 根据已检查的语法树和符号表生成四元式，返回中间代码、常量池和诊断。
// 直接调用前建立 TypeArenaScope，类型管理器须保留到 IR 用完。
IRResult generate(const Program& program, const SymbolTableData& symbols);
// 使用指定优化选项生成四元式；不修改已分析的语法树。
IRResult generate(const Program& program, const SymbolTableData& symbols,
                  const IRGenerationOptions& options);

}
