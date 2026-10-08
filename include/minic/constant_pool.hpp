#pragma once
#include "minic/interface.hpp"

namespace minic {

// 常量池管理类，类型支持和去重规则见 docs/constant-pool.md。
class ConstantPool {
public:
    // 登记常量，返回编号。类型和值相同则复用；非法输入抛出异常。
    ConstantId intern(TypePtr type, ConstantValue value,
                      const std::string& spelling, const SourceRange& range = {});

    // 按编号查询常量。找到返回条目指针，未找到返回 nullptr。
    const ConstantEntry* get(ConstantId id) const noexcept;
    // 获取去重后的常量数量。
    std::size_t size() const noexcept;
    // 查看全部常量信息，只读。
    const ConstantPoolData& data() const noexcept;
    // 取走常量表并清空本对象，用于交给 IR 程序。
    ConstantPoolData release() &&;

private:
    ConstantPoolData data_; // 常量信息表。
    std::unordered_map<std::string, ConstantId> index_; // 常量去重索引：类型和值对应常量编号。
};

// 把常量编号转换为 %c编号；无效编号抛出异常。
std::string constant_operand(ConstantId id);

}
