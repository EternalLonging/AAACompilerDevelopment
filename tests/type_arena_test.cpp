#include "minic/modules.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

using namespace minic;
static_assert(std::is_same_v<TypePtr, const TypeInfo*>);
static_assert(!std::is_copy_constructible_v<TypeArena>);
static_assert(std::is_nothrow_move_constructible_v<TypeArena>);

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void arena_tests() {
    bool rejected = false;
    try { make_type_info(); } catch (const std::logic_error&) { rejected = true; }
    require(rejected, "没有管理器时不能建立无人释放的类型");
    TypeArena outer, inner;
    TypePtr integer = nullptr, pointer = nullptr;
    {
        TypeArenaScope scope(outer);
        auto* value = make_type_info(); value->kind = TypeKind::Int;
        integer = value;
        auto* indirect = make_type_info(); indirect->kind = TypeKind::Pointer; indirect->base = integer;
        pointer = indirect;
        {
            TypeArenaScope nested(inner);
            make_type_info()->kind = TypeKind::Char;
        }
        make_type_info();
        require(outer.size() == 3 && inner.size() == 1, "内层退出后应恢复外层管理器");
        for (int i = 0; i < 4096; ++i) make_type_info();
        require(pointer->base == integer && integer->kind == TypeKind::Int, "扩容不能改变类型对象地址");
        auto* cyclic = make_type_info(); cyclic->kind = TypeKind::Pointer; cyclic->base = cyclic;
        require(!same_type(cyclic, cyclic), "循环类型应被拒绝，释放时不能递归删除子指针");
    }
    TypeArena moved(std::move(outer));
    require(outer.size() == 0 && pointer->base == integer, "移动构造应转交对象并保留地址");
    TypeArena assigned;
    assigned.create();
    assigned = std::move(moved);
    require(moved.size() == 0 && integer->kind == TypeKind::Int, "移动赋值应清理旧对象并接管新对象");
}

void comparison_tests() {
    // 类型比较只借用对象，不要求活动的类型管理器。
    TypeInfo integer; integer.kind = TypeKind::Int;
    TypeInfo qualified = integer; qualified.is_const = true;
    TypeInfo array; array.kind = TypeKind::Array; array.base = &integer; array.array_length = 3;
    TypeInfo pointer; pointer.kind = TypeKind::Pointer; pointer.base = &integer;
    TypeInfo left; left.kind = TypeKind::Function; left.base = &integer; left.params = {&array, &qualified};
    TypeInfo right = left; right.params = {&pointer, &integer};
    require(same_type(&left, &right), "函数形参比较应处理数组退化和顶层限定符");
    right.params[0] = nullptr;
    require(!same_type(&left, &right), "空形参类型应被拒绝");
}

void compilation_tests() {
    std::vector<CompilationResult> results;
    for (int i = 0; i < 16; ++i) {
        auto result = compile("int inc(int x){return x+1;} int main(void){int a[2]={3,5};"
                              "int (*f)(int)=inc;return f(a[1]);}");
        require(result.ok() && result.types.size() > 0, "编译结果必须持有类型对象");
        results.push_back(std::move(result));
    }
    auto replacement = compile("int main(void){return 1;}");
    replacement = std::move(results[0]);
    require(results[0].types.size() == 0, "移动后原结果不能再拥有类型对象");
    for (std::size_t i = 1; i < results.size(); ++i) {
        std::istringstream input; std::ostringstream output;
        const auto result = run(results[i].ir->program, results[i].semantic->symbols, input, output);
        require(result.ok() && result.exit_code == 6, "多次编译和容器移动不能使已有结果失效");
    }
    std::istringstream input; std::ostringstream output;
    auto execution = run(replacement.ir->program, replacement.semantic->symbols, input, output);
    require(execution.ok() && execution.exit_code == 6, "移动赋值后应仍可执行");
    {
        TypeArenaScope scope(replacement.types);
        auto check = compile("int main(void){return 0;}", "nested.c", CompileTarget::Check);
        const auto count = replacement.types.size();
        make_type_info();
        require(replacement.types.size() == count + 1, "嵌套编译结束后应恢复调用者的类型管理器");
        auto regenerated = generate(*replacement.syntax->root, replacement.semantic->symbols, {false});
        require(regenerated.ok(), "已有编译结果应支持再次生成中间代码");
    }
}

int main() {
    try {
        arena_tests();
        comparison_tests();
        compilation_tests();
        std::cout << "type_arena: raw pointers, growth, scopes, ownership moves and repeated compilation passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
