#pragma once
#include "minic/interface.hpp"

namespace minic {

// 诊断管理类，用于收集错误和警告。
class DiagnosticEngine {
public:
    // 构造函数。指定编译阶段和错误上限，默认最多 20 条错误。
    explicit DiagnosticEngine(Phase phase, std::size_t max_errors = 20);

    // 记录一条诊断，自动填写阶段和行列；达到错误上限后停止收集。
    void report(Level level, const SourceRange& range,
                const std::string& message, const std::string& code = "",
                const std::vector<SourceRange>& related = {});

    // 获取错误和致命错误的数量，不计警告。
    std::size_t error_count() const noexcept;
    // 判断是否需要停止本阶段：错误已达上限或出现致命错误时返回 true。
    bool should_stop() const noexcept;
    // 查看已收集的诊断，只读。
    const std::vector<Diagnostic>& diagnostics() const noexcept;
    // 取走诊断列表，同时清空计数和停止标记。
    std::vector<Diagnostic> take_diagnostics();

private:
    Phase phase_; // 所属编译阶段。
    std::size_t max_errors_; // 允许记录的错误数量上限。
    std::size_t error_count_ = 0; // 已记录的错误数量。
    bool fatal_seen_ = false; // 是否出现过致命错误。
    std::vector<Diagnostic> diagnostics_; // 诊断信息列表。
};

}
