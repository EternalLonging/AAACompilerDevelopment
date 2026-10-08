#pragma once
#include "minic/interface.hpp"

namespace minic {

// 【模块 3 的出错处理部分；实现文件 src/diagnostic.cpp】
// 每个编译阶段创建自己的收集器，不使用全局诊断列表。
class DiagnosticEngine {
public:
    // phase：本收集器固定所属阶段；max_errors：错误/致命错误上限，必须至少为 1。
    // 默认上限为需求规定的 20 条；max_errors=0 属于调用错误，抛 invalid_argument。
    explicit DiagnosticEngine(Phase phase, std::size_t max_errors = 20);

    // 记录一条诊断，自动使用固定 phase，并从 range.begin 同步 line/col。
    // message：中文消息；code：稳定代码；related：之前的声明等关联位置。
    // 达到上限的最后一条错误附加“错误过多，停止本阶段”，保留其原始消息和代码。
    // 达限后忽略后续报告；Fatal 不论是否达限，都要求立即停止该阶段。
    void report(Level level, const SourceRange& range,
                const std::string& message, const std::string& code = "",
                const std::vector<SourceRange>& related = {});

    // 本阶段已记录的 Error/Fatal 数量，不包含 Note/Warning。
    std::size_t error_count() const noexcept;
    // 是否已达到错误上限，或已记录 Fatal；循环应检查此值并结束本阶段。
    bool should_stop() const noexcept;
    // 只读访问诊断列表；返回引用不能比本对象活得更久。
    const std::vector<Diagnostic>& diagnostics() const noexcept;
    // 取走诊断列表，清空计数和停止标记；阶段结束后用它填写阶段结果。
    std::vector<Diagnostic> take_diagnostics();

private:
    Phase phase_; // 本收集器固定对应的编译阶段。
    std::size_t max_errors_; // 允许记录的错误/致命错误数量上限。
    std::size_t error_count_ = 0; // 当前已记录的错误/致命错误数量。
    bool fatal_seen_ = false; // 是否出现致命错误，出现后要求立即停止。
    std::vector<Diagnostic> diagnostics_; // 依报告顺序保存的诊断条目。
};

} // minic 命名空间
