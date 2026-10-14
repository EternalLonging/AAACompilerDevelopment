"""生成 diagrams.net 词法图稿，并核对保存后的全部字节转移。"""
from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.generate_lexer_dfa import SPEC, derive

OUT = ROOT / "实验设计" / "diagramsnet_当前词法"
TARGET = OUT / "词法状态转换图.drawio"
DATA = derive(SPEC["rules"])
GROUPS = [
    ("05", "空白与标识符", {1, 2, 3, 4, 26, 27}),
    ("06", "整数与进制入口", {18, 19, 55}),
    ("07", "小数与指数", {16, 50, 51, 58, 77, 79, 80}),
    ("08", "十六进制、后缀与数字错误", {56, 57, 59, 60, 61, 62, 81, 82, 83}),
    ("09", "字符串与转义", {6, 36, 37, 71, 72, 86, 87}),
    ("10", "字符常量与转义", {9, 41, 42, 43, 73, 74, 75, 88}),
    ("11", "斜杠与注释", {17, 52, 53, 54, 78, 89}),
]
assigned = set().union(*(states for _, _, states in GROUPS))
GROUPS.append(("12", "运算符与分隔符", set(range(1, DATA["state_count"])) - assigned))
OWNER = {s: page for page, _, states in GROUPS for s in states}
SHORT = {"WHITESPACE": "空白", "NEWLINE": "换行", "LINE_COMMENT": "行注释",
         "BLOCK_COMMENT": "块注释", "INT_LITERAL": "整数", "FLOAT_LITERAL": "浮点",
         "ID": "标识符", "STRING_LITERAL": "字符串", "CHAR_LITERAL": "字符",
         "UNTERMINATED_STRING": "串未闭合", "UNTERMINATED_CHAR": "字符未闭合",
         "UNTERMINATED_COMMENT": "注释未闭合", "INVALID_STRING": "非法字符串",
         "INVALID_CHAR_LITERAL": "非法字符", "INVALID_NUMBER": "非法数字",
         "INVALID_BYTE": "非法字节"}


def byte_text(byte):
    """返回一个输入字节的可见写法。"""
    return {9: r"\t", 10: r"\n", 11: r"\v", 12: r"\f", 13: r"\r", 32: "SP"}.get(
        byte, chr(byte) if 33 <= byte <= 126 else f"0x{byte:02X}")


def class_text(index):
    """返回输入类别的完整字节集合，连续字节写成范围。"""
    members = DATA["classes"][index]
    runs, begin, end = [], members[0], members[0]
    for byte in members[1:] + [None]:
        if byte is not None and byte == end + 1:
            end = byte
        else:
            runs.append(byte_text(begin) if begin == end else f"{byte_text(begin)}–{byte_text(end)}")
            begin = end = byte
    return ", ".join(runs)


def category_label(categories):
    """合并相邻类别编号，返回简短的箭头标签。"""
    runs, begin, end = [], categories[0], categories[0]
    for category in categories[1:] + [None]:
        if category is not None and category == end + 1:
            end = category
        else:
            runs.append(f"C{begin:02d}" if begin == end else f"C{begin:02d}–C{end:02d}")
            begin = end = category
    return ", ".join(runs)


def page(file, ident, name, width=1600, height=1100):
    diagram = ET.SubElement(file, "diagram", id=ident, name=f"{ident} {name}")
    model = ET.SubElement(diagram, "mxGraphModel", grid="1", gridSize="10", page="1",
                          pageScale="1", pageWidth=str(width), pageHeight=str(height),
                          background="#ffffff", math="0", shadow="0")
    root = ET.SubElement(model, "root")
    ET.SubElement(root, "mxCell", id="0")
    ET.SubElement(root, "mxCell", id="1", parent="0")
    text(root, "title", name, 40, 25, width - 80, 42, 27, True, "#172B4D")
    return root


def text(root, ident, value, x, y, width, height, size=16, bold=False, color="#42526E"):
    cell = ET.SubElement(root, "mxCell", id=ident, value=value, vertex="1", parent="1",
                         style=f"text;html=0;whiteSpace=wrap;align=left;verticalAlign=middle;"
                         f"fontFamily=Microsoft YaHei;fontSize={size};fontColor={color};"
                         f"fontStyle={int(bold)};strokeColor=none;fillColor=none;")
    ET.SubElement(cell, "mxGeometry", x=str(x), y=str(y), width=str(width), height=str(height), **{"as": "geometry"})
    return cell


