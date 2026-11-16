"""验证子路径部署、浏览器与原生结果一致、运行限制和线程恢复。"""

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import sys
import threading

from playwright.sync_api import sync_playwright, expect

from test_integration import FIXTURES, load_cases
from workbench_server import ROOT, example_programs, invoke


class SiteHandler(SimpleHTTPRequestHandler):
    # 使用与 GitHub 项目网站相同的子路径，发现误写成根路径的资源地址。
    prefix = "/AAACompilerDevelopment/"

    def do_GET(self):
        if not self.path.startswith(self.prefix):
            self.send_error(404)
            return
        self.path = self.path[len(self.prefix) - 1:]
        super().do_GET()

    def log_message(self, *args):
        pass


def verify(url, args):
    with sync_playwright() as p:
        options = {"headless": True}
        if args.browser:
            options["executable_path"] = args.browser
        browser = p.chromium.launch(**options)
        page = browser.new_page()
        errors, requests = [], []
        page.on("pageerror", lambda error: errors.append(str(error)))
        page.on("request", lambda request: requests.append(request.url))
        page.goto(url)
        expect(page.locator("#status")).to_have_text("准备就绪", timeout=60000)
        expect(page.locator(".local-dot")).to_have_text("浏览器运行")
        page.locator("#run").click()
        expect(page.locator("#status")).to_have_text("运行完成", timeout=20000)
        expect(page.locator("#output")).to_contain_text("value = 45, hits = 0")
        page.evaluate("async () => { const {BrowserCompiler} = await import('./compiler-client.js'); window.pagesCompiler = new BrowserCompiler(); await pagesCompiler.initialize(); }")

        def run(payload, action="run"):
            return page.evaluate("([action, payload]) => pagesCompiler.request(action, payload)", [action, payload])

        cases = [("example:" + e["name"], {"source": e["source"], "input": e["input"], "files": e["files"]}) for e in example_programs()]
        for case in load_cases():
            cases.append((case["id"], {"source": (FIXTURES / case["source"]).read_text(encoding="utf-8"), "input": case["stdin"]}))
        cases += [
            ("nested-unicode", {"entry": "源码/测试.c", "source": 'int main(void){printf("中文\\n");return 9;}', "files": {"other.c": "invalid"}}),
            ("step-limit", {"source": "int main(void){while(1){}return 0;}"}),
            ("division-by-zero", {"source": "int main(void){int x=0;return 1/x;}"}),
        ]
        for name, payload in cases:
            actual, native = run(payload), invoke(args.driver.resolve(), payload, "run")
            assert actual == native, f"浏览器与原生结果不一致：{name}"
        print(f"Pages: {len(cases)} 个原生/Wasm 完整结果一致", flush=True)
        inspected = run({"source": "int main(void){return 0;}"}, "inspect")
        assert inspected["optimized"]["run"] is None

        for payload in [
            {"source": "", "entry": "../demo.c"},
            {"source": "", "files": {"MAIN.c": ""}},
            {"source": "", "files": {"main.c/child.c": ""}},
            {"source": "", "entry": "CON.c"},
            {"source": "a" * 262145}, {"source": "", "input": "a" * 65537},
            {"source": "", "files": {f"file{i}.c": "" for i in range(33)}},
        ]:
            rejected = page.evaluate("payload => pagesCompiler.request('inspect', payload).then(() => false, e => Boolean(e.message))", payload)
            assert rejected, "非法项目应被拒绝"

        # 让真实 Worker 停在死循环中，验证页面仍能终止它并创建新编译器。
        page.workers[-1].evaluate("() => { self.onmessage = () => { while (true) {} }; }")
        stopped = page.evaluate("() => pagesCompiler.request('run', {source: ''}).then(() => '', e => e.message)")
        assert "15 秒" in stopped, stopped
        assert run({"source": "int main(void){return 7;}"})["optimized"]["run"]["exit"] == 7
        assert not errors, errors
        assert not any("/api/" in request for request in requests), "网站不应请求本地后端"
        assert any(request.endswith("minic.wasm") for request in requests), "必须实际加载 Wasm 编译器"
        browser.close()
    command = [sys.executable, str(ROOT / "tools/test_workbench_browser.py"), "--url", url, "--output", str(args.output)]
    if args.browser:
        command += ["--browser", args.browser]
    subprocess.run(command, check=True, timeout=180)
    print(f"Pages: {len(cases)} 个原生/Wasm 完整结果对照、分析模式、非法项目、超时恢复与网页交互通过")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--site", type=Path, default=ROOT / "build/site")
    parser.add_argument("--driver", type=Path, required=True)
    parser.add_argument("--browser", help="可选 Chrome/Edge 完整路径")
    parser.add_argument("--output", type=Path, default=ROOT / "build/pages-check")
    parser.add_argument("--url", help="可选：直接验证已经部署的网站")
    args = parser.parse_args()
    if args.url:
        verify(args.url, args)
        return
    with ThreadingHTTPServer(("127.0.0.1", 0), partial(SiteHandler, directory=str(args.site.resolve()))) as server:
        server.daemon_threads = True
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            verify(f"http://127.0.0.1:{server.server_port}" + SiteHandler.prefix, args)
        finally:
            server.shutdown()
            thread.join(timeout=2)


if __name__ == "__main__":
    main()
