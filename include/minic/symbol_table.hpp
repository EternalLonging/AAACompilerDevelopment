#pragma once
#include "minic/diagnostic.hpp"

namespace minic {

// 从已保存的表按作用域和光标位置查找可见名字，用于编辑器补全。
std::vector<SymbolId> visible_symbols(const SymbolTableData& data, const std::string& prefix,
                                      ScopeId scope, const SourceLocation& cursor);

// 符号表管理类，详细规则见 docs/modules.md。
class SymbolTable {
public:
    // 构造函数。建立全局作用域及空表，使用传入的诊断收集器。
    explicit SymbolTable(DiagnosticEngine& diagnostics);
    // 禁止复制符号表。
    SymbolTable(const SymbolTable&) = delete;
    // 禁止复制赋值。
    SymbolTable& operator=(const SymbolTable&) = delete;

    // 获取当前作用域编号，全局为 0。
    ScopeId current_scope() const noexcept;
    // 进入新的函数或块作用域，建立本层空表，返回作用域编号。
    ScopeId enter_scope(ScopeKind kind, const SourceRange& range);
    // 退出当前作用域，保留表中记录。成功返回 true，退出全局返回 false。
    bool exit_scope();

    // 登记普通符号，返回新建或复用的编号。冲突时返回空值并报错。
    std::optional<SymbolId> insert(SymbolEntry entry);
    // 登记有链接关系的对象，合并兼容声明；块内 extern 指向同一个全局对象。
    std::optional<SymbolId> declare_object(SymbolEntry entry);
    // 从当前作用域向外查普通名字。找到返回符号编号，未找到返回空值。
    std::optional<SymbolId> lookup(const std::string& name) const;
    // 只查当前作用域的普通名字。找到返回编号，未找到返回空值。
    std::optional<SymbolId> lookup_current(const std::string& name) const;
    // 按编号读取符号信息。编号无效时返回 nullptr。
    const SymbolEntry* symbol(SymbolId id) const noexcept;
    // 设置对象的存储偏移。成功返回 true，失败返回 false 并报错。
    bool set_symbol_offset(SymbolId id, std::size_t offset);

    // 登记结构体、联合体或枚举标签。返回记录编号，冲突时返回空值。
    std::optional<RecordId> declare_record(const std::string& tag,
                                          RecordKind kind,
                                          const SourceRange& range);
    // 补齐记录的成员和布局信息。成功返回 true，失败返回 false 并报错。
    bool complete_record(RecordId id, StructEntry definition);
    // 从当前作用域向外查标签。找到返回记录编号，未找到返回空值。
    std::optional<RecordId> lookup_tag(const std::string& tag) const;
    // 只查当前作用域的标签。找到返回记录编号，未找到返回空值。
    std::optional<RecordId> lookup_tag_current(const std::string& tag) const;
    // 按编号读取记录信息。编号无效时返回 nullptr。
    const StructEntry* record(RecordId id) const noexcept;
    // 查找记录中的成员。找到返回成员表下标，未找到返回空值。
    std::optional<std::size_t> find_member(RecordId id,
                                          const std::string& name) const;

    // 按前缀查询当前可见的普通名字，返回排序后的符号编号表。
    std::vector<SymbolId> prefix_query(const std::string& prefix) const;
    // 从指定作用域和光标位置查询前缀，返回可见的符号编号表。（重载）
    std::vector<SymbolId> prefix_query(const std::string& prefix,
                                       ScopeId scope,
                                       const SourceLocation& cursor) const;

    // 查看全部符号表数据，只读。
    const SymbolTableData& data() const noexcept;
    // 取走全部符号表数据，之后不再使用本对象。
    SymbolTableData release() &&;

private:
    // 允许内建登记函数使用本类的报错入口。
    friend bool register_builtins(SymbolTable& table);
    DiagnosticEngine& diagnostics_; // 诊断收集器，用来报告符号表错误。
    SymbolTableData data_; // 符号、标签记录、作用域和查询栈的数据。
};

// 登记 printf 和 scanf。成功返回 true，失败返回 false。
bool register_builtins(SymbolTable& table);

}
