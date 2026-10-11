#include "minic/diagnostic.hpp"

#include <stdexcept>
#include <utility>

namespace minic {

DiagnosticEngine::DiagnosticEngine(Phase phase, std::size_t max_errors)
    : phase_(phase), max_errors_(max_errors) {
    if (max_errors == 0) throw std::invalid_argument("错误上限必须大于 0");
}

void DiagnosticEngine::report(Level level, const SourceRange& range,
                              const std::string& message, const std::string& code,
                              const std::vector<SourceRange>& related) {
    if (should_stop()) return;
    const bool error = level == Level::Error || level == Level::Fatal;
    Diagnostic entry;
    entry.phase = phase_;
    entry.level = level;
    entry.line = range.begin.line;
    entry.col = range.begin.col;
    entry.message = message;
    entry.code = code;
    entry.range = range;
    entry.related = related;
    if (error && error_count_ + 1 == max_errors_)
        entry.message += "；错误过多，停止本阶段";
    diagnostics_.push_back(std::move(entry));
    if (error) ++error_count_;
    if (level == Level::Fatal) fatal_seen_ = true;
}

std::size_t DiagnosticEngine::error_count() const noexcept { return error_count_; }

bool DiagnosticEngine::should_stop() const noexcept {
    return fatal_seen_ || error_count_ >= max_errors_;
}

const std::vector<Diagnostic>& DiagnosticEngine::diagnostics() const noexcept {
    return diagnostics_;
}

std::vector<Diagnostic> DiagnosticEngine::take_diagnostics() {
    auto result = std::move(diagnostics_);
    diagnostics_.clear();
    error_count_ = 0;
    fatal_seen_ = false;
    return result;
}

}
