#include "minic/modules.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace minic;
static unsigned checks = 0;
static void require(bool value, const std::string& message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
static ParseResult syntax(const std::string& source) {
    auto lexical = lex(source, "test.c"); require(lexical.ok(), "测试源码应通过词法");
    return parse(lexical.tokens);
}
static void execute(const std::string& source, int expected, const std::string& output = "",
                    const std::string& input = "", bool preprocessing = false) {
    auto compiled = preprocessing ? compile_preprocessed(source, "test.c") : compile(source, "test.c");
    if (!compiled.ok()) { print_diagnostics(compiled.diagnostics, std::cerr); throw std::runtime_error("真实源码编译失败：" + source); }
    std::istringstream in(input); std::ostringstream out;
    auto execution = run(compiled.ir->program, compiled.semantic->symbols, in, out);
    if (!execution.ok()) print_diagnostics(execution.diagnostics, std::cerr);
    require(execution.ok(), "真实源码运行失败");
    require(execution.exit_code == expected, "返回值错误：" + source);
    require(out.str() == output, "输出错误：" + out.str());
}
static void rejects(const std::string& source, const std::string& code = "") {
    auto result = syntax(source);
    require(!result.ok() && !result.root, "非法语法必须返回空根：" + source);
    if (!code.empty()) {
        bool found = false;
        for (const auto& d : result.diagnostics) if (d.code == code) found = true;
        require(found, "缺少诊断 " + code);
    }
}
int main() {
    try {
        auto empty = lex("", "empty.c");
        require(empty.ok() && empty.tokens.size() == 1 && empty.tokens[0].lexeme.empty(), "空源码 EOF");
        require(parse(empty.tokens).ok(), "空翻译单元");
        require(!parse({}).ok(), "缺少 EOF");
        auto bad_stream = empty.tokens; bad_stream.push_back(empty.tokens[0]);
        require(!parse(bad_stream).ok(), "重复 EOF");
        auto lexical = lex("int int32 main <<= 2; 077 0xffUL .5e+2f 'a' L\"wide\" \"中文\"", "tokens.c");
        require(lexical.ok(), "所有词法类别");
        require(lexical.tokens[0].type == TokenType::KW_INT && lexical.tokens[1].type == TokenType::ID &&
                lexical.tokens[3].type == TokenType::SHIFT_LEFT_ASSIGN, "关键字边界和最长匹配");
        auto locations = lex("a/*x\r\ny*/\tb\r\nc", "loc.c");
        require(locations.ok() && locations.tokens[1].line == 2 && locations.tokens[1].col == 5 &&
                locations.tokens[2].line == 3 && locations.tokens[2].range.begin.offset == 13, "CRLF 和注释位置");
        auto utf8 = lex("\"中文\" x");
        require(utf8.tokens[1].col == 10 && utf8.tokens[1].range.begin.offset == 9, "UTF-8 字节列");
        for (const auto& source : {"08", "0x", "1UU", "1e+", "0x1.fp3", "\"\\q\"", "''", "'\\x'", "/* unfinished", "\"unfinished\nint x;", "@"}) {
            auto bad = lex(source);
            require(!bad.ok() && bad.tokens.back().type == TokenType::END_OF_FILE, "词法错误恢复");
        }
        auto limit = lex(std::string(30, '@'));
        require(limit.diagnostics.size() == 20 && limit.diagnostics.back().message.find("错误过多") != std::string::npos, "词法诊断上限");
        auto recovering = syntax("int main(void){ x = ; y = ; return 0; }");
        require(!recovering.root && recovering.diagnostics.size() == 2, "块内多错误恢复");
        auto tree = syntax("int *a[3]; int (*b)[3]; int (*f)(int); int main(void){ for(;;){break;} return 0; }");
        require(tree.ok(), "复杂声明符");
        const auto& ds = tree.root->children;
        require(ds[0]->declared_type->kind == TypeKind::Array && ds[0]->declared_type->base->kind == TypeKind::Pointer, "指针数组");
        require(ds[1]->declared_type->kind == TypeKind::Pointer && ds[1]->declared_type->base->kind == TypeKind::Array, "数组指针");
        require(ds[2]->declared_type->base->kind == TypeKind::Function && ds[2]->declared_type->base->params.size() == 1, "函数指针");
        const auto& loop = *ds[3]->children.back()->children[0];
        require(loop.kind == NodeType::For && loop.children.size() == 4 && loop.children[0]->kind == NodeType::Empty &&
                loop.children[1]->kind == NodeType::Empty && loop.children[2]->kind == NodeType::Empty, "for 可选孩子顺序");
        require(ds[3]->range.file == "test.c" && ds[3]->range.end.offset == 82, "函数源码范围");
        rejects("int main(void){ if(1) return 0; }", "PARSE_EXPECTED");
        rejects("int main(void){ if(1){} else if(0){} }", "PARSE_EXPECTED");
        rejects("int main(void){ for(int i=0;i<3;i++){} }", "PARSE_FOR_DECLARATION");
        rejects("int main(void){int x=0;x=1;int y=2;return x+y;}", "PARSE_DECLARATION_ORDER");
        rejects("int main(void){ a+b=3; }", "PARSE_ASSIGNMENT_TARGET");
        rejects("int main(void){ int a[2+3]; }", "PARSE_EXPECTED");
        rejects("int int x;", "PARSE_SPECIFIERS");
        rejects("long long x;", "PARSE_SPECIFIERS");
        rejects("struct S{int x:2;};", "PARSE_MEMBER");
        rejects("int main(int){ return 0; }", "PARSE_PARAMETER");
        rejects("int x, f(void){return 0;}", "PARSE_FUNCTION");
        rejects("typedef struct S{int x;};", "PARSE_DECLARATOR");
        rejects("struct S{struct T{int x;} t;};", "PARSE_MEMBER");
        rejects("int f(a) int a; {return a;}");
        std::string many_errors = "int main(void){";
        for (int i = 0; i < 30; ++i) many_errors += "x=;";
        auto error_limit = syntax(many_errors + "}");
        require(!error_limit.root && error_limit.diagnostics.size() == 20, "语法诊断上限");
        rejects("int main(void){ return " + std::string(150, '(') + "1" + std::string(150, ')') + "; }", "PARSE_DEPTH");
        std::string chain = "int main(void){return 1";
        for (int i = 0; i < 150; ++i) chain += "+1";
        rejects(chain + ";}", "PARSE_DEPTH");
        execute("int main(void){return 2+3*4-8/2;}", 10);
        execute("int main(void){int a=0,b=0; a=b=7; return a+b;}", 14);
        execute("int main(void){return 8-3-1;}", 4);
        execute("int main(void){return 1 ? 0 ? 2 : 3 : 4;}", 3);
        execute("int main(void){int x=1; return (x=2,x+3);}", 5);
        execute("int main(void){int x=1; (x)=4; return x;}", 4);
        execute("int main(void){int x=1; return x++ + ++x;}", 4);
        execute("int main(void){return (3<<2) | (7&3) ^ 1;}", 14);
        execute("int main(void){int a[2][3]={{1,2,3},{4,5,6}}; return a[1][2];}", 6);
        execute("int sum(int a[],int n){int i=0,s=0;for(i=0;i<n;i++){s+=a[i];}return s;} int main(void){int a[]={1,2,3}; return sum(a,3);}", 6);
        execute("struct S{char c;int x;}; int main(void){struct S s={'a',9};struct S *p=&s; p->x+=1; return s.x+sizeof(struct S);}", 18);
        execute("int main(void){struct {int x;} s={4};return s.x;}", 4);
        execute("union U{int x;char c;}; int main(void){union U u={65};return u.c;}", 65);
        execute("enum E{A=3,B,C=8};int main(void){enum E e=B;return e+C;}", 12);
        execute("typedef int T; int main(void){T x=2;T y=3;{int T=5; x+=T;}return (T)(x+y);}", 10);
        execute("typedef int T; int f(int T){return T+1;} int main(void){return f(4);}", 5);
        execute("typedef int T; int main(void){int x=0; T: x=3;return x;}", 3);
        execute("struct S; struct S *p; struct S{int x;}; int main(void){struct S s={7};p=&s;return p->x;}", 7);
        execute("int f(int x);int f(int x){return x+2;}int main(void){return f(3);}", 5);
        execute("int inc(int x){return x+1;} int call(int (*f)(int),int x){return f(x);}int main(void){int (*f)(int)=inc;return call(f,5);}", 6);
        execute("int fact(int n){if(n<=1){return 1;}else{return n*fact(n-1);}}int main(void){return fact(5);}", 120);
        execute("int main(void){int x=0; do{x++;}while(x<3); switch(x){case 2:x=9;break;case 3:x+=2;default:x+=1;} return x;}", 6);
        execute("int main(void){int i=0,s=0;while(i<5){i++;if(i==2){continue;}s+=i;if(i==4){break;}}return s;}", 8);
        execute("int main(void){int x=0; goto done;x=5;done:return x;}", 0);
        execute("int f(void){static int x=0;return ++x;}int main(void){return f()+f();}", 3);
        execute("extern int x; int x=3;int main(void){return x;}", 3);
        execute("int main(void){int x=0; if(0 && (x=3)){x=4;}return x;}", 0);
        execute("int main(void){double d=(double)3; unsigned int u=0xffffffffU; return (int)d+(int)(u>>31);}", 4);
        execute("int main(void){int n=0,s=0,i=0;scanf(\"%d\",&n);for(i=1;i<=n;i++){s+=i;}printf(\"sum = %d\\n\",s);return 0;}", 0, "sum = 15\n", "5");
        execute("int main(void){printf(\"a\" \"b\\n\");return 0;}", 0, "ab\n");
        execute("int main(void){char s[]=\"\\x4\" \"1\";return sizeof(s);}", 3);
        execute("int main(void){printf(\"a\\\"\" \"b\");return 0;}", 0, "a\"b");
        execute("#define TWICE(x) ((x)+(x))\nint main(void){return TWICE(4);}\n", 8, "", "", true);
        auto headers = compile_preprocessed("#include \"defs.h\"\nint main(void){return N;}\n", "main.c", CompileTarget::IR,
            [](const std::string& name, const std::string&) -> std::optional<std::string> {
                if (name == "defs.h") return "#define N 7\n";
                return std::nullopt;
            });
        require(headers.ok(), "真实头文件和宏链路");
        auto semantic_error = compile("int main(void){(1+2)=4;return 0;}");
        require(!semantic_error.ok() && semantic_error.syntax->ok() && !semantic_error.ir, "括号赋值目标由语义检查");
        auto lexical_error = compile("int main(void){return 08;}");
        require(!lexical_error.ok() && !lexical_error.syntax, "词法失败停止后续阶段");
        auto syntax_error = compile("int main(void){return ;");
        require(!syntax_error.ok() && !syntax_error.semantic, "语法失败停止后续阶段");
        std::cout << "frontend: " << checks << " checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
