#include "minic/modules.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace minic;
static unsigned checks = 0;
static void require(bool value, const std::string& message) { ++checks; if (!value) throw std::runtime_error(message); }
static ParseResult syntax(const std::string& source) {
    auto lexical = lex(source, "test.c"); if (!lexical.ok()) { print_diagnostics(lexical.diagnostics, std::cerr); throw std::runtime_error("测试源码应通过词法：" + source); } return parse(lexical.tokens);
}
static void execute(const std::string& source, int expected, const std::string& output = "", const std::string& input = "") {
    auto compiled = compile(source, "test.c");
    if (!compiled.ok()) { print_diagnostics(compiled.diagnostics, std::cerr); throw std::runtime_error("真实源码编译失败"); }
    std::istringstream in(input); std::ostringstream out;
    auto execution = run(compiled.ir->program, compiled.semantic->symbols, in, out);
    require(execution.ok() && execution.exit_code == expected && out.str() == output, "真实源码运行失败");
}
static void rejects(const std::string& source) {
    auto lexical = lex(source, "test.c");
    if (!lexical.ok()) return;
    auto result = parse(lexical.tokens);
    require(!result.ok() && !result.root, "非法语法必须返回空根");
}
int main() {
    TypeArena types; TypeArenaScope type_scope(types);
    try {
        auto empty = lex("", "empty.c");
        require(empty.ok() && empty.tokens.size() == 1 && empty.tokens[0].type == TokenType::END_OF_FILE, "空源码 EOF");
        require(parse(empty.tokens).ok(), "空翻译单元");
        auto lexical = lex("int main(void){return 1+2*3;}", "tokens.c");
        require(lexical.ok() && lexical.tokens[0].type == TokenType::KW_INT && lexical.tokens.back().type == TokenType::END_OF_FILE, "基本词法类别");
        auto locations = lex("a/*x\r\ny*/\tb\r\nc", "loc.c");
        require(locations.ok() && locations.tokens[1].line == 2 && locations.tokens[2].line == 3, "CRLF 和注释位置");
        auto tree = syntax("int a[3]; int main(void){for(;;){break;}return 0;}");
        require(tree.ok() && tree.root->children.size() == 2, "数组与函数定义");
        require(tree.root->children[0]->declared_type->kind == TypeKind::Array, "一维数组");
        execute("int main(void){return 2+3*4-8/2;}", 10);
        execute("int main(void){int a=0,b=0;a=b=7;return a+b;}", 14);
        execute("int sum(int a[],int n){int i=0,s=0;for(i=0;i<n;i=i+1){s=s+a[i];}return s;} int main(void){int a[3];a[0]=1;a[1]=2;a[2]=3;return sum(a,3);}", 6);
        execute("struct S{int x;};int main(void){struct S s;s.x=8;return s.x;}", 8);
        execute("int main(void){int i=0,s=0;while(i<5){i=i+1;if(i==2){continue;}s=s+i;if(i==4){break;}}return s;}", 8);
        rejects("int *p;");
        rejects("int main(void){if(1)return 0;}");
        rejects("int main(void){int x=0;x=1;int y=2;return x+y;}");
        rejects("int main(void){return 1?2:3;}");
        auto lexical_error = compile("int main(void){return 08;}");
        require(!lexical_error.ok() && !lexical_error.syntax, "词法失败停止后续阶段");
        std::cout << "frontend: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

