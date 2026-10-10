"""运行课堂演示，核对实际结果，并保存各阶段的 UTF-8 文本。"""

import argparse
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
DEMO = ROOT / "examples" / "showcase"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/cmake/minic.exe",
                        help="正式 minic 程序路径")
    parser.add_argument("--output", type=Path, default=ROOT / "build/showcase",
                        help="演示文本保存目录")
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    if not compiler.is_file():
        parser.error("找不到编译器，请先构建项目，或用 --compiler 指定 minic 的完整路径")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    summary = output / "summary.txt"
    summary.write_text("本轮演示尚未完成，不能使用上次的成功摘要作为本次结果。\n", encoding="utf-8")
    completed = []

    def execute(name, source, mode, *, data="", expected=None, contains=(), errors=(), raw=False, code=0):
        command = [str(compiler), mode] + (["--raw"] if raw else []) + [str(DEMO / source)]
        result = subprocess.run(command, input=data.encode("utf-8"), capture_output=True, timeout=20)
        out = result.stdout.decode("utf-8").replace("\r\n", "\n")
        err = result.stderr.decode("utf-8").replace("\r\n", "\n")
        (output / (name + ".txt")).write_text(out, encoding="utf-8")
        (output / (name + ".diagnostics.txt")).write_text(err, encoding="utf-8")
        assert result.returncode == code, f"{name}：退出码错误，{err}"
        if expected is not None:
            assert out == expected, f"{name}：实际输出 {out!r}，预期 {expected!r}"
        for text in contains:
            assert text in out, f"{name}：展示结果缺少 {text}"
        for text in errors:
            assert text in err, f"{name}：诊断缺少 {text}"
        if code == 0:
            assert not err, f"{name}：正常演示产生诊断 {err}"
        completed.append(name)
        print("PASS " + name)

    try:
        # 原始 Token 保留列号；其他阶段展示实际默认编译流程。
        for mode, texts in [
            ("tokens", ("KW_INT", "END_OF_FILE")),
            ("parse", ("FunctionDef", "For", "Call")),
            ("symbols", ("sum_to", "作用域", "scanf")),
            ("check", ("语义检查通过",)),
            ("ir", ("常量池", "call", "ret")),
        ]:
            execute("01_" + mode, "01_pipeline.c", mode, contains=texts, raw=mode == "tokens")
        execute("01_run", "01_pipeline.c", "run", data="5\n", expected="sum = 15\n")
        execute("02_run", "02_records_macros.c", "run",
                expected="Alice: 90\nBob: 55\nChen: 80\npassed = 2\naverage = 75.000000\n")
        execute("02_symbols", "02_records_macros.c", "symbols", contains=("Student", "name", "score"))
        execute("03_run", "03_callback_static.c", "run", expected="counter = 1 2\ntwice = 12\n")
        execute("04_semantic_error", "04_semantic_error.c", "run", expected="", code=1,
                errors=("[语义/错误]", "SEM_UNDECLARED", "04_semantic_error.c:3:1"))
        execute("05_runtime_error", "05_runtime_error.c", "run", expected="", code=1,
                errors=("[运行/错误]", "越界", "05_runtime_error.c:5:1"))
    except (AssertionError, OSError, UnicodeError, subprocess.TimeoutExpired) as error:
        summary.write_text("本轮演示失败：" + str(error) + "\n", encoding="utf-8")
        print("演示检查失败：" + str(error))
        return 1

    summary.write_text(
        f"演示检查 {len(completed)}/{len(completed)} 通过。\n"
        "01 输入：5；正常输出：sum = 15。\n"
        "02 展示数组、记录和宏。\n"
        "03 展示直接函数调用与静态存储。\n"
        "04、05 是预期失败的错误演示，脚本已核对错误阶段。\n"
        "文件 .txt 保存阶段结果，.diagnostics.txt 保存诊断。\n", encoding="utf-8")
    print(f"showcase: {len(completed)} checks passed；结果保存到 {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
