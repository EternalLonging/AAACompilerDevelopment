"""检查联调夹具；正式前端到位后，比较实际源码编译执行与预期结果。"""
from __future__ import annotations
import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/integration"
PHASES = {"Preprocess", "Lexer", "Parser", "Semantic", "IR", "Runtime"}
sys.dont_write_bytecode = True

def run(command, input=b"", timeout=20):
    return subprocess.run(command, input=input, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)

def normalized(data):
    # 只统一 Windows 换行，保留空格与结束换行，不能掩盖输出错误。
    return data.replace(b"\r\n", b"\n")

def load_cases():
    manifest = json.loads((FIXTURES / "manifest.json").read_text(encoding="utf-8"))
    if manifest["version"] != 1 or not manifest["cases"]:
        raise ValueError("夹具清单版本错误或为空")
    names = set()
    for case in manifest["cases"]:
        if case["id"] in names:
            raise ValueError("用例编号重复：" + case["id"])
        names.add(case["id"])
        path = (FIXTURES / case["source"]).resolve()
        if not path.is_relative_to(FIXTURES.resolve()) or not path.is_file():
            raise ValueError("源码路径无效：" + case["source"])
        for field in ("stdin", "stdout", "description"):
            if not isinstance(case[field], str):
                raise ValueError("夹具文字字段无效：" + field)
        if case["expect"] == "reject":
            if case["phase"] not in PHASES:
                raise ValueError("错误阶段无效")
        elif case["expect"] != "run" or not isinstance(case["exit_code"], int):
            raise ValueError("预期行为无效")
    listed = {case["source"] for case in manifest["cases"]}
    actual = {path.relative_to(FIXTURES).as_posix() for folder in ("programs", "errors") for path in (FIXTURES / folder).glob("*.c")}
    if listed != actual:
        raise ValueError("源码文件与清单不一致")
    return manifest["cases"]

def prepare(cases, preprocessor):
    spec = importlib.util.spec_from_file_location("fixture_lexer_rules", ROOT / "tools/check_lexer_patterns.py")
    scanner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(scanner)
    for case in cases:
        result = run([str(preprocessor), str(FIXTURES / case["source"])])
        expected_pp_failure = case["expect"] == "reject" and case["phase"] == "Preprocess"
        if expected_pp_failure:
            if result.returncode != 1 or b"[fixture_phase=Preprocess]" not in result.stderr:
                raise AssertionError(case["id"] + "：应在预处理阶段失败")
            continue
        if result.returncode:
            raise AssertionError(case["id"] + "：预处理失败\n" + result.stderr.decode("utf-8", errors="replace"))
        _, errors, _ = scanner.scan(result.stdout.decode("utf-8"))
        expected_lex_failure = case["expect"] == "reject" and case["phase"] == "Lexer"
        if bool(errors) != expected_lex_failure:
            raise AssertionError(f"{case['id']}：词法规则检查不符 {errors}")
    print(f"夹具准备检查通过：{len(cases)} 个用例的清单、预处理及词法规则符合预期；尚未验证正式语法/语义链路。")

def reference(cases, gcc):
    count = 0
    with tempfile.TemporaryDirectory(prefix="minic-fixtures-") as temporary:
        for case in cases:
            if case["expect"] != "run":
                continue  # 错误样例含项目约束和未定义行为，不能交给 GCC 当判据或执行。
            executable = Path(temporary) / (case["id"] + (".exe" if sys.platform == "win32" else ""))
            built = run([gcc, "-x", "c", "-std=c89", "-include", "stdio.h", str(FIXTURES / case["source"]), "-o", str(executable)])
            if built.returncode:
                raise AssertionError(case["id"] + "：GCC 参考构建失败\n" + built.stderr.decode("utf-8", errors="replace"))
            result = run([str(executable)], case["stdin"].encode("utf-8"))
            if result.returncode != case["exit_code"] or normalized(result.stdout) != case["stdout"].encode("utf-8"):
                raise AssertionError(f"{case['id']}：参考输出不符，实际={result.stdout!r}，退出码={result.returncode}")
            count += 1
    print(f"GCC 参考执行通过：{count} 个正常程序的输入、输出和退出码一致；这不代表本项目编译通过。")

def integration(cases, compiler):
    for case in cases:
        result = run([str(compiler), str(FIXTURES / case["source"])], case["stdin"].encode("utf-8"))
        stderr = result.stderr.decode("utf-8", errors="replace")
        if case["expect"] == "run":
            correct = result.returncode == 0 and f"[fixture_exit={case['exit_code']}]" in stderr and not re.search(r"\[fixture_phase=", stderr)
        else:
            phases = re.findall(r"\[fixture_phase=(\w+)\]", stderr)
            correct = result.returncode == 1 and phases and phases[0] == case["phase"] and "[fixture_exit=" not in stderr
            if "code" in case:
                correct = correct and f"[fixture_code={case['code']}]" in stderr
            if "message" in case:
                correct = correct and case["message"] in stderr
        if not correct or normalized(result.stdout) != case["stdout"].encode("utf-8"):
            raise AssertionError(f"{case['id']}：联调失败\n退出状态={result.returncode}\n输出={result.stdout!r}\n{stderr}")
        print("PASS " + case["id"] + " " + case["description"])
    print(f"真实源码联调全部通过：{len(cases)} 个用例。")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preprocessor", type=Path, help="仅预处理的 integration_preprocess_driver")
    parser.add_argument("--reference-gcc", help="可选：GCC 驱动路径，仅执行正常样例")
    parser.add_argument("--compiler", type=Path, help="正式前端链接后的 integration_driver")
    args = parser.parse_args()
    cases = load_cases()
    if not any((args.preprocessor, args.reference_gcc, args.compiler)):
        print(f"清单结构检查通过：{len(cases)} 个用例；未执行编译器。")
    if args.preprocessor:
        prepare(cases, args.preprocessor.resolve())
    if args.reference_gcc:
        reference(cases, args.reference_gcc)
    if args.compiler:
        integration(cases, args.compiler.resolve())

if __name__ == "__main__":
    try:
        main()
    except (AssertionError, ValueError, KeyError, OSError, subprocess.TimeoutExpired) as error:
        print("联调检查失败：" + str(error), file=sys.stderr)
        sys.exit(1)
