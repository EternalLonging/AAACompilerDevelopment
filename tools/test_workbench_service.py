"""验证真实工作台进程、JSON结果、编译失败与本地服务接口。"""

import argparse
import importlib.util
import json
from pathlib import Path
import threading
from http.server import ThreadingHTTPServer
import urllib.request
import urllib.error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("driver", type=Path)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("workbench_server", Path(__file__).with_name("workbench_server.py"))
    server_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(server_module)
    driver = args.driver.resolve()
    checks = 0

    def check(value, message):
        nonlocal checks
        checks += 1
        if not value:
            raise AssertionError(message)

    source = 'int main(void){int x=(2+3)*(4+5);printf("%d\\n",x);return 0;}'
    data = server_module.invoke(driver, {"source": source}, "run")
    check(data["ok"] and data["equivalent"] is True, "真实程序两侧应执行成功且等价")
    check(data["optimized"]["run"]["output"] == "45\n", "优化输出正确")
    check(len(data["optimized"]["quads"]) < len(data["baseline"]["quads"]), "真实生成的指令数应减少")
    check(data["tokens"] and data["ast"] and data["symbols"], "各阶段数据不能为演示假数据")
    check(all(n["parent"] == -1 or n["parent"] < n["id"] for n in data["ast"]), "图形AST父子编号有效")
    project = {"source": '#include "inc/a.h"\nint main(void){printf("%d\\n",N);return 0;}\n',
               "files": {"inc/a.h": '#include "b.h"\n', "inc/b.h": '#define N 7\n'}}
    check(server_module.invoke(driver, project, "run")["optimized"]["run"]["output"] == "7\n", "工作台嵌套头文件读取")
    failed = server_module.invoke(driver, {"source": 'int main(void){unknown=1;return 0;}'}, "run")
    check(not failed["ok"] and "baseline" not in failed and failed["diagnostics"][0]["phase"] == "语义", "失败阶段不能继续生成或运行")
    runtime = server_module.invoke(driver, {"source": 'int main(void){int x=0;return 1/x;}'}, "run")
    check(runtime["equivalent"] is None and not runtime["optimized"]["run"]["ok"], "失败运行不得宣称优化等价")
    limited = server_module.invoke(driver, {"source": 'int main(void){while(1){}return 0;}'}, "run")
    check(not limited["optimized"]["run"]["ok"] and limited["optimized"]["run"]["steps"] == 100000, "工作台运行受指令上限限制")
    inspected = server_module.invoke(driver, {"source": source}, "inspect")
    check(inspected["optimized"]["run"] is None and inspected["equivalent"] is None, "分析不自动执行")
    complete_source = "int main(void){int total=1;tot"
    completed = server_module.invoke(driver, {"source": complete_source, "cursor": len(complete_source)}, "complete")
    check(any(item["label"] == "total" for item in completed["items"]), "服务连接真实补全接口")
    for payload in [{"source": source, "files": {"../outside.h": ""}}, {"source": source, "cursor": -1}]:
        try:
            server_module.invoke(driver, payload, "inspect")
        except ValueError:
            check(True, "输入限制")
        else:
            check(False, "非法路径或光标应拒绝")

    with ThreadingHTTPServer(("127.0.0.1", 0), server_module.handler(driver)) as server:
        server.daemon_threads = True
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        url = f"http://127.0.0.1:{server.server_port}"

        def request(path, payload=None, headers=None):
            body = None if payload is None else json.dumps(payload).encode("utf-8")
            req = urllib.request.Request(url + path, data=body, headers=headers or {})
            try:
                with urllib.request.urlopen(req, timeout=20) as response:
                    return response.status, response.read()
            except urllib.error.HTTPError as error:
                return error.code, error.read()

        try:
            status, body = request("/")
            check(status == 200 and "源码编辑" in body.decode("utf-8"), "实际提供编辑器页面")
            status, body = request("/api/examples")
            check(status == 200 and len(json.loads(body)) == 6, "实际示例列表")
            status, body = request("/api/run", {"source": source})
            check(status == 200 and json.loads(body)["equivalent"], "HTTP执行真实编译器")
            check(request("/api/analyze", {"source": source}, {"Origin": "http://other-site.test"})[0] == 403, "仅允许本地工作台页面调用")
            check(request("/../README.md")[0] == 404, "静态页面不能读取仓库任意文件")
        finally:
            server.shutdown()
            thread.join(timeout=2)
    print(f"workbench service: {checks} checks passed")


if __name__ == "__main__":
    main()
