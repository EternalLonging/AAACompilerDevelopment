#pragma once
#include "minic/interface.hpp"

namespace minic {

// 检查语法树的语义，填写类型和符号编号，返回符号表及诊断。
SemanticResult analyze(Program& program);

// 判断两个类型是否相同。相同返回 true，不同或类型为空返回 false。
bool same_type(const TypePtr& left, const TypePtr& right);
// 计算两种算术类型的公共类型。不合法时返回 Error 类型。
TypePtr arithmetic_result(const TypePtr& left, const TypePtr& right);
// 判断来源类型能否赋给目标类型。允许返回 true，否则返回 false。
bool can_assign(const TypePtr& target, const TypePtr& source);

}
