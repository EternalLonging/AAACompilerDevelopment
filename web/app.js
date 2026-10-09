"use strict";
const $ = id => document.getElementById(id);
const editor = $("editor"), result = $("result"), popup = $("completion");
let files = {"main.c": ""}, activeFile = "main.c", data = null, view = "ast", examples = [], dirty = false;
let revision = 0, completionTicket = 0, completionData = null, selected = 0, timer = null, busy = false;
const labels = {
  ast: ["SYNTAX TREE", "语法树", "真实节点与父子关系。点击节点可以定位到源码。"],
  tokens: ["LEXICAL ANALYSIS", "单词表", "预处理后的真实单词序列，位置映射到原始文件与行号。"],
  symbols: ["SYMBOL TABLE", "符号表", "变量、函数与形参的类型和作用域。不同编号区分同名符号。"],
  ir: ["INTERMEDIATE REPRESENTATION", "四元式与常量池", "默认开启常量折叠；操作数引用符号、临时变量或常量编号。"],
  compare: ["CONSTANT FOLDING", "优化前后对比", "同一棵语义树分别生成四元式。运行时分别执行，检查输出与退出码。"],
  preprocessed: ["PREPROCESSOR", "预处理源码", "查看宏和本地头文件展开后，交给词法分析的实际内容。"]
};
function toast(message) { $("toast").textContent = message; $("toast").hidden = false; clearTimeout(toast.timer); toast.timer = setTimeout(() => $("toast").hidden = true, 4500); }
function textElement(tag, text, cls) { const e = document.createElement(tag); e.textContent = text; if (cls) e.className = cls; return e; }
async function api(path, payload) {
  const response = await fetch(path, {method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify(payload)});
  const body = await response.json(); if (!response.ok || body.error) throw new Error(body.error || "操作失败"); return body;
}
function stash() { files[activeFile] = editor.value; }
function payload() { stash(); const headers = {...files}; delete headers["main.c"]; return {source: files["main.c"], files: headers, input: $("stdin").value}; }
function refreshFiles() { const select = $("file"); select.replaceChildren(); Object.keys(files).forEach(name => { const option = new Option(name, name); select.add(option); }); select.value = activeFile; }
function switchFile(name) { stash(); activeFile = name; editor.value = files[name]; $("file").value = name; hideCompletion(); updateEditor(); }
function updateEditor() {
  const lineCount = editor.value.split("\n").length;
  $("lines").textContent = Array.from({length: lineCount}, (_, i) => i + 1).join("\n");
  $("lines").scrollTop = editor.scrollTop;
  const before = editor.value.slice(0, editor.selectionStart).split("\n");
  $("position").textContent = `行 ${before.length} · 列 ${before.at(-1).length + 1}`;
}
function changed() { revision++; dirty = true; $("status").textContent = "源码已修改 · 请重新分析"; hideCompletion(); updateEditor(); }
function download(name, content, type = "text/plain;charset=utf-8") {
  const url = URL.createObjectURL(new Blob([content], {type})); const a = document.createElement("a"); a.href = url; a.download = name; a.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
}
function table(headers, rows) {
  const t = document.createElement("table"); t.className = "data-table";
  const head = t.createTHead().insertRow(); headers.forEach(h => head.append(textElement("th", h)));
  const body = t.createTBody(); rows.forEach(row => { const tr = body.insertRow(); row.forEach(cell => tr.append(textElement("td", String(cell ?? "")))); });
  return t;
}
function jump(line, filename = "main.c") {
  if (dirty) { toast("源码已变化，请重新分析后定位"); return; }
  const name = Object.keys(files).find(f => f === filename) || Object.keys(files).find(f => f.split("/").at(-1) === filename);
  if (!name) { toast("该节点来自未在编辑器打开的文件"); return; }
  if (name !== activeFile) switchFile(name);
  const parts = editor.value.split("\n"); line = Math.max(1, Math.min(line, parts.length));
  const start = parts.slice(0, line - 1).reduce((n, t) => n + t.length + 1, 0);
  editor.focus(); editor.setSelectionRange(start, start + parts[line - 1].length); editor.scrollTop = Math.max(0, (line - 3) * 24); updateEditor();
}
function graph(nodes) {
  const box = document.createElement("div"), toolbar = document.createElement("div"); toolbar.className = "graph-tools";
  const viewport = document.createElement("div"); viewport.className = "ast-viewport";
  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg"); svg.setAttribute("role", "img"); svg.setAttribute("aria-label", "抽象语法树图");
  const map = new Map(nodes.map(n => [n.id, {...n, children: []}])); const root = map.get(nodes[0]?.id);
  for (const n of map.values()) if (map.has(n.parent)) map.get(n.parent).children.push(n);
  let leaf = 0, maxDepth = 0;
  function layout(n, depth) { n.depth = depth; maxDepth = Math.max(depth, maxDepth); if (!n.children.length) n.x = leaf++ * 185 + 96; else { n.children.forEach(c => layout(c, depth + 1)); n.x = (n.children[0].x + n.children.at(-1).x) / 2; } n.y = depth * 96 + 40; }
  layout(root, 0);
  const width = Math.max(500, leaf * 185 + 10), height = (maxDepth + 1) * 96 + 10; let scale = .8;
  svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
  function svge(tag, attrs, text) { const e = document.createElementNS(svg.namespaceURI, tag); Object.entries(attrs).forEach(([k, v]) => e.setAttribute(k, v)); if (text != null) e.textContent = text; return e; }
  for (const n of map.values()) for (const child of n.children) svg.append(svge("path", {d: `M ${n.x} ${n.y + 26} C ${n.x} ${n.y + 58} ${child.x} ${child.y - 42} ${child.x} ${child.y - 26}`, class: "ast-edge"}));
  for (const n of map.values()) {
    const g = svge("g", {class: "ast-node", tabindex: "0", "data-node": n.kind, transform: `translate(${n.x},${n.y})`, role: "button", "aria-label": `${n.kind} ${n.name} 第${n.line}行`});
    g.append(svge("title", {}, `${n.kind} ${n.name}\n${n.type}\n${n.file}:${n.line}`));
    g.append(svge("rect", {x: -80, y: -26, width: 160, height: 53, rx: 5}));
    g.append(svge("text", {x: 0, y: -6, "text-anchor": "middle"}, (n.kind + (n.name ? " · " + n.name : "")).slice(0, 22)));
    g.append(svge("text", {x: 0, y: 12, "text-anchor": "middle", class: "node-type"}, n.type.slice(0, 24)));
    g.addEventListener("click", () => jump(n.line, n.file)); g.addEventListener("keydown", e => { if (e.key === "Enter") jump(n.line, n.file); }); svg.append(g);
  }
  function resize() { svg.style.width = width * scale + "px"; svg.style.height = height * scale + "px"; }
  for (const [name, action] of [["−", () => scale = Math.max(.2, scale - .15)], ["＋", () => scale = Math.min(2, scale + .15)], ["适应宽度", () => scale = Math.max(.2, Math.min(1, (result.clientWidth - 20) / width))]]) { const b = textElement("button", name); b.onclick = () => { action(); resize(); }; toolbar.append(b); }
  toolbar.append(textElement("span", `${nodes.length} 个真实节点 · 可缩放与滚动`)); viewport.append(svg); box.append(toolbar, viewport); resize(); return box;
}
function compare() {
  const b = data.baseline, a = data.optimized;
  const wrapper = document.createElement("div"), metrics = document.createElement("div"); metrics.className = "compare-summary";
  for (const [label, value] of [["未折叠指令数", b.quads.length], ["折叠后指令数", a.quads.length], ["减少指令", b.quads.length - a.quads.length]]) { const m = document.createElement("div"); m.className = "metric"; m.append(textElement("span", label), textElement("b", value)); metrics.append(m); }
  wrapper.append(metrics);
  const message = data.equivalent === true ? `两边输出与退出码一致 · 执行步数 ${b.run.steps} → ${a.run.steps}` : data.equivalent === false ? "两边运行结果不一致，请查看诊断" : "点击编译并运行，核对两边输出；运行失败时不宣称等价。";
  wrapper.append(textElement("div", message, "compare-status"));
  const columns = document.createElement("div"); columns.className = "compare-columns";
  for (const [name, side] of [["优化前 · 关闭常量折叠", b], ["优化后 · 开启常量折叠", a]]) { const col = document.createElement("div"); col.append(textElement("h3", name), textElement("pre", side.text)); if (side.run) col.append(textElement("pre", `输出：\n${side.run.output}\n退出码：${side.run.exit ?? "运行失败"}\n执行步数：${side.run.steps}`)); columns.append(col); }
  wrapper.append(columns); return wrapper;
}
function render() {
  const [kicker, title, desc] = labels[view]; $("view-kicker").textContent = kicker; $("view-title").textContent = title; $("view-description").textContent = desc;
  document.querySelectorAll(".stage-tabs button").forEach(b => b.classList.toggle("active", b.dataset.view === view)); result.replaceChildren();
  if (!data) { const e = document.createElement("div"); e.className = "empty"; e.append(textElement("span", "⌘"), textElement("h3", "先写一段代码"), textElement("p", "点击「分析源码」，查看编译器实际生成的结果。")); result.append(e); return; }
  if (view === "ast" && data.ast.length) result.append(graph(data.ast));
  else if (view === "tokens") result.append(table(["种别", "原文", "文件", "行"], data.tokens.map(t => [t.kind, t.text, t.file, t.line])));
  else if (view === "symbols") result.append(table(["编号", "名字", "类型", "作用域", "行"], data.symbols.map(s => ["%s" + s.id, s.name, s.type, s.scope, s.line])));
  else if (view === "ir" && data.optimized) {
    result.append(table(["函数", "序号", "op", "arg1", "arg2", "result"], data.optimized.quads.map(q => [q.function, q.index, q.op, q.arg1, q.arg2, q.result])));
    result.append(textElement("h3", "常量池", "plain"), table(["编号", "类型", "值"], data.optimized.constants.map(c => ["%c" + c.id, c.type, c.value])));
  } else if (view === "compare" && data.baseline) result.append(compare());
  else if (view === "preprocessed") result.append(textElement("pre", data.preprocessed, "plain"));
  else result.append(textElement("div", "该阶段未完成。请查看下方诊断并修改源码。", "empty"));
}
function showDiagnostics() {
  const list = [...data.diagnostics, ...(data.baseline?.diagnostics || []), ...(data.optimized?.diagnostics || []), ...(data.optimized?.run?.diagnostics || []), ...(data.baseline?.run?.diagnostics || [])];
  const unique = list.filter((d, i) => list.findIndex(x => x.phase === d.phase && x.code === d.code && x.line === d.line && x.message === d.message) === i);
  $("diagnostic-count").textContent = unique.length; $("diagnostics").replaceChildren();
  if (!unique.length) $("diagnostics").append(textElement("span", "当前检查没有诊断。", "muted"));
  for (const d of unique) { const e = document.createElement("div"); e.className = "diagnostic"; e.append(textElement("strong", `${d.phase} · ${d.file}:${d.line}`), textElement("span", `${d.message} (${d.code})`)); e.onclick = () => { if (dirty) toast("源码已变化，请重新分析后定位"); else jump(d.line, d.file); }; $("diagnostics").append(e); }
}
async function compile(run) {
  if (busy) return;
  busy = true; hideCompletion(); const version = revision; $("run").disabled = $("analyze").disabled = true; $("status").textContent = run ? "正在编译并执行…" : "正在分析…";
  try {
    const next = await api(run ? "/api/run" : "/api/analyze", payload());
    if (version !== revision) { toast("源码在编译中发生变化，请重新分析"); return; }
    data = next; dirty = false; render(); showDiagnostics(); const execution = data.optimized?.run;
    $("output").textContent = execution ? execution.output || "（程序未输出文字）" : "尚未执行。点击「编译并运行」。";
    $("run-detail").textContent = execution ? (execution.ok ? `退出码 ${execution.exit} · ${execution.steps} 步` : "执行失败") : "尚未执行";
    $("status").textContent = !data.ok ? "编译失败 · 查看诊断" : execution && !execution.ok ? "执行失败 · 查看诊断" : run ? "运行完成" : "分析完成";
  } catch (e) { toast(e.message); $("status").textContent = "操作失败"; }
  finally { busy = false; $("run").disabled = $("analyze").disabled = false; if (version !== revision) $("status").textContent = "源码已修改 · 请重新分析"; }
}
function hideCompletion() { popup.hidden = true; completionData = null; completionTicket++; }
async function complete(force = false) {
  if (activeFile !== "main.c" || busy) return;
  const source = editor.value, cursor = editor.selectionStart; if (editor.selectionEnd !== cursor) return;
  const word = source.slice(0, cursor).match(/[A-Za-z_][A-Za-z_0-9]*$/)?.[0] || "";
  if (!force && word.length < 2 && !/[.>]$/.test(source.slice(0, cursor))) return;
  const ticket = ++completionTicket;
  try {
    const body = await api("/api/complete", {source, cursor: new TextEncoder().encode(source.slice(0, cursor)).length});
    if (ticket !== completionTicket || source !== editor.value || cursor !== editor.selectionStart) return;
    completionData = {body, source, cursor}; selected = 0; popup.replaceChildren();
    if (!body.items.length) { popup.hidden = true; if (force) toast("当前位置没有可用候选"); return; }
    body.items.slice(0, 50).forEach((item, i) => { const b = document.createElement("button"); b.setAttribute("role", "option"); b.append(textElement("span", item.kind, "badge"), textElement("span", item.label), textElement("small", item.detail)); b.addEventListener("mousedown", e => { e.preventDefault(); insertCompletion(i); }); popup.append(b); });
    popup.hidden = false; popup.style.top = Math.min(205, Math.max(35, source.slice(0, cursor).split("\n").length * 24 - editor.scrollTop + 18)) + "px"; selectCompletion();
    $("completion-note").textContent = body.recovered ? "补全已修补未完成片段 · 运行仍需完整源码" : "补全来自当前位置的类型与作用域";
  } catch (e) { if (force) toast(e.message); }
}
function selectCompletion() { [...popup.children].forEach((e, i) => { e.classList.toggle("selected", i === selected); e.setAttribute("aria-selected", i === selected); }); popup.children[selected]?.scrollIntoView({block: "nearest"}); }
function insertCompletion(i = selected) {
  if (!completionData) return;
  const {body, source, cursor} = completionData;
  const begin = new TextDecoder().decode(new TextEncoder().encode(source).slice(0, body.begin)).length;
  editor.setRangeText(body.items[i].label, begin, cursor, "end"); changed(); editor.focus();
}
editor.addEventListener("input", () => { changed(); clearTimeout(timer); timer = setTimeout(() => complete(), 280); });
editor.addEventListener("click", () => { hideCompletion(); updateEditor(); });
editor.addEventListener("keyup", updateEditor); editor.addEventListener("scroll", () => { $("lines").scrollTop = editor.scrollTop; hideCompletion(); });
editor.addEventListener("keydown", e => {
  if (e.ctrlKey && e.code === "Space") { e.preventDefault(); complete(true); return; }
  if (e.ctrlKey && e.key === "Enter") { e.preventDefault(); compile(true); return; }
  if (!popup.hidden) {
    if (e.key === "ArrowDown" || e.key === "ArrowUp") { e.preventDefault(); selected = (selected + (e.key === "ArrowDown" ? 1 : -1) + popup.children.length) % popup.children.length; selectCompletion(); return; }
    if (e.key === "Tab" || e.key === "Enter") { e.preventDefault(); insertCompletion(); return; }
    if (e.key === "Escape") { e.preventDefault(); hideCompletion(); return; }
  }
  if (e.key === "Tab") { e.preventDefault(); editor.setRangeText("    ", editor.selectionStart, editor.selectionEnd, "end"); changed(); }
});
editor.addEventListener("blur", () => setTimeout(hideCompletion, 120));
$("file").onchange = e => switchFile(e.target.value);
$("stdin").oninput = changed;
$("run").onclick = () => compile(true); $("analyze").onclick = () => compile(false);
document.querySelectorAll(".stage-tabs button").forEach(b => b.onclick = () => { view = b.dataset.view; render(); });
$("add-file").onclick = () => { const name = prompt("本地头文件名，例如 inc/config.h"); if (!name) return; if (!/^[\w\u0080-\uffff /.-]+\.h$/.test(name) || name.split("/").includes("..") || name.startsWith("/")) { toast("请填写项目内的 .h 相对路径"); return; } stash(); files[name] ??= ""; refreshFiles(); switchFile(name); changed(); };
$("save-source").onclick = () => download(activeFile.split("/").at(-1), editor.value);
$("save-project").onclick = () => { stash(); download("minic-project.json", JSON.stringify({files, input: $("stdin").value}, null, 2), "application/json"); };
$("import").onclick = () => $("upload").click();
$("upload").onchange = async e => {
  const file = e.target.files[0]; if (!file) return;
  try {
    if (dirty && !confirm("导入会修改当前项目，是否继续？")) return;
    const text = await file.text();
    if (file.name.endsWith(".json")) { const project = JSON.parse(text); if (!project.files || typeof project.files["main.c"] !== "string" || Object.values(project.files).some(v => typeof v !== "string")) throw new Error("不是有效的工作台项目"); files = project.files; $("stdin").value = project.input || ""; activeFile = "main.c"; }
    else { stash(); const name = file.name.endsWith(".h") ? file.name : "main.c"; files[name] = text; activeFile = name; }
    editor.value = files[activeFile]; refreshFiles(); changed();
  } catch (err) { toast(err.message); }
  finally { e.target.value = ""; }
};
$("reset").onclick = () => { if (!confirm("清空当前项目？可先使用「保存项目」。")) return; files = {"main.c": ""}; activeFile = "main.c"; editor.value = ""; $("stdin").value = ""; data = null; refreshFiles(); changed(); render(); $("output").textContent = "输出将显示在这里。"; $("diagnostics").textContent = "尚未分析。"; $("diagnostic-count").textContent = "0"; };
$("export").onclick = () => { if (!data || dirty) { toast("请先分析当前源码"); return; } if (view === "ast") { const current = result.querySelector("svg"); if (!current) { toast("当前没有可导出的语法树"); return; } const svg = current.cloneNode(true); svg.style.removeProperty("width"); svg.style.removeProperty("height"); const style = document.createElementNS(svg.namespaceURI, "style"); style.textContent = ".ast-edge{stroke:#aec4a6;fill:none}.ast-node rect{fill:#fffef9;stroke:#b6cbb0}.ast-node text{font:11px monospace;fill:#183a35}.node-type{font-size:9px}"; svg.prepend(style); download("minic-ast.svg", new XMLSerializer().serializeToString(svg), "image/svg+xml"); } else if (view === "compare") download("minic-optimization.txt", "优化前\n" + (data.baseline?.text || "") + "\n优化后\n" + (data.optimized?.text || "")); else download("minic-results.json", JSON.stringify(data, null, 2), "application/json"); };
$("example").onchange = e => { if (!e.target.value) return; if (dirty && !confirm("载入示例会替换当前项目，是否继续？")) { e.target.value = ""; return; } const item = examples[Number(e.target.value) - 1]; files = {"main.c": item.source, ...item.files}; activeFile = "main.c"; editor.value = files[activeFile]; $("stdin").value = item.input; refreshFiles(); data = null; changed(); render(); };
async function init() { try { examples = await (await fetch("/api/examples")).json(); examples.forEach((e, i) => $("example").add(new Option(e.name, String(i + 1)))); files["main.c"] = examples[0].source; editor.value = files["main.c"]; refreshFiles(); updateEditor(); } catch (e) { toast("无法载入示例：" + e.message); } }
init();
