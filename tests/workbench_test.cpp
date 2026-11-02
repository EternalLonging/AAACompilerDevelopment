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
    if (!compiled.ok()) { std::cerr << source << '\n'; print_diagnostics(compiled.diagnostics, std::cerr); }
    require(compiled.ok(), "对比源码必须先通过语义分析");
    auto before = generate(*compiled.syntax->root, compiled.semantic->symbols, {false});
    auto after = generate(*compiled.syntax->root, compiled.semantic->symbols, {true});
    require(before.ok() && after.ok(), "两种优化选项必须生成有效四元式");
    if (fewer) require(instructions(after.program) < instructions(before.program), "优化后应实际减少纯常量运算指令");
    std::istringstream in1, in2; std::ostringstream out1, out2;
    const auto a = run(before.program, compiled.semantic->symbols, in1, out1);
    const auto b = run(after.program, compiled.semantic->symbols, in2, out2);
    if (!a.ok() || !b.ok() || out1.str() != expected || out2.str() != expected) {
        print_diagnostics(a.diagnostics, std::cerr); print_diagnostics(b.diagnostics, std::cerr);
        std::cerr << "expected: " << expected << "baseline: " << out1.str() << "folded: " << out2.str();
    }
    require(a.ok() && b.ok() && a.exit_code == b.exit_code && out1.str() == out2.str() && out1.str() == expected,
            "优化前后必须保持输出和退出码");
}
// 核对语义只写标注，IR 可反复生成，节点和符号原类型始终保留。
void conversion_annotations() {
    auto compiled = compile("float f(float x){return x;}int main(void){int a=5;float b=a/2;"
                            "f(a);return a;}", "conversion.c", CompileTarget::Parse);
    TypeArenaScope type_scope(compiled.types);
    require(compiled.ok(), "转换用例必须能解析");
    struct Shape { ASTNode* node; NodeType kind; std::vector<ASTNode*> children; };
    std::vector<Shape> shapes;
    for (auto* node : compiled.syntax->arena.nodes) shapes.push_back({node, node->kind, node->children});
    auto semantic = analyze(*compiled.syntax->root, &compiled.syntax->arena);
    require(semantic.ok(), "转换用例必须通过语义");
    require(shapes.size() == compiled.syntax->arena.nodes.size(), "语义不得分配转换节点");
    std::size_t converted = 0, original_use = 0;
    for (const auto& shape : shapes) {
        const auto& node = *shape.node;
        require(node.kind == shape.kind && node.children == shape.children && node.kind != NodeType::ImplicitCast,
                "语义不得替换原节点或改变孩子顺序");
        if (node.converted_type) {
            ++converted;
            require(node.type->kind == TypeKind::Int && node.converted_type->kind == TypeKind::Float,
                    "转换节点必须同时保留原类型和目标类型");
        }
        if (node.kind == NodeType::Identifier && node.name == "a") {
            require(semantic.symbols.symbols[node.symbol_id].type == node.type && node.type->kind == TypeKind::Int,
                    "变量绑定和符号原类型不能被转换改写");
            if (!node.converted_type) ++original_use;
        }
    }
    require(converted == 2 && original_use == 2, "只有除法结果和调用实参应标注转换");
    std::ostringstream tree_before, tree_after;
    print_ast(*compiled.syntax->root, tree_before);
    for (bool folded : {false, true})
        require(generate(*compiled.syntax->root, semantic.symbols, {folded}).ok(), "两版 IR 都应接受同一棵树");
    print_ast(*compiled.syntax->root, tree_after);
    require(tree_before.str() == tree_after.str() && shapes.size() == compiled.syntax->arena.nodes.size(),
            "生成 IR 不得修改转换标注或树结构");
    require(tree_after.str().find("int -> float") != std::string::npos, "展示必须显示原类型和转换目标");
}
int main() {
    try {
        conversion_annotations();
        compare("float g=5/2;float f(float x){return x;}float c(void){return 'A';}"
                "int main(void){int a=5;float b=a/2;printf(\"%f %f %f %f %d\\n\","
                "g,b,f(a/2),c(),'A'<65.5);return 0;}", "2.000000 2.000000 2.000000 65.000000 1\n", false);
        compare("float pair(float a,float b){return a*10+b;}int main(void){int x=1;"
                "printf(\"%f %d\\n\",pair(x,x=2),x);return 0;}", "12.000000 2\n", false);
        compare("float first(int a[]){return a[0];}int main(void){int a[1];char s[3]=\"ok\";"
                "a[0]=7;printf(\"%f %s %s\\n\",first(a),s,\"hi\");return 0;}", "7.000000 ok hi\n", false);
        compare("int main(void){float a=16777216+1;float b=16777216.0+1;"
                "float c=(16777216+1)-16777216;printf(\"%f %f %f\\n\",a,b,c);return 0;}",
                "16777216.000000 16777216.000000 1.000000\n", false);
        compare("int main(void){int x=(2+3)*(4+5);printf(\"%d\\n\",x);return 0;}", "45\n", true);
        compare("int main(void){int x=0;int y=1; if(0 && (x=x+1)){x=99;} y=y+2;printf(\"%d %d\\n\",x,y);return 0;}", "0 3\n", false);
        for (const auto* source : {"int main(void){return 1/0;}",
                                  "int main(void){float x=1/0;return 0;}",
                                  "int main(void){float x=2147483647+1;return 0;}"}) {
            auto compiled = compile(source, "invalid-constant.c", CompileTarget::Check);
            TypeArenaScope type_scope(compiled.types);
            require(compiled.ok(), "除零和溢出留到运行期诊断");
            for (bool folded : {false, true}) {
                auto ir = generate(*compiled.syntax->root, compiled.semantic->symbols, {folded});
                std::istringstream in; std::ostringstream out;
                const auto run_result = run(ir.program, compiled.semantic->symbols, in, out);
                require(!run_result.ok(), "转换和折叠不能隐藏整数除零或溢出");
            }
        }
        std::cout << "workbench: folding and equivalent execution passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
