#pragma once
#include "minic/interface.hpp"

namespace minic {

// 【模块 2：语法分析；实现文件 src/parser.cpp】
// tokens：已通过词法检查的单词序列，必须恰有一个末尾 END_OF_FILE。
// 返回：Program 根节点与语法诊断，root 独占整棵树的所有权。
// 行为：递归下降建树，children 的排列必须遵循 docs/interface.md。
// 失败：恢复后继续收集诊断；只要有语法错误，最终 root 必须为空。
// 防御：输入为空、EOF 缺失/重复/位于中间时，返回语法阶段的输入格式诊断。
// 归属：AST 拥有名字和孩子，不保存 Token 元素的地址，不修改 tokens。
ParseResult parse(const std::vector<Token>& tokens);

} // minic 命名空间
