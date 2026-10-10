#include "minic/preprocessor.hpp"
#include <iostream>
#include <stdexcept>

using namespace minic;
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void rejects(const std::string& source, const std::string& message) {
    const auto result = preprocess(source, "bad.c");
    require(!result.ok() && result.diagnostics.size() == 1 && result.diagnostics[0].phase == Phase::Preprocess &&
            result.diagnostics[0].message.find(message) != std::string::npos, "应报告明确的预处理错误");
}
int main() {
    try {
        auto result = preprocess("#define N 3\n#define ADD(a,b) ((a)+(b))\nADD(N,ADD(1,2))\n\"N #\" 'N'\n#define HASH \"#\"\nHASH\n");
        require(result.ok() && result.source.find("((3)+(((1)+(2))))") != std::string::npos &&
                result.source.find("\"N #\" 'N'") != std::string::npos && result.source.find("\"#\"") != std::string::npos, "宏应展开且保护字面量");
        result = preprocess("#define X 1 + \\\n2\nX /* comment\n comment */ + 3 // end\n");
        require(result.ok() && result.source.find("1 + 2") != std::string::npos && result.source.find("comment") == std::string::npos && result.lines[1].line == 3, "续行、注释及行映射错误");
        result = preprocess("#define X 1\n#if defined(X) && !UNSET\nyes\n#elif 1\nno\n#else\nno\n#endif\n#undef X\n#ifndef X\nagain\n#endif\n");
        require(result.ok() && result.source.find("yes") != std::string::npos && result.source.find("again") != std::string::npos && result.source.find("no") == std::string::npos, "条件分支选择错误");
        result = preprocess("#define N 3\n#if N * 2 >= 6 && ((1 << 3) == 8) && (1 || 1/0) && (0 ? 1/0 : 7)\narithmetic\n#endif\n");
        require(result.ok() && result.source.find("arithmetic") != std::string::npos, "条件算术、优先级和短路错误");
        rejects("#if 1/0\n#endif\n", "除零");
        rejects("#if 2147483647 + 1\n#endif\n", "32 位");
        result = preprocess("#define X X\n#define Y Z\n#define Z Y\nX Y\n");
        require(result.ok() && result.source.find("X Y") != std::string::npos, "递归宏应停止重新展开");
        result = preprocess("#define xFF 7\n0xFF\n");
        require(result.ok() && result.source.find("0xFF") != std::string::npos, "数字中的名字不能展开宏");
        rejects("#include \"missing.h\"\n", "不支持 #include");
        rejects("#if 1\n", "endif"); rejects("#endif\n", "缺少 if");
        rejects("#ifdef X Y\n#endif\n", "宏名");
        rejects("#define F(a,a) a\n", "重复"); rejects("#define F(a,) a\n", "不能为空");
        rejects("#define F(a) a\nF(1,2)\n", "数量");
        rejects("#define X 1\n#define X 2\n", "重复定义");
        result = preprocess("#define N 3\n#define STR(x) #x\n#define XSTR(x) STR(x)\nSTR(N) XSTR(N)\nSTR(a  +  b)\nSTR(\"a\\n\")\n");
        require(result.ok() && result.source.find("\"N\" \"3\"") != std::string::npos &&
                result.source.find("\"a + b\"") != std::string::npos && result.source.find("\"\\\"a\\\\n\\\"\"") != std::string::npos, "字符串化应使用原始实参并转义字面量");
        result = preprocess("#define N 7\n#define CAT(a,b) a ## b\n#define XCAT(a,b) CAT(a,b)\n#define varN 11\n#define var7 12\nCAT(var,N) XCAT(var,N) CAT(,N) CAT(N,) CAT(,)\nCAT(+,=) CAT(1,e3)\n");
        require(result.ok() && result.source.find("11 12 7 7") != std::string::npos && result.source.find("+= 1e3") != std::string::npos, "拼接应使用原始实参、处理空参数并重扫描结果");
        result = preprocess("#define F(x) ((x)+1)\n#define ALIAS F\nALIAS(2) F(F(1))\n#define JOIN(a,b,c) a ## b ## c\nJOIN(,x,)\n#define PLUS +\nPLUS+\n");
        require(result.ok() && result.source.find("((2)+1)") != std::string::npos && result.source.find("((((1)+1))+1)") != std::string::npos &&
                result.source.find("x") != std::string::npos && result.source.find("+ +") != std::string::npos, "别名与源码需一起重扫描，普通替换不能意外拼成 ++");
        rejects("#define F(a) #bad\n", "字符串化");
        rejects("#define F(a) ##a\n", "左右");
        rejects("#define F(a,b) a##b\nF(x,+)\n", "有效预处理单词");
        result = preprocess("#define f(a) a*g\n#define g(a) f(a)\nf(2)(9)\n");
        require(result.ok() && result.source.find("2*9*g") != std::string::npos, "跨宏边界的函数调用不能错误禁用后续宏");
        rejects("/* unfinished", "注释"); rejects("#error stop\n", "stop");
        rejects("#if " + std::string(140, '!') + "1\n#endif\n", "过深");
        std::cout << "预处理：宏、条件、拒绝包含、行映射和错误边界全部通过\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
