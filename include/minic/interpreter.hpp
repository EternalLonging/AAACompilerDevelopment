#pragma once
#include "minic/interface.hpp"
#include <istream>
#include <ostream>

namespace minic {

// 执行配置；限制用于识别无限循环和无限递归，不改变编译阶段的语言规则。
struct RunOptions {
    std::uint64_t max_steps = 1'000'000; // 总指令步数上限，包含初始化和被调函数；0 表示不限制。
    std::size_t max_call_depth = 1024; // 用户函数调用帧上限，包含 main；0 表示不限制。
};

// 执行结果；程序本身返回非零退出码不等于解释器发生错误。
struct RunResult {
    std::optional<std::int32_t> exit_code; // main 正常结束时的退出码；运行失败时为空。
    std::uint64_t executed_steps = 0; // 实际执行的四元式数量，失败时也保留已执行数量。
    std::vector<Diagnostic> diagnostics; // 运行时诊断，例如除零、缺入口、输入错误或超限。
    bool ok() const { return exit_code.has_value() && !has_errors(diagnostics); }
};

// 【四元式解释器；实现文件 src/interpreter.cpp】
// program/symbols：同一次成功编译的 IR 与类型/符号信息，只读且调用期间必须有效。
// input/output：程序 scanf/printf 使用的输入输出流；由调用者拥有，函数不关闭流。
// options：步数及调用深度限制；每次 run 都重建全局存储、调用帧及待传参队列。
// 流程：全局初始化 -> 入口 main -> 函数调用；M1 要求 main 为无参数 int 函数。
// 失败：验证 IR 后再执行；缺失入口、未知指令、无效编号等产生 Runtime 诊断。
// 错误时保留已经产生的输出，exit_code 为空，不承诺回滚程序已做的输入输出。
RunResult run(const IRProgram& program, const SymbolTableData& symbols,
              std::istream& input, std::ostream& output,
              const RunOptions& options = {});

} // minic 命名空间
