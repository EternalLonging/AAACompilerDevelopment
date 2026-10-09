"""在已启动的工作台上验证编辑、补全、图形展示与优化对比。需 Playwright。"""

import argparse
import json
from pathlib import Path
from playwright.sync_api import sync_playwright, expect


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://127.0.0.1:8765")
    parser.add_argument("--browser", help="可选：Chrome/Edge 的完整路径")
    parser.add_argument("--output", type=Path, default=Path("build/browser-check"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with sync_playwright() as p:
        options = {"headless": True}
        if args.browser:
            options["executable_path"] = args.browser
        browser = p.chromium.launch(**options)
        page = browser.new_page(viewport={"width": 1440, "height": 1050}, device_scale_factor=1)
        # 原生保存窗口不由 Playwright 控制；验证下载分支实际生成的磁盘文件。
        page.add_init_script("window.showSaveFilePicker = undefined")
        errors = []
        page.on("pageerror", lambda error: errors.append(str(error)))
        page.on("dialog", lambda dialog: dialog.accept())
        page.goto(args.url)
        page.wait_for_load_state("networkidle")
        expect(page.locator("#editor")).to_contain_text("")
        page.locator("#run").click()
        expect(page.locator("#status")).to_have_text("运行完成", timeout=20000)
        expect(page.locator("#output")).to_contain_text("value = 45, hits = 0")
        expect(page.locator(".ast-node").first).to_be_visible()
        page.locator(".ast-node").first.click()
        assert page.locator("#editor").evaluate("e => e.selectionStart === 0")
        page.locator('[data-view="compare"]').click()
        expect(page.locator(".compare-status")).to_contain_text("两边输出与退出码一致")
        values = page.locator(".metric b").all_text_contents()
        assert int(values[0]) > int(values[1]), values
        page.screenshot(path=str(args.output / "desktop.png"), full_page=True)
        page.locator('[data-view="tokens"]').click()
        expect(page.locator(".data-table")).to_contain_text("KW_INT")
        page.locator('[data-view="symbols"]').click()
        expect(page.locator(".data-table")).to_contain_text("hits")
        page.locator('[data-view="ast"]').click()
        with page.expect_download() as download:
            page.locator("#export").click()
        exported = args.output / "ast.svg"
        download.value.save_as(str(exported))
        assert "ast-node" in exported.read_text(encoding="utf-8")

        editor = page.locator("#editor")
        editor.fill("int main(void){int score=5; sco")
        editor.press("Control+Space")
        expect(page.locator("#completion")).to_be_visible(timeout=15000)
        expect(page.locator("#completion")).to_contain_text("score")
        editor.press("Tab")
        assert editor.input_value().endswith("score")
        editor.fill("struct S{int score;};int main(void){struct S item;item.sc")
        editor.press("Control+Space")
        expect(page.locator("#completion")).to_be_visible(timeout=15000)
        expect(page.locator("#completion")).to_contain_text("score", timeout=15000)
        editor.press("Enter")
        assert editor.input_value().endswith("item.score")
        # pr 只显示匹配前缀，按当前、外层、全局作用域排序。
        ranked = 'int prefix;int main(void){int project;int a;{int prize;pr}}'
        editor.fill(ranked)
        editor.evaluate("e => { const p=e.value.lastIndexOf('pr}}')+2; e.setSelectionRange(p,p); }")
        editor.press("Control+Space")
        expect(page.locator("#completion")).to_be_visible(timeout=15000)
        candidate_labels = page.locator('#completion button > span:nth-child(2)').all_text_contents()
        assert candidate_labels == ["prize", "project", "prefix", "printf"], candidate_labels
        editor.press("Tab")
        assert editor.input_value().endswith('prize}}')
        exact = 'int pr;int main(void){int project;pr}'
        editor.fill(exact)
        editor.evaluate("e => e.setSelectionRange(e.value.length-1,e.value.length-1)")
        editor.press("Control+Space")
        expect(page.locator("#completion")).to_be_visible(timeout=15000)
        assert page.locator('#completion button > span:nth-child(2)').first.text_content() == 'pr'
        editor.fill('int main(void){int a;pr}')
        editor.evaluate("e => e.setSelectionRange(e.value.length-1,e.value.length-1)")
        # 直接输入 pr 时，第一个且唯一的候选应是 printf。
        editor.press("Control+Space")
        expect(page.locator("#completion")).to_be_visible(timeout=15000)
        assert page.locator('#completion button > span:nth-child(2)').all_text_contents() == ['printf']
        editor.press("ArrowLeft")
        expect(page.locator("#completion")).to_be_hidden()
        editor.press("Tab")
        assert 'printf' not in editor.input_value(), '光标移动后不能插入旧候选'
        # 空前缀可以显示 a/main，移动回 pr 后必须清除这份旧列表。
        editor.fill('int main(void){int a;pr; }')
        editor.evaluate("e => e.setSelectionRange(e.value.length-1,e.value.length-1)")
        editor.press("Control+Space")
        expect(page.locator("#completion")).to_be_visible(timeout=15000)
        assert 'a' in page.locator('#completion button > span:nth-child(2)').all_text_contents()
        editor.press("ArrowLeft")
        editor.press("ArrowLeft")
        expect(page.locator("#completion")).to_be_hidden()
        editor.fill('int main(void){ printf("sco')
        editor.press("Control+Space")
        expect(page.locator("#completion")).to_be_hidden(timeout=15000)

        page.locator("#example").select_option("3")
        expect(page.locator("#editor")).to_have_value(page.locator("#editor").input_value())
        page.locator("#run").click()
        expect(page.locator("#status")).to_have_text("运行完成", timeout=20000)
        expect(page.locator("#output")).to_contain_text("average = 75.000000")
        page.locator("#file").select_option("config.h")
        expect(editor).to_have_value(editor.input_value())
        assert "PASS_SCORE" in editor.input_value()
        editor.fill("#define PASS_SCORE 85\n#define STUDENT_COUNT 3\n")
        page.locator("#file").select_option("main.c")
        page.locator("#run").click()
        expect(page.locator("#status")).to_have_text("运行完成", timeout=20000)
        expect(page.locator("#output")).to_contain_text("passed = 1")
        page.locator("#example").select_option("5")
        page.locator("#run").click()
        expect(page.locator("#status")).to_have_text("编译失败 · 查看诊断", timeout=20000)
        expect(page.locator("#diagnostics")).to_contain_text("SEM_UNDECLARED")
        page.locator(".diagnostic").click()
        expect(page.locator("#position")).to_contain_text("行 3")

        # 从界面新建源文件与头文件，验证实际编译、保存和重新导入。
        editor.press("Control+n")
        expect(page.locator("#new-file-dialog")).to_be_visible()
        page.locator("#new-file-name").fill("../demo.c")
        page.locator("#new-file-form button[type=submit]").click()
        expect(page.locator("#new-file-error")).to_contain_text("项目内")
        page.locator("#new-file-name").fill("MAIN.c")
        page.locator("#new-file-form button[type=submit]").click()
        expect(page.locator("#new-file-error")).to_contain_text("已存在")
        page.locator("#new-file-name").fill("src/demo.c")
        page.locator("#new-file-form button[type=submit]").click()
        expect(page.locator("#new-file-dialog")).not_to_be_visible()
        expect(page.locator("#file")).to_have_value("src/demo.c")
        expect(page.locator("#entry")).to_have_value("src/demo.c")
        expect(editor).to_have_value("int main(void) {\n    return 0;\n}\n")
        editor.fill('int main(void){int score=5; sco')
        editor.press("Control+Space")
        expect(page.locator("#completion")).to_be_visible(timeout=15000)
        expect(page.locator("#completion")).to_contain_text("score")
        editor.press("Tab")
        assert editor.input_value().endswith("score")
        editor.fill('#include "../inc/config.h"\nint main(void){printf("%d\\n",N);return 0;}\n')
        # 未保存的新源文件不能直接切换到新建头文件。
        page.locator("#add-file").click()
        expect(page.locator("#unsaved-file-dialog")).to_be_visible()
        page.locator("#stay-file").click()
        expect(page.locator("#new-file-dialog")).not_to_be_visible()
        expect(page.locator("#file")).to_have_value("src/demo.c")
        with page.expect_download() as source_download:
            editor.press("Control+s")
        source_path = args.output / "demo.c"
        source_download.value.save_as(str(source_path))
        assert source_path.read_text(encoding="utf-8") == editor.input_value()
        page.locator("#add-file").click()
        expect(page.locator("#new-file-dialog")).to_be_visible()
        page.locator("#new-file-name").fill("inc/config.h")
        page.locator("#new-file-form button[type=submit]").click()
        expect(page.locator("#file")).to_have_value("inc/config.h")
        expect(page.locator("#entry")).to_have_value("src/demo.c")
        editor.fill("#define N 42\n")
        page.locator("#run").click()
        expect(page.locator("#status")).to_have_text("运行完成", timeout=20000)
        expect(page.locator("#output")).to_have_text("42\n")
        page.locator(".ast-node").first.click()
        expect(page.locator("#unsaved-file-dialog")).to_be_visible()
        with page.expect_download() as header_download:
            page.locator("#save-file-and-continue").click()
        header_path = args.output / "config.h"
        header_download.value.save_as(str(header_path))
        assert header_path.read_text(encoding="utf-8") == "#define N 42\n"
        expect(page.locator("#file")).to_have_value("src/demo.c")
        page.locator("#entry").select_option("main.c")
        page.locator("#run").click()
        expect(page.locator("#status")).to_have_text("编译失败 · 查看诊断", timeout=20000)
        page.locator("#entry").select_option("src/demo.c")
        with page.expect_download() as saved:
            page.locator("#save-project").click()
        saved_path = args.output / "project.json"
        saved.value.save_as(str(saved_path))
        project = json.loads(saved_path.read_text(encoding="utf-8"))
        assert project["entry"] == "src/demo.c" and "main.c" in project["files"]
        project["files"].pop("main.c")
        page.locator("#upload").set_input_files({"name": "project.json", "mimeType": "application/json", "buffer": json.dumps(project).encode("utf-8")})
        expect(page.locator("#entry")).to_have_value("src/demo.c")
        assert page.locator('#file option[value="main.c"]').count() == 0
        page.locator("#run").click()
        expect(page.locator("#status")).to_have_text("运行完成", timeout=20000)
        expect(page.locator("#output")).to_have_text("42\n")
        page.locator("#upload").set_input_files({"name": "imported.c", "mimeType": "text/plain", "buffer": b'int main(void){printf("imported");return 0;}'})
        expect(page.locator("#file")).to_have_value("imported.c")
        expect(page.locator("#entry")).to_have_value("imported.c")
        assert page.locator('#file option[value="src/demo.c"]').count() == 1
        page.locator("#run").click()
        expect(page.locator("#status")).to_have_text("运行完成", timeout=20000)
        expect(page.locator("#output")).to_have_text("imported")
        editor.press("Control+n")
        expect(page.locator("#new-file-dialog")).to_be_visible()
        page.locator("#new-file-name").fill("scratch.c")
        page.locator("#new-file-form button[type=submit]").click()
        editor.fill("int main(void){return 123;}")
        page.locator("#file").select_option("imported.c")
        expect(page.locator("#unsaved-file-dialog")).to_be_visible()
        expect(page.locator("#file")).to_have_value("scratch.c")
        page.locator("#stay-file").click()
        expect(editor).to_have_value("int main(void){return 123;}")
        page.locator("#file").select_option("imported.c")
        expect(page.locator("#unsaved-file-dialog")).to_be_visible()
        page.evaluate("() => { window.showSaveFilePicker = async () => { throw new DOMException('Cancelled', 'AbortError'); }; }")
        page.locator("#save-file-and-continue").click()
        expect(page.locator("#unsaved-file-dialog")).to_be_visible()
        expect(page.locator("#file")).to_have_value("scratch.c")
        page.evaluate("window.showSaveFilePicker = undefined")
        page.locator("#stay-file").click()
        page.locator("#import").click()
        expect(page.locator("#unsaved-file-dialog")).to_be_visible()
        page.locator("#stay-file").click()
        editor.press("Control+n")
        expect(page.locator("#unsaved-file-dialog")).to_be_visible()
        page.keyboard.press("Escape")
        expect(page.locator("#unsaved-file-dialog")).not_to_be_visible()
        expect(page.locator("#file")).to_have_value("scratch.c")
        page.locator("#entry").select_option("imported.c")
        expect(page.locator("#unsaved-file-dialog")).to_be_visible()
        page.locator("#discard-file").click()
        expect(page.locator("#file")).to_have_value("imported.c")
        assert page.locator('#file option[value="scratch.c"]').count() == 0
        page.set_viewport_size({"width": 390, "height": 844})
        page.locator("#add-file").click()
        expect(page.locator("#new-file-dialog")).to_be_visible()
        page.screenshot(path=str(args.output / "mobile.png"), full_page=True)
        page.locator("#cancel-new-file").click()
        assert page.evaluate("document.documentElement.scrollWidth <= window.innerWidth"), "手机页面横向溢出"
        assert not errors, errors
        browser.close()
    print("browser: execution, completion, AST export, Ctrl+N/Ctrl+S, save/discard guards, entry selection, project roundtrip, import, diagnostics and responsive layout passed")


if __name__ == "__main__":
    main()
