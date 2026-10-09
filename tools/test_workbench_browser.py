"""在已启动的工作台上验证编辑、补全、图形展示与优化对比。需 Playwright。"""

import argparse
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
        page.set_viewport_size({"width": 390, "height": 844})
        page.screenshot(path=str(args.output / "mobile.png"), full_page=True)
        assert page.evaluate("document.documentElement.scrollWidth <= window.innerWidth"), "手机页面横向溢出"
        assert not errors, errors
        browser.close()
    print("browser: execution, scope/member completion, AST export, header editing, diagnostics and responsive layout passed")


if __name__ == "__main__":
    main()
