#pragma once
#include "minic/interface.hpp"
#include "minic/preprocessor.hpp"

namespace minic {

// 编译终止阶段：单词、语法树、语义检查或中间代码。
enum class CompileTarget { Tokens, Parse, Check, IR };

// 一次编译的结果，各阶段未执行时对应项为空。
struct CompilationResult {
    TypeArena types; // 保存本次编译的类型对象，其他结果销毁后再统一释放。
    CompileTarget target = CompileTarget::IR; // 请求编译到哪个阶段。
    std::optional<LexResult> lexical; // 词法分析结果；未执行时为空。
    std::optional<ParseResult> syntax; // 语法分析结果；未执行时为空。
    std::optional<SemanticResult> semantic; // 语义分析结果；未执行时为空。
    std::optional<IRResult> ir; // 中间代码生成结果；未执行时为空。
    std::vector<Diagnostic> diagnostics; // 所有已执行阶段的诊断汇总。
    std::optional<PreprocessResult> preprocessing; // 预处理结果；直接调用 compile 时为空。
    // 判断是否成功编译到请求阶段。成功返回 true，否则返回 false。
    bool ok() const;
};

// 执行编译到指定阶段，返回所有中间结果和诊断；出错后停止后续阶段。
CompilationResult compile(const std::string& source,
                          const std::string& filename = "<input>",
                          CompileTarget target = CompileTarget::IR);

// 先展开宏再编译，错误位置映射到原文件行号；不支持 #include。
// 展开后的列号和字节偏移不能精确还原，映射时列号置 1、偏移置 0。
CompilationResult compile_preprocessed(const std::string& source,
                                      const std::string& filename = "<input>",
                                      CompileTarget target = CompileTarget::IR);

}
