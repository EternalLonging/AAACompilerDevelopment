#include "minic/symbol_table.hpp"
#include "minic/semantic.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace minic {
namespace {

// 编号不能占用 invalid_id。
template <typename T>
std::uint32_t next_id(const std::vector<T>& entries) {
    if (entries.size() >= invalid_id) throw std::length_error("符号表编号已用完");
    return static_cast<std::uint32_t>(entries.size());
}

bool object_kind(SymbolKind kind) {
    return kind == SymbolKind::Variable || kind == SymbolKind::Parameter ||
           kind == SymbolKind::Array;
}

// 成员必须是可存储的类型，数组必须有长度。
bool member_type(const TypePtr& type, const SymbolTableData& data) {
    if (!type || !same_type(type, type)) return false;
    switch (type->kind) {
    case TypeKind::Void: case TypeKind::Function:
        return false;
    case TypeKind::Array:
        return type->array_length && *type->array_length > 0 &&
               member_type(type->base, data);
    case TypeKind::Struct: case TypeKind::Union: case TypeKind::Enum:
        return type->record_id < data.records.size() &&
               data.records[type->record_id].is_complete &&
               ((type->kind == TypeKind::Struct && data.records[type->record_id].kind == RecordKind::Struct) ||
                (type->kind == TypeKind::Union && data.records[type->record_id].kind == RecordKind::Union) ||
                (type->kind == TypeKind::Enum && data.records[type->record_id].kind == RecordKind::Enum));
    default:
        return true;
    }
}

// 收集可见名字，先过滤光标后的声明，再处理同名遮蔽。
std::vector<SymbolId> collect_prefix(const SymbolTableData& data,
                                     const std::string& prefix, ScopeId scope,
                                     const SourceLocation* cursor) {
    std::unordered_set<std::string> seen;
    std::vector<SymbolId> result;
    while (scope != invalid_id && scope < data.scopes.size()) {
        for (const auto& item : data.scopes[scope].symbols) {
            const auto& entry = data.symbols[item.second];
            if (cursor && entry.builtin == BuiltinKind::None &&
                entry.range.begin.offset > cursor->offset) continue;
            if (seen.insert(item.first).second && item.first.compare(0, prefix.size(), prefix) == 0)
                result.push_back(item.second);
        }
        scope = data.scopes[scope].parent;
    }
    std::sort(result.begin(), result.end(), [&data](SymbolId left, SymbolId right) {
        return data.symbols[left].name < data.symbols[right].name;
    });
    return result;
}

}

SymbolTable::SymbolTable(DiagnosticEngine& diagnostics) : diagnostics_(diagnostics) {
    ScopeEntry global;
    global.id = 0;
    global.kind = ScopeKind::Global;
    data_.scopes.push_back(std::move(global));
    data_.active_scopes.push_back(0);
}

ScopeId SymbolTable::current_scope() const noexcept { return data_.active_scopes.back(); }

ScopeId SymbolTable::enter_scope(ScopeKind kind, const SourceRange& range) {
    if (kind == ScopeKind::Global) throw std::invalid_argument("不能再次建立全局作用域");
    ScopeEntry entry;
    entry.id = next_id(data_.scopes);
    entry.parent = current_scope();
    entry.kind = kind;
    entry.range = range;
    const auto id = entry.id;
    data_.scopes.push_back(std::move(entry));
    try { data_.active_scopes.push_back(id); }
    catch (...) { data_.scopes.pop_back(); throw; }
    return id;
}

bool SymbolTable::exit_scope() {
    if (data_.active_scopes.size() == 1) {
        diagnostics_.report(Level::Fatal, data_.scopes[0].range,
                            "不能退出全局作用域", "SYM_GLOBAL_EXIT");
        return false;
    }
    data_.active_scopes.pop_back();
    return true;
}

