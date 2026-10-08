#pragma once
#include "minic/interface.hpp"
#include <istream>
#include <ostream>

namespace minic {

// 程序执行限制。
struct RunOptions {
    std::uint64_t max_steps = 1'000'000; // 最多执行的指令数；0 表示不限。
    std::size_t max_call_depth = 1024; // 最多允许的函数调用层数；0 表示不限。
};

// 程序执行结果。
struct RunResult {
    std::optional<std::int32_t> exit_code; // main 的退出码；执行失败时为空。
    std::uint64_t executed_steps = 0; // 实际执行的指令数。
    std::vector<Diagnostic> diagnostics; // 程序执行时的诊断列表。
    // 判断程序是否正常执行完毕。正常返回 true，否则返回 false。
    bool ok() const { return exit_code.has_value() && !has_errors(diagnostics); }
};

// 执行中间代码，使用传入的输入输出流，返回退出码和运行诊断。
RunResult run(const IRProgram& program, const SymbolTableData& symbols,
              std::istream& input, std::ostream& output,
              const RunOptions& options = {});

}
