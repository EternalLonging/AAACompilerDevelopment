#pragma once
#include "minic/diagnostic.hpp"

namespace minic {

// 【模块 3 的符号表部分；实现文件 src/symbol_table.cpp】
// 语义遍历通过本类维护表；IR 只读取最终的 SymbolTableData。
class SymbolTable {
public:
    // 自动创建 id=0 的全局作用域和 active_scopes={0}，不自动登记内建函数。
    // diagnostics 必须属于 Semantic 阶段，且比本符号表活得更久。
    explicit SymbolTable(DiagnosticEngine& diagnostics);
    SymbolTable(const SymbolTable&) = delete;
    SymbolTable& operator=(const SymbolTable&) = delete;

    // 返回当前活动作用域编号，刚创建时为 0。
    ScopeId current_scope() const noexcept;
    // 在当前层之下新建 Function/Block 作用域并压栈，返回永久编号。
    // kind=Global 属于调用错误，抛 invalid_argument；range 为作用域源码范围。
    ScopeId enter_scope(ScopeKind kind, const SourceRange& range);
    // 弹出当前层但保留条目；若试图退出全局，报告 Fatal 并返回 false。
    bool exit_scope();

    // 在当前层登记声明，自动填写 id/scope/line/col，输入的这些字段不用于分配身份。
    // 新声明返回新编号；合法的函数原型/定义重声明复用旧编号；冲突返回 nullopt 并报错。
    // M1 局部对象同层重复声明、函数重复定义或不兼容签名均为错误。
    std::optional<SymbolId> insert(SymbolEntry entry);
    // 从当前层向外查找普通名字；没找到返回 nullopt，不自动报错。
    std::optional<SymbolId> lookup(const std::string& name) const;
    // 只查当前层，主要用于重复声明检查，忽略外层同名声明。
    std::optional<SymbolId> lookup_current(const std::string& name) const;
    // 通过编号读取符号；编号非法返回 nullptr；修改表后不得继续使用旧元素指针。
    const SymbolEntry* symbol(SymbolId id) const noexcept;
    // 为已登记对象设置存储布局偏移；非法编号/非对象符号返回 false 并报告错误。
    bool set_symbol_offset(SymbolId id, std::size_t offset);

    // 在当前层登记未完整的标签并分配 RecordId；同层同种标签复用编号。
    // 标签为空表示匿名类型，分配编号但不写入 tags；同层不同种标签冲突则报错。
    std::optional<RecordId> declare_record(const std::string& tag,
                                          RecordKind kind,
                                          const SourceRange& range);
    // 填入已声明记录的完整定义；definition 的 id/tag/kind/scope 必须与条目一致。
    // 定义必须带已检查成员/枚举项、大小和对齐；函数校验后置 is_complete=true。
    // 重复定义、身份不符、重复成员或不完整布局返回 false 并报错，不覆盖旧条目。
    bool complete_record(RecordId id, StructEntry definition);
    // 从当前层向外查标签；与普通名字查询独立，没找到不自动报错。
    std::optional<RecordId> lookup_tag(const std::string& tag) const;
    // 只查询当前层标签，用于判断声明是否会遮蔽外层标签。
    std::optional<RecordId> lookup_tag_current(const std::string& tag) const;
    // 通过编号只读访问记录；非法编号返回 nullptr，指针不能跨表修改保存。
    const StructEntry* record(RecordId id) const noexcept;
    // 查询完整 struct/union 的成员，返回 members 下标；未找到/不完整/编号非法返回空值。
    std::optional<std::size_t> find_member(RecordId id,
                                          const std::string& name) const;

    // 当前活动环境中的可见普通名字前缀查询：内层优先去重，再按名字排序返回编号。
    // prefix 为空时返回所有可见普通符号；不包含标签和成员。
    std::vector<SymbolId> prefix_query(const std::string& prefix) const;
    // 编译结束后从指定作用域沿 parent 查询；排除 cursor 之后才首次声明的名字。
    // cursor 使用原始字节偏移，作用域与光标必须属于同一源文件；非法作用域返回空列表。
    std::vector<SymbolId> prefix_query(const std::string& prefix,
                                       ScopeId scope,
                                       const SourceLocation& cursor) const;

    // 只读查看全部持久数据，不能借此绕过插入和作用域规则。
    const SymbolTableData& data() const noexcept;
    // 语义结束后移动出全部数据；仅允许 std::move(table).release()，之后不再使用本对象。
    SymbolTableData release() &&;

private:
    // 内建登记函数需通过同一个收集器报告调用环境或签名冲突，不能另建报错通道。
    friend bool register_builtins(SymbolTable& table);
    DiagnosticEngine& diagnostics_; // 借用语义诊断收集器，符号表不拥有它。
    SymbolTableData data_; // 本次编译拥有的符号、记录类型、作用域及活动栈。
};

// 在全局作用域登记 printf/scanf；重复调用不制造重复内建条目。
// 签名冲突或非全局调用通过表的诊断收集器报告，返回 false。
bool register_builtins(SymbolTable& table);

} // minic 命名空间