std::optional<SymbolId> SymbolTable::insert(SymbolEntry entry) {
    if (entry.name.empty() || !same_type(entry.type, entry.type) ||
        (entry.kind == SymbolKind::Function && entry.type->kind != TypeKind::Function) ||
        (object_kind(entry.kind) && (entry.type->kind == TypeKind::Void ||
                                   entry.type->kind == TypeKind::Function))) {
        diagnostics_.report(Level::Error, entry.range, "符号名字或类型无效", "SYM_INVALID_ENTRY");
        return std::nullopt;
    }
    auto& names = data_.scopes[current_scope()].symbols;
    const auto found = names.find(entry.name);
    if (found != names.end()) {
        auto& previous = data_.symbols[found->second];
        const bool linkage_conflict =
            (entry.storage == StorageClass::Static && previous.storage != StorageClass::Static) ||
            (entry.storage == StorageClass::Extern && previous.storage == StorageClass::Static);
        if (previous.kind == SymbolKind::Function && entry.kind == SymbolKind::Function &&
            same_type(previous.type, entry.type) && !linkage_conflict &&
            !(previous.is_defined && entry.is_defined) &&
            !(previous.builtin != BuiltinKind::None && entry.is_defined) &&
            (entry.builtin == BuiltinKind::None || entry.builtin == previous.builtin)) {
            previous.is_defined = previous.is_defined || entry.is_defined;
            return previous.id;
        }
        diagnostics_.report(Level::Error, entry.range,
                            "同一作用域中的声明冲突：" + entry.name,
                            "SYM_DECL_CONFLICT", {previous.range});
        return std::nullopt;
    }
    entry.id = next_id(data_.symbols);
    entry.scope = current_scope();
    entry.line = entry.range.begin.line;
    entry.col = entry.range.begin.col;
    const auto id = entry.id;
    data_.symbols.push_back(std::move(entry));
    try { names.emplace(data_.symbols.back().name, id); }
    catch (...) { data_.symbols.pop_back(); throw; }
    return id;
}

std::optional<SymbolId> SymbolTable::lookup(const std::string& name) const {
    for (auto it = data_.active_scopes.rbegin(); it != data_.active_scopes.rend(); ++it) {
        const auto& names = data_.scopes[*it].symbols;
        const auto found = names.find(name);
        if (found != names.end()) return found->second;
    }
    return std::nullopt;
}

std::optional<SymbolId> SymbolTable::declare_object(SymbolEntry entry) {
    const auto scope = current_scope();
    if (scope != 0 && entry.storage != StorageClass::Extern) return insert(std::move(entry));
    auto& global = data_.scopes[0].symbols;
    const auto found = global.find(entry.name);
    SymbolId id;
    if (found != global.end()) {
        auto& previous = data_.symbols[found->second];
        if (!object_kind(previous.kind) || !same_type(previous.type, entry.type) ||
            (previous.is_defined && entry.is_defined) ||
            (entry.storage == StorageClass::Static && previous.storage != StorageClass::Static) ||
            (previous.storage == StorageClass::Static && entry.storage == StorageClass::None)) {
            diagnostics_.report(Level::Error, entry.range, "对象声明类型、链接或定义冲突：" + entry.name, "SYM_DECL_CONFLICT", {previous.range}); return std::nullopt;
        }
        previous.is_defined = previous.is_defined || entry.is_defined;
        if (entry.storage != StorageClass::Extern) previous.storage = entry.storage;
        id = previous.id;
    } else {
        // 暂时切到全局登记，之后恢复调用者的活动作用域。
        data_.active_scopes.push_back(0);
        const auto created = insert(std::move(entry));
        data_.active_scopes.pop_back();
        if (!created) return std::nullopt;
        id = *created;
    }
    if (scope != 0) {
        const auto local = data_.scopes[scope].symbols.find(data_.symbols[id].name);
        if (local != data_.scopes[scope].symbols.end() && local->second != id) {
            diagnostics_.report(Level::Error, data_.symbols[id].range, "extern 与本层名字冲突", "SYM_DECL_CONFLICT"); return std::nullopt;
        }
        data_.scopes[scope].symbols[data_.symbols[id].name] = id;
    }
    return id;
}

