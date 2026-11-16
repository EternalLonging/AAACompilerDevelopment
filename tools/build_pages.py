"""将网页、Wasm 编译器和示例整理为 GitHub Pages 静态网站。"""

import argparse
import json
from pathlib import Path
import shutil

from workbench_server import ROOT, example_programs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wasm-dir", type=Path, default=ROOT / "build/wasm")
    parser.add_argument("--output", type=Path, default=ROOT / "build/site")
    args = parser.parse_args()
    assets = ("minic.js", "minic.wasm")
    for name in assets:
        if not (args.wasm_dir / name).is_file():
            parser.error(f"找不到 {name}，请先构建 WebAssembly 工作台")
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ("app.js", "style.css", "compiler-client.js", "wasm-worker.js"):
        shutil.copy2(ROOT / "web" / name, args.output / name)
    html = (ROOT / "web/index.html").read_text(encoding="utf-8")
    html = html.replace('<html lang="zh-CN">', '<html lang="zh-CN" data-runtime="wasm">')
    html = html.replace('>本地运行</span>', '>浏览器运行</span>')
    html = html.replace('<footer>', '<footer><a href="./learning/reduce-actions.html">语法归约动画 ↗</a>')
    (args.output / "index.html").write_text(html, encoding="utf-8")
    for name in assets:
        shutil.copy2(args.wasm_dir / name, args.output / name)
    (args.output / "examples.json").write_text(json.dumps(example_programs(), ensure_ascii=False), encoding="utf-8")
    (args.output / ".nojekyll").touch()
    learning = args.output / "learning"
    learning.mkdir(exist_ok=True)
    shutil.copy2(ROOT / "docs/learning/reduce-actions.html", learning / "reduce-actions.html")
    print(f"GitHub Pages 网站：{args.output.resolve()}")


if __name__ == "__main__":
    main()
