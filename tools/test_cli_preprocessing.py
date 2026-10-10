"""通过正式命令行验证宏、头文件拒绝、位置映射和失败停止。"""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler", type=Path)
    args = parser.parse_args()
    compiler = str(args.compiler.resolve())
    checks = []

    with tempfile.TemporaryDirectory(prefix="minic-preprocessing-") as temporary:
        folder = Path(temporary) / "中文 源码目录"
        folder.mkdir()

        def write(name, source):
            path = folder / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding="utf-8")
            return path

        def check(name, file, *, mode="run", raw=False, code=0, output=None, contains=(), forbidden=(), errors=()):
            command = [compiler, mode] + (["--raw"] if raw else []) + [str(file)]
            try:
                result = subprocess.run(command, input=b"", capture_output=True, timeout=20, cwd=temporary)
                out = result.stdout.decode("utf-8").replace("\r\n", "\n")
                err = result.stderr.decode("utf-8").replace("\r\n", "\n")
                assert result.returncode == code, f"退出码 {result.returncode}；诊断 {err}"
                if output is not None:
                    assert out == output, f"输出 {out!r}；预期 {output!r}"
                for text in contains:
                    assert text in out, f"输出缺少 {text!r}"
                for text in forbidden:
                    assert text not in out, f"输出不应含 {text!r}"
                for text in errors:
                    assert text in err, f"诊断缺少 {text!r}；实际 {err!r}"
                if code == 0:
                    assert not err, err
                checks.append(True)
                print("PASS " + name)
            except (AssertionError, OSError, UnicodeError, subprocess.TimeoutExpired) as error:
                checks.append(False)
                print("FAIL " + name + ": " + str(error))

        macro = write("macro.c", '#define N 7\n#define ADD(a,b) ((a)+(b))\n#if defined(N) && N>3\nint main(void) { printf("macro = %d\\n",ADD(N,2)); return 0; }\n#else\n#error wrong branch\n#endif\n')
        check("macro_run", macro, output="macro = 9\n")
        for mode, texts in [
            ("tokens", ("INT_LITERAL", '"7"')),
            ("parse", ("FunctionDef", "Call")),
            ("symbols", ("main", "作用域")),
            ("check", ("语义检查通过",)),
            ("ir", ("常量池", "call")),
        ]:
            check("macro_" + mode, macro, mode=mode, contains=texts,
                  forbidden=("HASH", 'ID\t"N"') if mode == "tokens" else ())

        for name, directive in [("quote", '#include "config.h"'), ("angle", '#include <stdio.h>'), ("macro", '#define HEADER "config.h"\n#include HEADER')]:
            source = write("include_" + name + ".c", directive + '\nint main(void){return 0;}\n')
            check("reject_include_" + name, source, code=1, output="", errors=("[预处理/错误]", "不支持 #include"))
        failure = write("error.c", '#error stop here\nint main(void){return 0;}\n')
        check("preprocessing_stops_run", failure, code=1, output="", errors=("[预处理/错误]", "stop here"))
        raw = write("raw.c", 'int main(void) { return 0; }\n')
        check("raw_positions", raw, mode="tokens", raw=True, contains=("ID\t\"main\"\t1:5",))
        check("raw_macro_tokens", macro, mode="tokens", raw=True, contains=("HASH", "ID\t\"N\""))
        check("raw_execution", raw, raw=True, output="")
        legacy = Path(__file__).resolve().parents[1] / "tests/integration/programs/13_macros.c"
        check("existing_macro_fixture", legacy, output="macro = 7 3\n")

    print(f"cli preprocessing: {sum(checks)}/{len(checks)} checks passed")
    return 0 if all(checks) else 1


if __name__ == "__main__":
    raise SystemExit(main())