std::optional<SymbolId> SymbolTable::lookup_current(const std::string& name) const {
    const auto& names = data_.scopes[current_scope()].symbols;
    const auto found = names.find(name);
    return found == names.end() ? std::nullopt : std::optional<SymbolId>(found->second);
}

const SymbolEntry* SymbolTable::symbol(SymbolId id) const noexcept {
    return id < data_.symbols.size() ? &data_.symbols[id] : nullptr;
}

bool SymbolTable::set_symbol_offset(SymbolId id, std::size_t offset) {
    const auto* entry = symbol(id);
    if (!entry || !object_kind(entry->kind)) {
        diagnostics_.report(Level::Error, entry ? entry->range : SourceRange{},
                            "只有有效的对象符号才能设置存储偏移", "SYM_INVALID_OFFSET");
        return false;
    }
    data_.symbols[id].offset = offset;
    return true;
}

std::optional<RecordId> SymbolTable::declare_record(const std::string& tag, RecordKind kind,
                                                   const SourceRange& range) {
    auto& tags = data_.scopes[current_scope()].tags;
    if (!tag.empty()) {
        const auto found = tags.find(tag);
        if (found != tags.end()) {
            const auto& previous = data_.records[found->second];
            if (previous.kind == kind) return previous.id;
            diagnostics_.report(Level::Error, range, "标签种类冲突：" + tag,
                                "SYM_TAG_CONFLICT", {previous.range});
            return std::nullopt;
        }
    }
    StructEntry entry;
    entry.id = next_id(data_.records);
    entry.tag = tag;
    entry.kind = kind;
    entry.scope = current_scope();
    entry.range = range;
    const auto id = entry.id;
    data_.records.push_back(std::move(entry));
    if (!tag.empty()) {
        try { tags.emplace(tag, id); }
        catch (...) { data_.records.pop_back(); throw; }
    }
    return id;
}

bool SymbolTable::complete_record(RecordId id, StructEntry definition) {
    const auto* previous = record(id);
    const auto fail = [&](const std::string& message) {
        diagnostics_.report(Level::Error, definition.range, message, "SYM_RECORD_INVALID",
                            previous ? std::vector<SourceRange>{previous->range} : std::vector<SourceRange>{});
        return false;
    };
    if (!previous || definition.id != id || definition.tag != previous->tag ||
        definition.kind != previous->kind || definition.scope != previous->scope)
        return fail("记录编号、标签或作用域不一致");
    if (previous->is_complete) return fail("记录已经定义：" + previous->tag);
    if (!definition.size || !definition.alignment || *definition.size == 0 ||
        *definition.alignment == 0 ||
        (*definition.alignment & (*definition.alignment - 1)) != 0 ||
        *definition.size % *definition.alignment != 0)
        return fail("记录缺少有效的大小或对齐信息");
    std::unordered_set<std::string> names;
    if (definition.kind == RecordKind::Enum) {
        if (!definition.members.empty() || definition.enumerators.empty())
            return fail("枚举定义必须包含枚举项，不能包含成员");
        for (const auto& value : definition.enumerators) {
            const auto* entry = symbol(value.symbol_id);
            if (value.name.empty() || !names.insert(value.name).second || !entry ||
                entry->kind != SymbolKind::EnumConstant || entry->name != value.name ||
                entry->scope != definition.scope)
                return fail("枚举项重复或尚未登记到普通符号表");
        }
    } else {
        if (!definition.enumerators.empty() || definition.members.empty())
            return fail("结构体和联合体必须包含成员，不能包含枚举项");
        for (const auto& member : definition.members) {
            if (member.name.empty() || !names.insert(member.name).second)
                return fail("成员名字为空或重复：" + member.name);
            if (!member_type(member.type, data_) || !member.offset ||
                *member.offset >= *definition.size ||
                (definition.kind == RecordKind::Union && *member.offset != 0))
                return fail("成员类型或偏移无效：" + member.name);
        }
    }
    definition.is_complete = true;
    data_.records[id] = std::move(definition);
    return true;
}

