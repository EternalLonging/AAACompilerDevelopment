#include "minic/modules.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace minic;
static void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
// 编译并执行完整源码，核对返回值、输出或运行错误。
static void execute(const std::string& source, int exit = 0,
                    const std::string& output = "", const std::string& input = "",
                    const std::string& error = "") {
    auto compiled = compile(source, "minic-test.c");
    if (!compiled.ok()) print_diagnostics(compiled.diagnostics, std::cerr);
    require(compiled.ok(), "保留的 MiniC 功能应能编译");
    std::istringstream in(input); std::ostringstream out;
    const auto result = run(compiled.ir->program, compiled.semantic->symbols, in, out);
    if (error.empty()) {
        if (!result.ok()) print_diagnostics(result.diagnostics, std::cerr);
        require(result.ok() && result.exit_code == exit && out.str() == output, "执行结果不符");
    } else {
        require(!result.ok() && result.diagnostics[0].message.find(error) != std::string::npos,
                "应报告运行错误：" + error);
    }
}
// 编译范围外的源码，核对错误代码并确认后续阶段没有执行。
static void rejects(const std::string& source, const std::string& code) {
    auto result = compile(source, "minic-test.c");
    bool found = false;
    for (const auto& diagnostic : result.diagnostics) if (diagnostic.code == code) found = true;
    if (!found) print_diagnostics(result.diagnostics, std::cerr);
    require(!result.ok() && found && !result.ir, "缺少范围拒绝诊断：" + code);
}
int main() {
    try {
        execute("int total=7;int get(){return total;}int main(){int total=99;return total+get();}", 106);
        execute("void print(char c){printf(\"%c\",c);return;}int main(){print('A');return 0;}", 0, "A");
        execute("int main(){int n;float f;char c;char s[6];scanf(\"%d %f %c %s\",&n,&f,&c,s);printf(\"%d %f %c %s %%\",n,f,c,s);return 0;}",
                0, "12 1.250000 A hello %", "12 1.25 A hello");
        execute("struct S{int x;char c;};int main(){struct S s={7,'a'};struct S t;t=s;t.x=9;return s.x+t.x;}",16);
        execute("int sum(int a[],int n){int i=0,s=0;for(i=0;i<n;i=i+1){s=s+a[i];}return s;}int main(){int a[]={1,2,3};return sum(a,3);}",6);
        execute("struct S{int x;};int main(){int a[2]={0,0};struct S s={0};scanf(\"%d %d\",&a[1],&s.x);return a[1]+s.x;}",7,"","3 4");
        execute("int main(){char s[3];scanf(\"%s\",s);return 0;}",0,"","abcd","容量");
        execute("int main(){char s[2]=\"ab\";printf(\"%s\",s);return 0;}",0,"","","终止零");
        execute("int main(){int x=2147483647;return x+1;}",0,"","","范围");
        execute("int main(){int a[2]={1,2};return a[2];}",0,"","","越界");
        execute("int main(){int x;return x;}",0,"","","初始化");
        execute("int main(){int x=0;return 1/x;}",0,"","","除零");
        for (const auto* type : {"short", "long", "double", "signed", "unsigned", "volatile"})
            rejects(std::string(type)+" x;int main(){return 0;}","PARSE_TYPE_REMOVED");
        for (const auto* value : {"1U", "1L", "1.0L"})
            rejects(std::string("int main(){return ")+value+";}","PARSE_TYPE_REMOVED");
        for (const auto* expression : {"x++", "--x", "~x", "x<<1", "x>>1", "x&1", "x|1", "x^1", "x?1:2", "sizeof(x)", "(float)x"})
            rejects(std::string("int main(){int x=1;return ")+expression+";}","PARSE_OPERATOR_REMOVED");
        for (const auto* op : {"+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="})
            rejects(std::string("int main(){int x=1;x")+op+"1;return x;}","PARSE_COMPOUND_ASSIGNMENT");
        rejects("int main(){int x=1;return (x=2,x);}","PARSE_EXPECTED");
        rejects("int a[2][2];", "PARSE_ARRAY_DIMENSION");
        rejects("int f(int a[][2]){return 0;}", "PARSE_ARRAY_DIMENSION");
        rejects("int f(int x);", "PARSE_FUNCTION_DECL_REMOVED");
        rejects("struct S;", "PARSE_RECORD_FORWARD");
        rejects("struct {int x;} s;", "PARSE_RECORD_ANONYMOUS");
        rejects("struct S{int x;};int f(struct S s){return s.x;}", "PARSE_FUNCTION_REMOVED");
        rejects("struct S{int x;};struct S f(){struct S s;return s;}", "PARSE_FUNCTION_REMOVED");
        rejects("int f(int x,...){return x;}", "PARSE_FUNCTION_REMOVED");
        for (const auto* format : {"%u", "%x", "%o", "%ld", "%lf", "%4s", "%.2f"})
            rejects(std::string("int main(){printf(\"")+format+"\",1);return 0;}","SEM_FORMAT");
        std::cout << "MiniC 类型、数组、结构体、函数、输入输出和报错检查通过\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
