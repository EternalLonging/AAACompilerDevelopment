"""启动仅供本机使用的编译器工作台，不需要第三方 Python 包。"""

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import tempfile
import threading
import webbrowser


ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web"


def project_path(name):
    # 项目文件只使用相对路径，统一拒绝 Windows 保留名字和容易混淆的路径。
    if not isinstance(name, str):
        raise ValueError("文件名必须为文本")
    name = name.strip().replace("\\", "/")
    parts = name.split("/")
    if len(name) > 180 or not re.fullmatch(r"[\w\u0080-\uffff /.-]+\.c", name) or any(
        not part or part in (".", "..") or part.endswith((".", " ")) or
        re.match(r"^(con|prn|aux|nul|com[1-9]|lpt[1-9])(\.|$)", part, re.I)
        for part in parts
    ) or parts[0].lower() == "input.txt":
        raise ValueError("文件名应为项目内的 .c 相对路径")
    return PurePosixPath(name)


def invoke(driver, payload, action):
    source = payload.get("source", "")
    stdin = payload.get("input", "")
    files = payload.get("files", {})
    if not isinstance(source, str) or not isinstance(stdin, str) or not isinstance(files, dict):
        raise ValueError("源码、输入或项目文件格式不正确")
    if len(files) > 32 or len(source.encode("utf-8")) > 262144 or len(stdin.encode("utf-8")) > 65536:
        raise ValueError("源码、输入或项目文件数量超过工作台限制")
    entry = project_path(payload.get("entry", "main.c"))
    if entry.suffix != ".c":
        raise ValueError("编译文件必须是 .c 源文件")
    project = {entry: source}
    for name, text in files.items():
        if not isinstance(text, str):
            raise ValueError("项目文件内容必须为文本")
        path = project_path(name)
        lower = str(path).lower()
        if any(lower == str(p).lower() or lower.startswith(str(p).lower() + "/") or str(p).lower().startswith(lower + "/") for p in project):
            raise ValueError("项目文件重名或目录冲突")
        if len(text.encode("utf-8")) > 262144:
            raise ValueError("项目文件过大")
        project[path] = text
    with tempfile.TemporaryDirectory(prefix="minic-workbench-") as temporary:
        folder = Path(temporary)
        (folder / "input.txt").write_bytes(stdin.encode("utf-8"))
        for path, text in project.items():
            target = folder.joinpath(*path.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            # 原样写入 UTF-8，Windows 自动转换换行会让编辑器光标与源码字节位置错位。
            target.write_bytes(text.encode("utf-8"))
        cursor = 0
        if not isinstance(cursor, int) or cursor < 0 or cursor > len(source.encode("utf-8")):
            raise ValueError("光标位置无效")
        result = subprocess.run([str(driver), action, str(folder.joinpath(*entry.parts)), str(folder / "input.txt"), str(cursor), str(folder)],
                                capture_output=True, timeout=15)
        if result.returncode:
            raise ValueError(result.stderr.decode("utf-8", errors="replace") or "编译服务失败")
        return json.loads(result.stdout.decode("utf-8"))


def handler(driver):
    slots = threading.BoundedSemaphore(2)

    class Handler(BaseHTTPRequestHandler):
        # 只提供工作台文件和固定 API，避免把仓库与用户磁盘当成静态目录。
        def send(self, status, data, content_type="application/json; charset=utf-8"):
            try:
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(data)))
                self.send_header("Cache-Control", "no-store")
                self.send_header("X-Content-Type-Options", "nosniff")
                self.end_headers()
                self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                pass  # 页面切换后，客户端可能已经断开，无需再次发送错误。

        def json(self, status, payload):
            self.send(status, json.dumps(payload, ensure_ascii=False).encode("utf-8"))

        def allowed(self):
            address = f"127.0.0.1:{self.server.server_port}"
            if self.headers.get("Host") != address:
                self.json(403, {"error": "请使用启动时显示的本地地址"}); return False
            origin = self.headers.get("Origin")
            if origin and origin != "http://" + address:
                self.json(403, {"error": "仅接受工作台页面请求"}); return False
            return True

        def do_GET(self):
            if not self.allowed():
                return
            if self.path == "/api/examples":
                demos = []
                for file in sorted((ROOT / "examples/showcase").glob("*.c")):
                    demos.append({"name": file.name, "source": file.read_text(encoding="utf-8"),
                                  "input": "5\n" if file.name.startswith("01") else "",
                                  "files": {}})
                demos.insert(0, {"name": "常量折叠与短路", "source": 'int main(void) {\n    int x = (2 + 3) * (4 + 5);\n    int hits = 0;\n    if (0 && ++hits) {\n        hits = 99;\n    }\n    printf("value = %d, hits = %d\\n", x, hits);\n    return 0;\n}\n', "input": "", "files": {}})
                self.json(200, demos); return
            assets = {"/": ("index.html", "text/html"), "/app.js": ("app.js", "text/javascript"), "/style.css": ("style.css", "text/css")}
            if self.path not in assets:
                self.json(404, {"error": "页面不存在"}); return
            name, mime = assets[self.path]
            self.send(200, (WEB / name).read_bytes(), mime + "; charset=utf-8")

        def do_POST(self):
            if not self.allowed():
                return
            actions = {"/api/analyze": "inspect", "/api/run": "run"}
            if self.path not in actions:
                self.json(404, {"error": "操作不存在"}); return
            if not slots.acquire(blocking=False):
                self.json(429, {"error": "正在编译，请稍后重试"}); return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 2097152:
                    raise ValueError("请求为空或过大")
                payload = json.loads(self.rfile.read(length))
                if not isinstance(payload, dict):
                    raise ValueError("请求必须为对象")
                self.json(200, invoke(driver, payload, actions[self.path]))
            except (ValueError, OSError, subprocess.TimeoutExpired) as error:
                self.json(400, {"error": str(error)})
            finally:
                slots.release()

        def log_message(self, format, *args):
            pass

    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", type=Path, default=ROOT / "build/cmake/workbench_driver.exe")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--open", action="store_true", help="启动后在默认浏览器打开")
    args = parser.parse_args()
    driver = args.driver.resolve()
    if not driver.is_file():
        parser.error("找不到 workbench_driver，请先构建项目，或使用 --driver 指定路径")
    with ThreadingHTTPServer(("127.0.0.1", args.port), handler(driver)) as server:
        server.daemon_threads = True
        url = f"http://127.0.0.1:{server.server_port}"
        print("编译器工作台：" + url, flush=True)
        print("按 Ctrl+C 停止服务。源码仅在本机临时目录编译。", flush=True)
        if args.open:
            webbrowser.open(url)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