std::optional<RecordId> SymbolTable::lookup_tag(const std::string& tag) const {
    for (auto it = data_.active_scopes.rbegin(); it != data_.active_scopes.rend(); ++it) {
        const auto& tags = data_.scopes[*it].tags;
        const auto found = tags.find(tag);
        if (found != tags.end()) return found->second;
    }
    return std::nullopt;
}

std::optional<RecordId> SymbolTable::lookup_tag_current(const std::string& tag) const {
    const auto& tags = data_.scopes[current_scope()].tags;
    const auto found = tags.find(tag);
    return found == tags.end() ? std::nullopt : std::optional<RecordId>(found->second);
}

const StructEntry* SymbolTable::record(RecordId id) const noexcept {
    return id < data_.records.size() ? &data_.records[id] : nullptr;
}

std::optional<std::size_t> SymbolTable::find_member(RecordId id, const std::string& name) const {
    const auto* entry = record(id);
    if (!entry || !entry->is_complete || entry->kind == RecordKind::Enum) return std::nullopt;
    for (std::size_t i = 0; i < entry->members.size(); ++i)
        if (entry->members[i].name == name) return i;
    return std::nullopt;
}

std::vector<SymbolId> SymbolTable::prefix_query(const std::string& prefix) const {
    return collect_prefix(data_, prefix, current_scope(), nullptr);
}

std::vector<SymbolId> SymbolTable::prefix_query(const std::string& prefix, ScopeId scope,
                                               const SourceLocation& cursor) const {
    return collect_prefix(data_, prefix, scope, &cursor);
}

const SymbolTableData& SymbolTable::data() const noexcept { return data_; }
SymbolTableData SymbolTable::release() && { return std::move(data_); }

bool register_builtins(SymbolTable& table) {
    if (table.current_scope() != 0) {
        table.diagnostics_.report(Level::Error, table.data_.scopes[table.current_scope()].range,
                                  "内建函数必须登记在全局作用域", "SYM_BUILTIN_SCOPE");
        return false;
    }
    auto integer = std::make_shared<TypeInfo>();
    integer->kind = TypeKind::Int;
    auto character = std::make_shared<TypeInfo>();
    character->kind = TypeKind::Char;
    character->is_const = true;
    auto pointer = std::make_shared<TypeInfo>();
    pointer->kind = TypeKind::Pointer;
    pointer->base = character;
    auto function = std::make_shared<TypeInfo>();
    function->kind = TypeKind::Function;
    function->base = integer;
    function->params = {pointer};
    function->variadic = true;
    const std::pair<const char*, BuiltinKind> builtins[] = {
        {"printf", BuiltinKind::Printf}, {"scanf", BuiltinKind::Scanf}
    };
    // 先检查全部名字，避免名字冲突时只登记了一半。
    for (const auto& builtin : builtins) {
        const auto id = table.lookup_current(builtin.first);
        if (id) {
            const auto* entry = table.symbol(*id);
            if (entry->kind != SymbolKind::Function ||
                (entry->builtin != BuiltinKind::None && entry->builtin != builtin.second) ||
                entry->storage == StorageClass::Static || entry->is_defined ||
                !same_type(entry->type, function)) {
                table.diagnostics_.report(Level::Error, entry->range,
                                          "内建函数名字已被占用：" + entry->name,
                                          "SYM_BUILTIN_CONFLICT");
                return false;
            }
        }
    }
    for (const auto& builtin : builtins) {
        if (const auto id = table.lookup_current(builtin.first)) {
            table.data_.symbols[*id].builtin = builtin.second;
            continue;
        }
        SymbolEntry entry;
        entry.name = builtin.first;
        entry.kind = SymbolKind::Function;
        entry.type = function;
        entry.builtin = builtin.second;
        entry.range.file = "<builtin>";
        if (!table.insert(std::move(entry))) return false;
    }
    return true;
}

}
