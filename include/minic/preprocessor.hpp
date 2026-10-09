#pragma once
#include "minic/interface.hpp"
#include <functional>

namespace minic {

struct PreprocessedLine {
    std::string file; // 原始文件名。
    std::uint32_t line = 1; // 原始行号。
};
struct PreprocessResult {
    std::string source; // 展开宏和头文件后的源码。
    std::vector<PreprocessedLine> lines; // 输出每一行对应的原始位置。
    std::vector<Diagnostic> diagnostics; // 预处理错误和警告。
    bool ok() const { return !has_errors(diagnostics); }
};
// 调用者负责读取头文件；找不到时返回空值。参数为头文件名和包含它的文件名。
using IncludeLoader = std::function<std::optional<std::string>(const std::string&, const std::string&)>;

// 将头文件名和包含者文件名转换为实际路径，用于读取、嵌套包含和错误定位。
// 不提供时保留原行为，直接使用 include 中写出的名字。
using IncludePathResolver = std::function<std::string(const std::string&, const std::string&)>;

// 展开对象宏、函数宏、条件指令和头文件，返回源码、行映射及诊断。
PreprocessResult preprocess(const std::string& source, const std::string& filename = "<input>",
                            const IncludeLoader& loader = {}, const IncludePathResolver& resolver = {});

}
