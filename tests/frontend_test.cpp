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
        auto expressions = syntax("int f(int x,int y){int a,b;a=b=9;return 8-3-1+2*4;}");
        require(expressions.ok(), "表驱动表达式解析");
        auto body = expressions.root->children[0]->children.back();
        auto assignment = body->children[2]->children[0];
        require(assignment->kind == NodeType::Assign && assignment->children[0]->name == "a" &&
                assignment->children[1]->kind == NodeType::Assign &&
                assignment->children[1]->children[0]->name == "b", "赋值必须右结合");
        auto sum = body->children.back()->children[0];
        require(sum->name == "+" && sum->children[0]->name == "-" &&
                sum->children[0]->children[0]->name == "-" && sum->children[1]->name == "*",
                "减法左结合，乘法先于加法");
        auto suffixes = syntax("struct S{int a[3],b;};int f(struct S s[],int i){return s[i].a[1];}");
        require(suffixes.ok(), "结构体数组形参和后缀序列");
        auto access = suffixes.root->children[1]->children.back()->children[0]->children[0];
        require(access->kind == NodeType::ArrayAccess && access->children[0]->kind == NodeType::MemberAccess &&
                access->children[0]->name == "a" && access->children[0]->children[0]->kind == NodeType::ArrayAccess,
                "下标和成员后缀按源码顺序组合");
        auto call = syntax("int f(){return g(1,2,3);}");
        auto called = call.root->children[0]->children.back()->children[0]->children[0];
        require(called->kind == NodeType::Call && called->children.size() == 4 &&
                called->children[0]->name == "g" && called->children[3]->name == "3", "调用的首个孩子是被调函数");
        auto optional = syntax("void f(){for(;;){break;}return;}");
        auto loop = optional.root->children[0]->children.back()->children[0];
        require(loop->children.size() == 4 && loop->children[0]->kind == NodeType::Empty &&
                loop->children[1]->kind == NodeType::Empty && loop->children[2]->kind == NodeType::Empty &&
                loop->children[3]->kind == NodeType::Block, "for 缺项仍占三个位置");
        require(loop->children[0]->range.end.offset == loop->children[0]->range.begin.offset + 1 &&
                loop->children[2]->range.end.offset == loop->children[2]->range.begin.offset + 1,
                "for 缺项保留分号或右括号的位置");
        auto parentheses = syntax("int f(){return (1+2)*3;}");
        auto product = parentheses.root->children[0]->children.back()->children[0]->children[0];
        require(product->name == "*" && product->children[0]->name == "+" &&
                product->children[0]->range.begin.offset == 15 && product->children[0]->range.end.offset == 20,
                "括号影响优先级并保留完整源码范围");
        rejects("int f(){int a,b,c;a+b=c;return 0;}");
        require(syntax("int f(){int a,b,c;(a+b)=c;return 0;}").ok(), "括号主式的左值合法性交给语义检查");
        auto first_error = compile("int f(){\na=;\nb=;\nreturn 0;}", "first_error.c");
        require(!first_error.ok() && first_error.syntax && !first_error.syntax->root &&
                first_error.diagnostics.size() == 1 && first_error.diagnostics[0].line == 2 &&
                first_error.diagnostics[0].col == 3 && first_error.diagnostics[0].code == "PARSE_EXPECTED" &&
                !first_error.semantic && !first_error.ir, "预测表空格只报告第一处错误，停止后续分析阶段");
        const std::string mismatched = "int f(){if(1]{}return 0;}";
        auto terminal_error = syntax(mismatched);
        require(!terminal_error.root && terminal_error.diagnostics.size() == 1 &&
                terminal_error.diagnostics[0].range.begin.offset == mismatched.find(']') &&
                terminal_error.diagnostics[0].message == "需要 RPAREN，实际遇到 ]",
                "终结符失配立即停止并保留错误位置和提示");
        auto bound_error = syntax("int a[0];\nint b[0];");
        require(!bound_error.root && bound_error.diagnostics.size() == 1 &&
                bound_error.diagnostics[0].line == 1 && bound_error.diagnostics[0].code == "PARSE_ARRAY_BOUND",
                "AST 完成动作出错也立即停止");
        std::string flat = "void f(){";
        for (int i = 0; i < 3000; ++i) flat += ";";
        flat += "return;}";
        auto long_list = syntax(flat);
        require(long_list.ok() && long_list.root->children[0]->children.back()->children.size() == 3001,
                "平坦语句列表不消耗 C++ 递归栈");
        rejects("int f(){return " + std::string(140, '(') + "1" + std::string(140, ')') + ";}");
        rejects("int f(){return " + std::string(140, '!') + "1;}");
        rejects("int a[0];");
        rejects("int a[2U];");
        auto stream = lex("int a;", "stream.c").tokens;
        stream[0].type = static_cast<TokenType>(10000);
        require(!parse(stream).root, "无效 Token 编号不得用于访问预测表");
        const std::string declarations = "struct S{int a,b[2];char c;} first,second[3];int x=1,y[2],z;float f(char c,int a[],struct S s[]){return 1.0;}";
        auto lists = syntax(declarations);
        require(lists.ok() && lists.root->children.size() == 7, "结构体对象与逗号声明列表");
        const auto& globals = lists.root->children;
        require(globals[0]->kind == NodeType::StructDef && globals[1]->name == "first" &&
                globals[2]->name == "second" && globals[3]->name == "x" && globals[4]->name == "y" &&
                globals[5]->name == "z" && globals[6]->name == "f", "全局声明保持源码顺序");
        require(globals[0]->children[0]->name == "a" && globals[0]->children[1]->name == "b" &&
                globals[0]->children[2]->name == "c" &&
                globals[0]->children[1]->declared_type->base->kind == TypeKind::Int &&
                globals[2]->declared_type->array_length.value() == 3, "成员顺序和数组基础类型");
        require(globals[3]->range.begin.offset == declarations.find("int x") &&
                globals[3]->range.end.offset == declarations.find(",y") &&
                globals[5]->range.end.offset == declarations.find(";float") + 1 &&
                globals[0]->range.end.offset == declarations.find("} first") + 1,
                "逗号声明和结构体定义的源码范围");
        const auto& signature = globals[6]->children;
        require(signature.size() == 4 && signature[0]->name == "c" && signature[1]->name == "a" &&
                signature[2]->name == "s" && signature[2]->declared_type->base->name == "S" &&
                globals[6]->declared_type->params[0]->kind == TypeKind::Char &&
                globals[6]->declared_type->base->kind == TypeKind::Float, "形参顺序和函数完整类型");
        auto strings = syntax("char text[]=\"a\" \"b\";");
        require(strings.ok() && strings.root->children[0]->children[0]->name == "\"a\" \"b\"" &&
                strings.root->children[0]->declared_type->base->kind == TypeKind::Char,
                "连续字符串初始化和无界字符数组");
        std::string many = "int v0";
        for (int i = 1; i < 1500; ++i) many += ",v" + std::to_string(i);
        auto variables = syntax(many + ";");
        require(variables.ok() && variables.root->children.size() == 1500 &&
                variables.root->children[749]->name == "v749" && variables.root->children.back()->name == "v1499",
                "长逗号声明列表保持顺序并完成类型");
        std::string broken = "int f(){";
        for (int i = 0; i < 30; ++i) broken += "a=;";
        auto errors = syntax(broken + "}");
        require(!errors.root && errors.diagnostics.size() == 1 &&
                errors.diagnostics[0].message.find("错误过多") == std::string::npos,
                "连续错误只返回第一条诊断，不追加错误上限提示");
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

