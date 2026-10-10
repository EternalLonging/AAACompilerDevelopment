#pragma once
#include <cstddef>
#include <vector>

namespace minic {
struct TypeInfo;

// 统一保存和释放类型对象；其他结构的类型指针不能单独 delete。
class TypeArena {
public:
    TypeArena() = default;
    // 释放本管理器创建的全部类型对象。
    ~TypeArena();
    TypeArena(const TypeArena&) = delete;
    TypeArena& operator=(const TypeArena&) = delete;
    // 接管另一个管理器的对象，类型对象的地址不变。
    TypeArena(TypeArena&& other) noexcept;
    // 释放原有对象，再接管另一个管理器的对象；返回本管理器。
    TypeArena& operator=(TypeArena&& other) noexcept;
    // 新建类型，返回可填写成员的普通指针；内存由本管理器释放。
    TypeInfo* create();
    // 复制类型描述，子类型指针仍借用原对象；返回新对象的普通指针。
    TypeInfo* create(const TypeInfo& type);
    // 返回本管理器拥有的类型对象数量。
    std::size_t size() const noexcept;
private:
    std::vector<TypeInfo*> objects_; // 本次创建的类型对象，析构时逐个 delete。
    // 释放保存的类型对象并清空列表。
    void clear() noexcept;
};

// 指定当前线程的类型创建目标，离开本代码块时恢复先前目标。
class TypeArenaScope {
public:
    explicit TypeArenaScope(TypeArena& arena) noexcept;
    ~TypeArenaScope();
    TypeArenaScope(const TypeArenaScope&) = delete;
    TypeArenaScope& operator=(const TypeArenaScope&) = delete;
private:
    TypeArena* previous_; // 进入前的类型管理器，退出时恢复。
};

// 在当前管理器中建立类型；没有 TypeArenaScope 时抛出异常。
TypeInfo* make_type_info();
// 在当前管理器中复制类型；子类型指针仍借用原对象。
TypeInfo* make_type_info(const TypeInfo& type);
}
