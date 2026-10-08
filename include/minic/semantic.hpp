#pragma once
#include "minic/interface.hpp"

namespace minic {

// 【模块 4：语义分析；实现文件 src/semantic.cpp】
// program：语法成功产生的 Program 根；本函数独立创建诊断收集器和符号表。
// 行为：预登记内建函数；维护作用域；检查声明、引用、赋值、调用、成员、下标和 return。
// 修改：填入 AST 的 type/category/symbol_id/scope_id 等，并显式插入 ImplicitCast。
// 返回：持久符号表及语义诊断；表中所有记录在离开作用域后仍保留。
// 失败：保留部分标注和表供展示，不交给 IR；kind 非 Program 则返回 Fatal 诊断。
// 约束：只对一棵新解析的树调用一次；重新分析应先重新 parse，避免叠加转换节点。
SemanticResult analyze(Program& program);

// 类型规则由语义模块统一提供，其他模块不得各自写一套类型矩阵。
// 递归判断两个已解析类型是否相同，包含限定符、函数签名和记录身份；空指针返回 false。
bool same_type(const TypePtr& left, const TypePtr& right);
// M1 算术公共类型：char 先提升为 int，int/int 得 int，含 float 得 float。
// 空指针、Error/Unknown 或非算术类型返回 kind=Error 的非空类型，调用者负责报错。
TypePtr arithmetic_result(const TypePtr& left, const TypePtr& right);
// M1 赋值/固定形参传参规则：相同基本类型、char->int、int/char->float 可接受。
// 其他组合为 false；更完整的 C 转换在后续里程碑统一扩展，左值/const 检查另做。
bool can_assign(const TypePtr& target, const TypePtr& source);

} // minic 命名空间
