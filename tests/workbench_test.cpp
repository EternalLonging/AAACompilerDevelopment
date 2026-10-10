#include "minic/modules.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace minic;
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
std::size_t instructions(const IRProgram& ir) {
    auto count = ir.global_initializers.size();
    for (const auto& f : ir.functions) count += f.quads.size();
    return count;
}
void compare(const std::string& source, const std::string& expected, bool fewer) {
    auto compiled = compile(source, "test.c", CompileTarget::Check);
    TypeArenaScope type_scope(compiled.types);
    require(compiled.ok(), "对比源码必须先通过语义分析");
    auto before = generate(*compiled.syntax->root, compiled.semantic->symbols, {false});
    auto after = generate(*compiled.syntax->root, compiled.semantic->symbols, {true});
    require(before.ok() && after.ok(), "两种优化选项必须生成有效四元式");
    if (fewer) require(instructions(after.program) < instructions(before.program), "优化后应实际减少纯常量运算指令");
    std::istringstream in1, in2; std::ostringstream out1, out2;
    const auto a = run(before.program, compiled.semantic->symbols, in1, out1);
    const auto b = run(after.program, compiled.semantic->symbols, in2, out2);
    require(a.ok() && b.ok() && a.exit_code == b.exit_code && out1.str() == out2.str() && out1.str() == expected,
            "优化前后必须保持输出和退出码");
}
int main() {
    try {
        compare("int main(void){int x=(2+3)*(4+5);printf(\"%d\\n\",x);return 0;}", "45\n", true);
        compare("int main(void){int x=0;int y=1; if(0 && ++x){x=99;} y+=2;printf(\"%d %d\\n\",x,y);return 0;}", "0 3\n", false);
        auto compiled = compile("int main(void){return 1/0;}", "zero.c", CompileTarget::Check);
        TypeArenaScope type_scope(compiled.types);
        require(compiled.ok(), "除零留到运行期诊断");
        for (bool folded : {false, true}) {
            auto ir = generate(*compiled.syntax->root, compiled.semantic->symbols, {folded});
            std::istringstream in; std::ostringstream out;
            const auto run_result = run(ir.program, compiled.semantic->symbols, in, out);
            require(!run_result.ok(), "优化不能隐藏除零错误");
        }
        std::cout << "workbench: folding and equivalent execution passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
