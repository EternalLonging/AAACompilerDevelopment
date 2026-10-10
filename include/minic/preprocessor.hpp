#pragma once
#include "minic/interface.hpp"

namespace minic {

struct PreprocessedLine {
    std::string file; // 原始文件名。
    std::uint32_t line = 1; // 原始行号。
};
struct PreprocessResult {
    std::string source; // 展开宏后的源码。
    std::vector<PreprocessedLine> lines; // 输出每一行对应的原始位置。
    std::vector<Diagnostic> diagnostics; // 预处理错误和警告。
    bool ok() const { return !has_errors(diagnostics); }
};
// 展开对象宏、函数宏和条件指令，返回源码、行映射及诊断；不支持 #include。
PreprocessResult preprocess(const std::string& source, const std::string& filename = "<input>");

}
