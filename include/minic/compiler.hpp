#pragma once
#include "minic/interface.hpp"

namespace minic {

// 请求执行到哪个阶段；symbols/check 都需要执行到 Check，run 需要先执行到 IR。
enum class CompileTarget { Tokens, Parse, Check, IR };

// 一次编译保留的所有中间产物；optional 为空表示该阶段未执行，非空但 ok=false 表示失败。
struct CompilationResult {
    CompileTarget target = CompileTarget::IR; // 调用者请求的终止阶段。
    std::optional<LexResult> lexical; // 词法产物；源码为空也必须执行词法并产生 EOF。
    std::optional<ParseResult> syntax; // 语法产物，拥有 AST；词法失败或仅请求 Tokens 时为空。
    std::optional<SemanticResult> semantic; // 符号表与语义诊断；前阶段失败/请求 Parse 时为空。
    std::optional<IRResult> ir; // 完整中间代码；请求到 IR 且语义成功时才执行生成。
    std::vector<Diagnostic> diagnostics; // 各已执行阶段诊断的副本，按阶段和报告顺序汇总。
    // 必须执行并成功到达 target 才返回 true；未执行的默认结果不能算成功。
    bool ok() const;
};

// 【编译总控；实现文件 src/compiler.cpp】
// 按 lex -> parse -> analyze -> generate 顺序执行到 target，任一阶段失败立即停止后续阶段。
// 不读取文件、不打印内容、不运行程序；调用者负责读源码，并通过返回产物展示/执行。
// 结果拥有 AST、符号表和 IR，后续使用期间应保留整份结果；转交结果时使用 move。
CompilationResult compile(const std::string& source,
                          const std::string& filename = "<input>",
                          CompileTarget target = CompileTarget::IR);

} // minic 命名空间
