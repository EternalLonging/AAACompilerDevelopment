#include "minic/constant_pool.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace minic {
namespace {

// 将对象表示的字节加入内部键；仅在本次编译进程内使用，不作为磁盘序列化格式。
template <typename T>
void append_bytes(std::string& key, const T& value) {
    key.append(reinterpret_cast<const char*>(&value), sizeof(value));
}

bool integer_kind(TypeKind kind) {
    return kind == TypeKind::Char || kind == TypeKind::Short ||
           kind == TypeKind::Int || kind == TypeKind::Long;
}

// 只快照本池支持的合法类型，避免调用者保留可写 shared_ptr 后更改已登记的类型。
TypePtr snapshot_type(const TypePtr& type) {
    if (!type || type->base || type->array_length || !type->params.empty() ||
        type->variadic || !type->has_prototype || !type->name.empty() ||
        type->record_id != invalid_id) {
        throw std::invalid_argument("常量标量类型的字段不符合合同");
    }
    return std::make_shared<const TypeInfo>(*type);
}

void append_scalar_type(std::string& key, const TypeInfo& type) {
    append_bytes(key, type.kind);
    append_bytes(key, type.is_unsigned);
    append_bytes(key, type.is_const);
    append_bytes(key, type.is_volatile);
}

} // 内部辅助函数不作为跨模块接口。

ConstantId ConstantPool::intern(TypePtr type, ConstantValue value,
                                const std::string& spelling,
                                const SourceRange& range) {
    if (!type) throw std::invalid_argument("常量类型不能为空");
    std::string key;
    if (integer_kind(type->kind)) {
        type = snapshot_type(type);
        append_scalar_type(key, *type);
        if (type->is_unsigned) {
            const auto number = std::get_if<std::uint64_t>(&value);
            if (!number) throw std::invalid_argument("无符号整数常量需要 uint64_t 值");
            append_bytes(key, *number);
        } else {
            const auto number = std::get_if<std::int64_t>(&value);
            if (!number) throw std::invalid_argument("有符号整数常量需要 int64_t 值");
            append_bytes(key, *number);
        }
    } else if (type->kind == TypeKind::Float || type->kind == TypeKind::Double) {
        type = snapshot_type(type);
        if (type->is_unsigned) throw std::invalid_argument("浮点类型不能带 unsigned");
        auto number = std::get_if<double>(&value);
        if (!number || !std::isfinite(*number)) {
            throw std::invalid_argument("浮点常量需要有限的 double 值");
        }
        append_scalar_type(key, *type);
        if (type->kind == TypeKind::Float) {
            // 先判断范围，避免把超出 float 范围的值直接窄化转换。
            if (std::fabs(*number) > std::numeric_limits<float>::max()) {
                throw std::invalid_argument("浮点常量超出 float 可表示范围");
            }
            const float rounded = static_cast<float>(*number);
            *number = static_cast<double>(rounded);
        }
        // 不做 == 或近似比较：正负零必须分开，不同浮点位表示不能误合并。
        append_bytes(key, *number);
    } else if (type->kind == TypeKind::Array) {
        const auto bytes = std::get_if<std::string>(&value);
        if (!bytes || !type->base || type->base->kind != TypeKind::Char ||
            type->is_unsigned || !type->array_length ||
            *type->array_length == 0 || *type->array_length - 1 != bytes->size() ||
            !type->params.empty() || type->variadic || !type->has_prototype ||
            !type->name.empty() || type->record_id != invalid_id) {
            throw std::invalid_argument("字符串常量需要长度包含终止零的窄 char[N] 类型");
        }
        const auto base = snapshot_type(type->base);
        if (base->is_unsigned) throw std::invalid_argument("字符串元素必须为普通 char");
        auto copy = std::make_shared<TypeInfo>(*type);
        copy->base = base;
        type = std::move(copy);
        append_scalar_type(key, *type);
        append_bytes(key, *type->array_length);
        append_scalar_type(key, *type->base);
        key.append(*bytes); // std::string 按长度追加，内容中的零字节不会被截断。
    } else {
        throw std::invalid_argument("该常量类型尚未支持，不能丢失类型或精度后入池");
    }

    const auto found = index_.find(key);
    if (found != index_.end()) return found->second;
    if (data_.entries.size() >= invalid_id) {
        throw std::length_error("常量编号已耗尽");
    }
    const auto id = static_cast<ConstantId>(data_.entries.size());
    data_.entries.push_back({id, std::move(type), std::move(value), spelling, range});
    try {
        index_.emplace(std::move(key), id);
    } catch (...) {
        data_.entries.pop_back(); // 索引分配失败时不留下没有去重索引的半条记录。
        throw;
    }
    return id;
}

const ConstantEntry* ConstantPool::get(ConstantId id) const noexcept {
    return id < data_.entries.size() ? &data_.entries[id] : nullptr;
}

std::size_t ConstantPool::size() const noexcept { return data_.entries.size(); }

const ConstantPoolData& ConstantPool::data() const noexcept { return data_; }

ConstantPoolData ConstantPool::release() && {
    ConstantPoolData result;
    result.entries.swap(data_.entries);
    index_.clear();
    return result;
}

std::string constant_operand(ConstantId id) {
    if (id == invalid_id) throw std::invalid_argument("无效常量编号不能生成操作数");
    return "%c" + std::to_string(id);
}

} // minic 命名空间
