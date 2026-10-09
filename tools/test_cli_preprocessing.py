"""通过正式命令行验证宏、嵌套本地头文件、位置映射和失败停止。"""

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

        write("inc/outer.h", '#ifndef OUTER_H\n#define OUTER_H\n#include "子目录/config.h"\n#endif\n')
        write("inc/子目录/config.h", '#define VALUE 11\n#include "../outer.h"\n')
        nested = write("nested.c", '#include "inc/outer.h"\n#include "inc/outer.h"\nint main(void) { printf("nested = %d\\n",VALUE); return 0; }\n')
        check("nested_relative_and_guard", nested, output="nested = 11\n")

        write("left/entry.h", '#include "config.h"\n')
        write("left/config.h", '#define LEFT 3\n')
        write("right/entry.h", '#include "config.h"\n')
        write("right/config.h", '#define RIGHT 8\n')
        siblings = write("siblings.c", '#include "left/entry.h"\n#include "right/entry.h"\nint main(void) { printf("siblings = %d %d\\n",LEFT,RIGHT); return 0; }\n')
        check("same_named_headers", siblings, output="siblings = 3 8\n")
        angled = write("angle.c", '#include <left/config.h>\nint main(void) { return LEFT-3; }\n')
        check("local_angle_header", angled, output="")
        absolute = write("absolute.c", '#include "' + (folder / "right/config.h").as_posix() + '"\nint main(void) { return RIGHT-8; }\n')
        check("absolute_header", absolute, output="")

        broken_header = write("errors/bad.h", '\nint h=;\n')
        broken = write("broken.c", '#include "errors/bad.h"\nint main(void) { printf("SHOULD_NOT_RUN"); return 0; }\n')
        check("header_diagnostic_path", broken, code=1, output="", errors=(str(broken_header).replace("\\", "/") + ":2:1", "[语法/错误]"))
        missing_header = write("errors/missing.h", '#include "absent.h"\n')
        missing = write("missing.c", '#include "errors/missing.h"\nint main(void) { return 0; }\n')
        check("missing_nested_header", missing, code=1, output="", errors=(str(missing_header).replace("\\", "/") + ":1:1", "[预处理/错误]", "absent.h"))
        failure = write("error.c", '#error stop here\nint main(void) { printf("SHOULD_NOT_RUN"); return 0; }\n')
        check("preprocessing_stops_run", failure, code=1, output="", errors=("[预处理/错误]", "stop here"))
        write("cycle/a.h", '#include "b.h"\n')
        write("cycle/b.h", '#include "a.h"\n')
        cycle = write("cycle.c", '#include "cycle/a.h"\nint main(void) { return 0; }\n')
        check("include_depth_limit", cycle, code=1, output="", errors=("[预处理/错误]", "头文件包含超过 64 层"))
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
