#pragma once
#include "minic/interface.hpp"

namespace minic {

// 【模块 5：中间代码生成；实现文件 src/ir.cpp】
// program：已通过语义检查的 AST；symbols：同一次 analyze 返回的持久符号表。
// 返回：按函数组织的四元式、全局初始化、临时变量类型、入口和生成诊断。
// 行为：使用 AST 已绑定的编号，表达式后序生成；&&/|| 必须按短路控制流生成。
// 只读：不改 AST、源程序符号或已有类型；临时量登记在 IRFunction 中。
// 失败：缺少类型/编号、树形不符合合同或节点尚未支持时报告 IR 错误，不静默跳过。
// 约束：不得混用两次编译的 AST 和符号表；每次调用重置临时量与标号计数。
IRResult generate(const Program& program, const SymbolTableData& symbols);

// gen_expr、emit、new_temp、new_label 是 IR 实现内部函数，不作为跨模块公共接口。

} // minic 命名空间
