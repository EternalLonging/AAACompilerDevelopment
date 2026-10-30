#include "minic/modules.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace minic;
static void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
static void execute(const std::string& source, int exit = 0, const std::string& output = "", const std::string& input = "") {
    auto compiled = compile(source, "minic-test.c");
    if (!compiled.ok()) print_diagnostics(compiled.diagnostics, std::cerr);
    require(compiled.ok(), "保留的 MiniC 功能应能编译");
    std::istringstream in(input); std::ostringstream out;
    const auto result = run(compiled.ir->program, compiled.semantic->symbols, in, out);
    if (!result.ok()) print_diagnostics(result.diagnostics, std::cerr);
    require(result.ok() && result.exit_code == exit && out.str() == output, "执行结果不符");
}
int main() {
    try {
        execute("int total=7;int get(void){return total;}int main(void){int total=99;return total+get();}", 106);
        execute("void print(char c){printf(\"%c\",c);return;}int main(void){print('A');return 0;}", 0, "A");
        execute("int sum(int a[],int n){int i=0,s=0;for(i=0;i<n;i=i+1){s=s+a[i];}return s;}int main(void){int a[3];a[0]=1;a[1]=2;a[2]=3;return sum(a,3);}", 6);
        execute("struct S{int x;char c;};int main(void){struct S s;struct S t;s.x=7;s.c='a';t=s;t.x=9;return s.x+t.x;}", 16);
        execute("int main(void){int n;float f;char c;char s[6];scanf(\"%d %f %c %s\",&n,&f,&c,s);printf(\"%d %f %c %s %%\",n,f,c,s);return 0;}", 0, "12 1.250000 A hello %", "12 1.25 A hello");
        std::cout << "MiniC 类型、数组、结构体、函数、输入输出和运行检查通过\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
