#include "minic/modules.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace minic;
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
bool has(const CompletionResult& result, const std::string& name) {
    return std::any_of(result.items.begin(), result.items.end(), [&](const CompletionItem& item) { return item.label == name; });
}
void completion_tests() {
    const std::string source = "int global; int main(void){int local=1;{int inner=2;return local;}return local;}";
    auto cursor = source.find("return local;") + 9;
    auto result = complete(source, cursor);
    require(has(result, "local") && !has(result, "global"), "补全应匹配前缀");
    require(result.begin == cursor - 2 && result.end == cursor, "补全应替换实际前缀范围");
    result = complete("int main(void){int score=5; sco", 30);
    require(has(result, "score") && result.recovered, "未完成输入应恢复已声明的局部名字");
    const std::string scope = "int main(void){int outside;{int inside; inside=1;}outside=2;return 0;}";
    result = complete(scope, scope.find("outside=2"));
    require(has(result, "outside") && !has(result, "inside"), "补全不能泄漏已退出作用域的名字");
    const std::string future = "int main(void){int earlier;int future;return 0;}";
    result = complete(future, future.find("int future"));
    require(has(result, "earlier") && !has(result, "future"), "补全不能引用光标后的声明");
    const std::string shadow = "int amount;int main(void){float amount;return amount;}";
    result = complete(shadow, shadow.find("return amount") + 10);
    const auto local = std::find_if(result.items.begin(), result.items.end(), [](const CompletionItem& item) { return item.label == "amount"; });
    require(local != result.items.end() && local->detail == "float", "内层同名变量应遮蔽外层并显示真实类型");
    const std::string record = "struct S{int score;int sum;};int main(void){struct S item; item.sc";
    result = complete(record, record.size());
    require(has(result, "score") && !has(result, "sum") && !has(result, "sizeof"), "成员补全应按记录和前缀匹配");
    const std::string arrow = "struct S{int score;};int main(void){struct S item;struct S *p=&item;p->sc";
    result = complete(arrow, arrow.size());
    require(has(result, "score"), "支持结构体指针成员补全");
    result = complete("int main(void){ // score", 23);
    require(result.items.empty(), "注释内不应补全");
    result = complete("int main(void){printf(\"sc", 25);
    require(result.items.empty(), "字符串内不应补全");
    result = complete("int main(void){ret", 18);
    require(has(result, "return"), "未完成语句支持关键字补全");
    const std::string unicode = "/* 中文说明 */ int main(void){int score=2; sco";
    result = complete(unicode, unicode.size());
    require(has(result, "score") && result.end == unicode.size(), "中文注释后的光标应按字节位置补全");
    const std::string ranked = "int prefix;int main(void){int project;{int prize;pr}}";
    result = complete(ranked, ranked.find("pr}}") + 2);
    require(result.items.size() == 4 && result.items[0].label == "prize" && result.items[1].label == "project" &&
            result.items[2].label == "prefix" && result.items[3].label == "printf", "前缀候选应按当前、外层、全局作用域排序");
    for (const auto& item : result.items) require(item.label.compare(0, 2, "pr") == 0, "pr 不能混入 a、main 等无关名字");
    const std::string exact = "int pr;int main(void){int project;pr}";
    result = complete(exact, exact.find("pr}") + 2);
    require(!result.items.empty() && result.items[0].label == "pr", "完全匹配应优先于内层作用域的前缀匹配");
}
std::size_t instructions(const IRProgram& ir) {
    auto count = ir.global_initializers.size();
    for (const auto& f : ir.functions) count += f.quads.size();
    return count;
}
void compare(const std::string& source, const std::string& expected, bool fewer) {
    auto compiled = compile(source, "test.c", CompileTarget::Check);
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
        completion_tests();
        compare("int main(void){int x=(2+3)*(4+5);printf(\"%d\\n\",x);return 0;}", "45\n", true);
        compare("int main(void){int x=0;int y=1; if(0 && ++x){x=99;} y+=2;printf(\"%d %d\\n\",x,y);return 0;}", "0 3\n", false);
        compare("int bump(int *p){*p+=1;return *p;}int main(void){int x=0;printf(\"%d\\n\",bump(&x));return 0;}", "1\n", false);
        const auto compiled = compile("int main(void){return 1/0;}", "zero.c", CompileTarget::Check);
        require(compiled.ok(), "除零留到运行期诊断");
        for (bool folded : {false, true}) {
            auto ir = generate(*compiled.syntax->root, compiled.semantic->symbols, {folded});
            std::istringstream in; std::ostringstream out;
            const auto run_result = run(ir.program, compiled.semantic->symbols, in, out);
            require(!run_result.ok(), "优化不能隐藏除零错误");
        }
        std::cout << "workbench: completion scopes, recovery, members, folding and equivalent execution passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
