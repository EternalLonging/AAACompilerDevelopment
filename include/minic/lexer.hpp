#pragma once
#include "minic/interface.hpp"

namespace minic {

// 【模块 1：词法分析；实现文件 src/lexer.cpp】
// source：完整源码文本；filename：来源文件名，用于填写源码范围和诊断。
// 返回：Token 序列与词法诊断；即使 source 为空，序列也必须包含末尾 EOF。
// 行为：最长匹配，跳过空白/注释，保留字面量原文；非法字符报错后继续扫描。
// 失败：词法错误通过返回结果报告，不用异常表示用户源码错误。
// 归属：结果拥有所有字符串，不引用 source 的内存，不修改输入。
// 约束：每次调用独立，不依赖上次调用的全局状态。
LexResult lex(const std::string& source,
              const std::string& filename = "<input>");

} // minic 命名空间