def graph_page(file, ident, title, sources, edges):
    root = page(file, ident, title)
    text(root, "subtitle", "逐字节 DFA · 双圈为接受状态（含跳过与报错）· 虚线状态请接着查看标注页", 40, 78, 1500, 28)
    text(root, "legend", "绿色：输出 Token    青色：跳过    红色：诊断候选    灰色：跨页入口    每条边消耗一个字节", 40, 112, 1500, 28, 15)
    states = sorted(sources | {target for _, target in edges})
    for index, state in enumerate(states):
        rule_index = DATA["accept"][state]
        rule = SPEC["rules"][rule_index] if rule_index >= 0 else None
        external = state not in sources
        label = f"S{state}"
        if rule:
            label += "\n" + SHORT.get(rule["name"], rule.get("literal", rule["name"]))
        if external:
            label += f"\n见 {OWNER[state]} 页"
        color = {"token": ("#E7F5EC", "#28844C"), "skip": ("#E5F6F8", "#25838B"),
                 "error": ("#FDECEA", "#C8473F")}.get(rule["action"] if rule else "", ("#EAF0FF", "#4267B2"))
        if external:
            color = ("#F4F5F7", "#7A869A")
        obj = ET.SubElement(root, "object", id=f"s{state}", label=label, dfaState=str(state),
                            dfaOwner=OWNER.get(state, "00"), dfaRole="portal" if external else "source",
                            acceptingRule=rule["name"] if rule else "", action=rule["action"] if rule else "")
        if external:
            obj.set("link", f"data:page/id,{OWNER[state]}")
        cell = ET.SubElement(obj, "mxCell", vertex="1", parent="1", style=
                            f"shape={'doubleEllipse' if rule else 'ellipse'};html=0;whiteSpace=wrap;"
                            f"aspect=fixed;fontFamily=Microsoft YaHei;fontSize=14;strokeWidth=1.7;"
                            f"fillColor={color[0]};strokeColor={color[1]};dashed={int(external)};")
        ET.SubElement(cell, "mxGeometry", x=str(60 + index % 6 * 220), y=str(200 + index // 6 * 180),
                      width="108", height="108", **{"as": "geometry"})
    if 0 in sources:
        cell = ET.SubElement(root, "mxCell", id="initial", value="开始", edge="1", parent="1", target="s0",
                             style="endArrow=block;endFill=1;html=0;fontSize=14;strokeWidth=1.7;")
        geometry = ET.SubElement(cell, "mxGeometry", relative="1", **{"as": "geometry"})
        ET.SubElement(geometry, "mxPoint", x="15", y="254", **{"as": "sourcePoint"})
    for (source, target), categories in sorted(edges.items()):
        label = category_label(categories)
        if len(categories) == 1 and len(DATA["classes"][categories[0]]) <= 3:
            label += "  " + class_text(categories[0])
        obj = ET.SubElement(root, "object", id=f"e{source}-{target}", label=label,
                            dfaSource=str(source), dfaTarget=str(target), dfaClasses=",".join(map(str, categories)))
        cell = ET.SubElement(obj, "mxCell", edge="1", parent="1", source=f"s{source}", target=f"s{target}", style=
                            "edgeStyle=orthogonalEdgeStyle;rounded=1;html=0;endArrow=block;endFill=1;"
                            "fontFamily=Microsoft YaHei;fontSize=13;labelBackgroundColor=#ffffff;"
                            "strokeColor=#52657A;strokeWidth=1.4;jettySize=30;")
        ET.SubElement(cell, "mxGeometry", relative="1", **{"as": "geometry"})


def generate():
    file = ET.Element("mxfile", host="app.diagrams.net", type="device", version="29.0.0")
    root = page(file, "00", "当前词法状态转换图 · 阅读导航")
    blocks = [
        ("90 个状态 / 47 类字节 / 62 条规则", "从 lexer/patterns.json 推导，状态编号与当前 src/lexer_dfa.hpp 一致。\n当前词法保留 C89 形式；MiniC 语法会进一步拒绝范围外的关键字和运算符。"),
        ("01–04  起始分派", "从 S0 按首字节进入各分支。四页合起来是 S0 的全部有效转移。"),
        ("05–12  各类单词的精确转移", "05 空白与标识符 · 06 整数 · 07 小数和指数 · 08 十六进制、后缀和数字错误\n09 字符串 · 10 字符常量 · 11 注释 · 12 运算符和分隔符"),
        ("13  输入字节类别", "边上的 C00–C46 对应精确的输入字节集合；共覆盖 0–255，任意两类不相交。\n箭头每次读取一个字节；反斜杠转义也是实际输入字节，不能把它当作图中的宏边。"),
        ("14  接受动作与扫描约定", "圆圈：非接受状态；双圈：接受状态。绿色输出 Token，青色跳过，红色记录错误候选。\n虚线圆圈是同一个 S 状态的跨页副本，点击可跳到该状态的后续转移页。"),
        ("最长匹配决定最终动作", "继续读入并记录最后接受状态；没有可用转移时退回最后接受位置，再执行输出、跳过或诊断。\n红色接受状态也可继续转移；例如 S62 读到 0x 时是错误候选，再读合法十六进制数字会转为合法整数。"),
        ("完整性", "图中包含全部有效转移和自循环。没有画出的转移对应表中的 -1，表示本次匹配停止。\nEOF 由扫描驱动在输入结束或错误上限处生成，不属于字节 DFA 的状态；没有头文件上下文分支。"),
    ]
    for i, (title, body) in enumerate(blocks):
        text(root, f"h{i}", title, 55, 110 + i * 130, 1450, 35, 21, True)
        text(root, f"b{i}", body, 55, 150 + i * 130, 1450, 65, 17)
    dispatch = [
        ("01", "起始分派 · 空白与标识符", {1, 2, 3, 4, 26, 27}),
        ("02", "起始分派 · 字面量与点号", {6, 9, 16, 18, 19}),
        ("03", "起始分派 · 运算符", {5, 7, 8, 12, 13, 15, 17, 22, 23, 24, 30, 32}),
        ("04", "起始分派 · 分隔符", {10, 11, 14, 20, 21, 25, 28, 29, 31, 33, 34}),
    ]
    for ident, title, targets in dispatch:
        edges = defaultdict(list)
        for c, target in enumerate(DATA["matrix"][0]):
            if target in targets:
                edges[0, target].append(c)
        graph_page(file, ident, title, {0}, edges)
    for ident, title, sources in GROUPS:
        edges = defaultdict(list)
        for source in sorted(sources):
            for c, target in enumerate(DATA["matrix"][source]):
                if target >= 0:
                    edges[source, target].append(c)
        graph_page(file, ident, title, sources, edges)
    root = page(file, "13", "输入字节类别 · 精确集合", 1800, 1400)
    text(root, "subtitle", r"SP 为空格；\t / \v / \f / \r / \n 为控制字节；0xHH 为十六进制字节；范围含两端。", 40, 80, 1700, 40, 17)
    for i in range(len(DATA["classes"])):
        col, row = divmod(i, 16)
        text(root, f"c{i}", f"C{i:02d}    {class_text(i)}", 45 + col * 585, 145 + row * 73, 550, 65, 16)
    root = page(file, "14", "接受动作 · 完整状态索引", 1800, 1950)
    text(root, "subtitle", "以下动作在最长匹配结束后执行。同长按 rules 顺序优先；ID 识别完整后再查 32 个关键字。", 40, 80, 1700, 40, 17)
    for state, index in enumerate(DATA["accept"]):
        col, row = divmod(state, 30)
        rule = SPEC["rules"][index] if index >= 0 else None
        action = (rule.get("token") or rule.get("code") or rule["name"]) if rule else "非接受"
        text(root, f"a{state}", f"S{state:02d}  {rule['action'] if rule else '—'}  {action}", 40 + col * 585, 140 + row * 56, 570, 48, 15)
    ET.indent(file, space="  ")
    return ET.tostring(file, encoding="utf-8", xml_declaration=True)


def verify(path):
    """检查图稿和当前 DFA 的逐字节转移、状态及接受动作完全相同。"""
    root = ET.parse(path).getroot()
    actual, states, grouped_count = {}, set(), 0
    for diagram in root.findall("diagram"):
        ids = {node.get("id") for node in diagram.iter() if node.get("id")}
        for obj in diagram.iter("object"):
            if "dfaState" in obj.attrib:
                state = int(obj.get("dfaState"))
                states.add(state)
                index = DATA["accept"][state]
                rule = SPEC["rules"][index] if index >= 0 else None
                assert obj.get("acceptingRule") == (rule["name"] if rule else "")
                assert obj.get("action") == (rule["action"] if rule else "")
                style = obj.find("mxCell").get("style")
                assert ("shape=doubleEllipse;" in style) == bool(rule)
            if "dfaSource" not in obj.attrib:
                continue
            grouped_count += 1
            source, target = int(obj.get("dfaSource")), int(obj.get("dfaTarget"))
            cell = obj.find("mxCell")
            assert cell.get("source") in ids and cell.get("target") in ids
            for category in map(int, obj.get("dfaClasses").split(",")):
                for byte in DATA["classes"][category]:
                    key = source, byte
                    assert key not in actual, f"重复转移: {key}"
                    actual[key] = target
    expected = {(s, b): target for s, row in enumerate(DATA["full"]) for b, target in enumerate(row) if target >= 0}
    assert actual == expected, f"转移不一致: {len(actual)} / {len(expected)}"
    assert states == set(range(DATA["state_count"]))
    assert len(root.findall("diagram")) == 15
    print(f"PASS: 15 页 / {len(states)} 状态 / {grouped_count} 组箭头 / {len(actual)} 条有效字节转移；接受动作一致")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", type=Path, help="核对网站保存后的 drawio 文件")
    args = parser.parse_args()
    if args.check:
        verify(args.check)
    else:
        OUT.mkdir(parents=True, exist_ok=True)
        TARGET.write_bytes(generate())
        verify(TARGET)
