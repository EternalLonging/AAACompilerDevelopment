#pragma once
#include "minic/interface.hpp"
// 用法：analyze(*syntax.root, &syntax.arena)；第二个参数仅兼容原调用，语义阶段不再分配节点。
namespace minic {

// 只接收当前解析器成功生成、未经语义分析的新树；不校验手工 AST 的结构。
// 填写类型、符号编号及 converted_type 转换目标，不改变树结构，返回符号表及诊断；同一棵树只分析一次。
// 直接调用前建立 TypeArenaScope，类型管理器须保留到结果用完。
SemanticResult analyze(Program& program, NodeArena* arena = nullptr);

// 判断两个类型是否相同。相同返回 true，不同或类型为空返回 false。
bool same_type(const TypePtr& left, const TypePtr& right);
// 计算两种算术类型的公共类型。不合法时返回 Error 类型。
// 返回的普通指针由当前 TypeArenaScope 指定的管理器保存。
TypePtr arithmetic_result(const TypePtr& left, const TypePtr& right);
// 判断来源类型能否赋给目标类型。允许返回 true，否则返回 false。
bool can_assign(const TypePtr& target, const TypePtr& source);

}
