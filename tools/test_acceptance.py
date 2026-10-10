"""验收正式命令行：展示各阶段、执行真实程序、检查失败与文件路径。"""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile


# 这些程序遵守项目语法，并且不依赖动态内存或系统头文件。
PROGRAMS = [
    ("bubble_sort", """void sort(int a[],int n) { int i,j,t;
for(i=0;i<n;i++) { for(j=0;j<n-1-i;j++) {
if(a[j]>a[j+1]) { t=a[j]; a[j]=a[j+1]; a[j+1]=t; } } } }
int main(void) { int a[5]={5,1,4,2,3}; sort(a,5);
printf("sort = %d %d %d %d %d\\n",a[0],a[1],a[2],a[3],a[4]); return 0; }
""", "sort = 1 2 3 4 5\n", ""),
    ("record_scope", """struct S { int x; };
int main(void) { struct S a={3};
{ struct S { int y; int z; }; struct S b={4,5}; a.x+=b.z; }
printf("shadow = %d\\n",a.x); return 0; }
""", "shadow = 8\n", ""),
    ("input", """int main(void) { int a,b; scanf("%d%d",&a,&b);
printf("input = %d\\n",a+b); return 0; }
""", "input = 12\n", "5 7\n"),
]


def invoke(command, data=""):
    result = subprocess.run(command, input=data.encode("utf-8"), capture_output=True, timeout=20)
    # 只统一 Windows 换行，不掩盖乱码、额外输出或空格差异。
    return result.returncode, result.stdout.decode("utf-8").replace("\r\n", "\n"), result.stderr.decode("utf-8").replace("\r\n", "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler", type=Path, help="正式 minic 可执行文件")
    parser.add_argument("--reference-gcc", help="可选：以 C89 编译并运行上述正常程序")
    parser.add_argument("--report", type=Path, help="保存本次逐项验收结果")
    args = parser.parse_args()
    compiler = str(args.compiler.resolve())
    results = []

    def check(name, command, *, data="", code=0, output=None, contains=(), errors=(), empty_output=False):
        try:
            actual_code, out, err = invoke(command, data)
            assert actual_code == code, f"退出码 {actual_code}，预期 {code}"
            if output is not None:
                assert out == output, f"输出 {out!r}，预期 {output!r}"
            if empty_output:
                assert not out, f"失败后不应有程序输出：{out!r}"
            for text in contains:
                assert text in out, f"输出缺少 {text!r}"
            for text in errors:
                assert text in err, f"诊断缺少 {text!r}；实际 {err!r}"
            if code == 0:
                assert not err, f"成功时不应有诊断：{err!r}"
            results.append({"name": name, "passed": True})
        except (AssertionError, OSError, UnicodeError, subprocess.TimeoutExpired) as error:
            results.append({"name": name, "passed": False, "error": str(error)})
        print(("PASS " if results[-1]["passed"] else "FAIL ") + name)
        if not results[-1]["passed"]:
            print(results[-1]["error"])

    with tempfile.TemporaryDirectory(prefix="minic-acceptance-") as temporary:
        folder = Path(temporary)
        basic = folder / "circle.c"
        basic.write_text('int main(void) { float r=1; printf("c = %f\\n",2*3.14159*r); return 0; }\n', encoding="utf-8")
        check("help", [compiler, "--help"], contains=("tokens|parse|symbols|check|ir|run",))
        for mode, texts in [
            ("tokens", ("KW_INT", "END_OF_FILE")),
            ("parse", ("Program", "FunctionDef", "Call")),
            ("symbols", ("符号编号", "main", "printf", "作用域")),
            ("check", ("语义检查通过",)),
            ("ir", ("常量池", "函数", "call", "ret")),
        ]:
            check("display_" + mode, [compiler, mode, str(basic)], contains=texts)
        check("run_circle", [compiler, "run", str(basic)], output="c = 6.283180\n")
        check("missing_arguments", [compiler], code=1, errors=("用法",), empty_output=True)
        check("unknown_command", [compiler, "unknown", str(basic)], code=1, errors=("用法",), empty_output=True)
        check("missing_file", [compiler, "run", str(folder / "missing.c")], code=1, errors=("无法打开",), empty_output=True)
        # 用真实非 ASCII 文件名验收；不能把问号替代的路径当成中文路径测试。
        unicode_file = folder / "验收 中文文件.c"
        unicode_file.write_bytes(basic.read_bytes())
        check("unicode_path", [compiler, "run", str(unicode_file)], output="c = 6.283180\n")

        for name, source, expected, data in PROGRAMS:
            file = folder / (name + ".c")
            file.write_text(source, encoding="utf-8")
            check(name, [compiler, "run", str(file)], output=expected, data=data)
            if args.reference_gcc:
                executable = folder / (name + ".exe")
                built, _, err = invoke([args.reference_gcc, "-x", "c", "-std=c89", "-include", "stdio.h", str(file), "-o", str(executable)])
                if built:
                    results.append({"name": "reference_" + name, "passed": False, "error": err})
                else:
                    check("reference_" + name, [str(executable)], output=expected, data=data)

        for name, source, phase, message in [
            ("lexer_stop", 'int main(void) { @ printf("SHOULD_NOT_RUN"); return 0; }', "词法", "LEX_"),
            ("parser_stop", 'int main(void) { int x=; printf("SHOULD_NOT_RUN"); return 0; }', "语法", "PARSE_"),
            ("semantic_stop", 'int main(void) { x=1; printf("SHOULD_NOT_RUN"); return 0; }', "语义", "SEM_UNDECLARED"),
            ("runtime_zero", 'int main(void) { int x=0; x=1/x; printf("SHOULD_NOT_RUN"); return 0; }', "运行", "除零"),
            ("runtime_bounds", 'int main(void) { int a[2]={1,2}; int i=2; a[i]=3; printf("SHOULD_NOT_RUN"); return 0; }', "运行", "越界"),
        ]:
            file = folder / (name + ".c")
            file.write_text(source + "\n", encoding="utf-8")
            check(name, [compiler, "run", str(file)], code=1, errors=("[" + phase + "/", message), empty_output=True)
        file = folder / "exit_code.c"
        file.write_text("int main(void) { return 7; }\n", encoding="utf-8")
        check("main_exit_code", [compiler, "run", str(file)], code=7, output="")
        file = folder / "recover.c"
        file.write_text("int main(void) {\n x=;\n y=;\n return 0;\n}\n", encoding="utf-8")
        check("multiple_diagnostics", [compiler, "check", str(file)], code=1,
              errors=("recover.c:2:", "recover.c:3:", "[语法/错误]"), empty_output=True)

        # 通过真实源码检查删除功能的拒绝行为和数值边界，失败后不应输出后续提示。
        for name, source, phase, message in [
            ("dangling_pointer", 'int *bad(void){int x=7;return &x;} int main(void){int *p=bad();return *p;}', "语法", "PARSE_POINTER_REMOVED"),
            ("one_past_pointer", 'int main(void){int a[2]={1,2};int *p=a+2;return *p;}', "语法", "PARSE_POINTER_REMOVED"),
            ("volatile_removed", 'volatile int x;int main(void){return 0;}', "语法", "PARSE_"),
            ("for_declaration", 'int main(void){for(int i=0;i<3;i++){}return 0;}', "语法", "PARSE_FOR_DECLARATION"),
            ("late_declaration", 'int main(void){int x=0;x++;int y=2;return y;}', "语法", "PARSE_DECLARATION_ORDER"),
            ("address_removed", 'int main(void){int x=0;return &x;}', "语义", "SEM_ADDRESS"),
            ("negative_index", 'int main(void){int a[2]={1,2};int i=-1;return a[i];}', "运行", "越界"),
            ("unrelated_pointers", 'int main(void){int a[2],b[2];return a-b;}', "语义", "SEM_OPERANDS"),
            ("shift_count", 'int main(void){int x=1,n=32;return x<<n;}', "运行", "移位"),
            ("integer_overflow", 'int main(void){int x=2147483647;return x+1;}', "运行", "范围"),
            ("object_size_limit", 'int main(void){int a[5000000];return 0;}', "语义", "16 MiB"),
            ("const_pointer_write", 'int main(void){int x=1;const int *p=&x;*p=2;return 0;}', "语法", "PARSE_POINTER_REMOVED"),
        ]:
            file = folder / (name + ".c")
            file.write_text(source + "\n", encoding="utf-8")
            check(name, [compiler, "run", str(file)], code=1,
                  errors=("[" + phase + "/", message), empty_output=True)

    passed = sum(item["passed"] for item in results)
    print(f"acceptance: {passed}/{len(results)} checks passed")
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps({"passed": passed, "total": len(results), "checks": results}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
