#include "minic/interface.hpp"
#include <stdexcept>
#include <utility>

namespace minic {
namespace {
thread_local TypeArena* current_arena = nullptr; // 当前线程的类型创建目标，不跨线程共享。
}

TypeArena::~TypeArena() { clear(); }
void TypeArena::clear() noexcept {
    for (auto* type : objects_) delete type;
    objects_.clear();
}
TypeArena::TypeArena(TypeArena&& other) noexcept { objects_.swap(other.objects_); }
TypeArena& TypeArena::operator=(TypeArena&& other) noexcept {
    if (this != &other) { clear(); objects_.swap(other.objects_); }
    return *this;
}
TypeInfo* TypeArena::create() { return create(TypeInfo{}); }
TypeInfo* TypeArena::create(const TypeInfo& type) {
    auto* result = new TypeInfo(type);
    try { objects_.push_back(result); }
    catch (...) { delete result; throw; }
    return result;
}
std::size_t TypeArena::size() const noexcept { return objects_.size(); }
TypeArenaScope::TypeArenaScope(TypeArena& arena) noexcept : previous_(current_arena) { current_arena = &arena; }
TypeArenaScope::~TypeArenaScope() { current_arena = previous_; }
TypeInfo* make_type_info() {
    if (!current_arena) throw std::logic_error("创建类型前需要 TypeArena 和 TypeArenaScope");
    return current_arena->create();
}
TypeInfo* make_type_info(const TypeInfo& type) {
    if (!current_arena) throw std::logic_error("创建类型前需要 TypeArena 和 TypeArenaScope");
    return current_arena->create(type);
}
}
