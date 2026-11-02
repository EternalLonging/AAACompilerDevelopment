#include "minic/modules.hpp"

#include <iostream>
#include <stdexcept>

using namespace minic;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// 用真实源码检查：结构合法的 AST 仍须拒绝名字、类型和上下文错误。
static void rejects(const std::string& source, const std::string& code) {
    auto result = compile(source, "semantic-test.c", CompileTarget::Check);
    require(result.syntax && result.syntax->ok(), "语义用例须先通过词法和语法");
    require(result.semantic && !result.semantic->ok() && !result.ir, "语义错误不能进入 IR");
    for (const auto& diagnostic : result.semantic->diagnostics)
        if (diagnostic.code == code) return;
    print_diagnostics(result.diagnostics, std::cerr);
    throw std::runtime_error("缺少预期的语义诊断：" + code);
}

int main() {
    try {
        rejects("int main(void){return missing;}", "SEM_UNDECLARED");
        rejects("int main(void){int a;int a;return 0;}", "SYM_DECL_CONFLICT");
        rejects("int main(void){int a=1.5;return 0;}", "SEM_INIT_TYPE");
        rejects("int main(void){int a;a=1.5;return 0;}", "SEM_ASSIGN_TYPE");
        rejects("int main(void){int a[2];a=1;return 0;}", "SEM_LVALUE");
        rejects("int main(void){return 1.5%2;}", "SEM_OPERANDS");
        rejects("int main(void){int a[2];return a[1.5];}", "SEM_INDEX");
        rejects("struct S{int x;};int main(void){struct S s;return s.missing;}", "SEM_MEMBER");
        rejects("struct S{int x;};int main(void){struct S s;if(s){return 1;}return 0;}", "SEM_CONDITION");
        rejects("int main(void){break;return 0;}", "SEM_LOOP");
        rejects("int main(void){continue;return 0;}", "SEM_LOOP");
        rejects("int main(void){return 1.5;}", "SEM_RETURN");
        rejects("int main(void){int a=0;}", "SEM_MISSING_RETURN");
        rejects("int f(int a){return a;}int main(void){return f();}", "SEM_ARGUMENT_COUNT");
        rejects("int f(int a){return a;}int main(void){return f(1.5);}", "SEM_ARGUMENT_TYPE");
        rejects("int main(void){int f=1;return f();}", "SEM_CALL");
        rejects("int main(void){printf(\"%f\",1);return 0;}", "SEM_FORMAT");
        rejects("int main(void){int a;scanf(\"%d\",a);return 0;}", "SEM_FORMAT");
        rejects("int main(void){int a;return &a;}", "SEM_ADDRESS");
        rejects("int a=1;int b=a;int main(void){return 0;}", "SEM_GLOBAL_INIT");
        rejects("int main(void){char s[2]=\"abc\";return 0;}", "SEM_INIT_COUNT");
        rejects("int main(void){return 2147483648;}", "SEM_LITERAL");
        rejects("int main(void){struct Missing s;return 0;}", "SEM_TAG");
        rejects("int f(struct Missing a[]){return 0;}int main(void){return 0;}", "SEM_TAG");

        // 参数只解析一次；保留数组传参、隐式转换和递归绑定。
        auto result = compile(
            "float sum(int a[],int n){if(n==0){return 0;}return a[n-1]+sum(a,n-1);}"
            "int main(void){int a[2];float total;a[0]=2;a[1]=3;total=sum(a,2);"
            "printf(\"%f\",total);return 0;}");
        if (!result.ok()) print_diagnostics(result.diagnostics, std::cerr);
        require(result.ok(), "数组参数、递归和数值转换须能生成 IR");
        const auto& function = *result.syntax->root->children[0];
        require(function.type->params[0]->kind == TypeKind::Address &&
                function.type->params[0] == function.children[0]->type,
                "形参节点和函数签名须使用同一份解析结果");
        require(!result.ir->program.functions.empty(), "须生成函数四元式");
        std::cout << "解析器 AST 的语义拒绝、数组参数、递归及转换检查通过\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
