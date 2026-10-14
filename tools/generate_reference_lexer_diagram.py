"""按参考图生成单张词法识别图，并核对正常识别语言。"""
from __future__ import annotations

import argparse
from collections import defaultdict, deque
import hashlib
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.generate_lexer_dfa import NFA, Parser, SPEC, derive, expand

REFERENCE = ROOT / "tools/资源/词法参考布局.drawio"
TARGET = ROOT / "实验设计/词法分析/状态转换图/词法状态转换图.drawio"
NORMAL_RULES = [rule for rule in SPEC["rules"] if rule["action"] != "error"]
TAIL = r'''[abfnrtv\\'"?]|[0-7][0-7]?[0-7]?|x{HEX_DIGIT}+'''


def expressions():
    """返回参考图各条边实际读取的字节或序列正则。"""
    result = {
        0: r"[ \t\v\f]", 1: r"[ \t\v\f]", 2: r"\r\n|\r|\n",
        3: "{LETTER}", 4: "{ID_CONTINUE}", 5: 'L?"',
        6: "{STRING_CHAR}", 7: '"', 8: r"\\", 9: TAIL,
        10: "L?'", 11: "{CHAR_CHAR}|{ESCAPE}",
        12: "{CHAR_CHAR}|{ESCAPE}", 13: "'",
        14: "[1-9]", 15: "{DIGIT}", 20: "0", 21: "[0-7]",
        26: "0[xX]", 27: "{HEX_DIGIT}", 28: "{HEX_DIGIT}",
        33: "{DIGIT}+", 34: r"\.", 35: "{DIGIT}",
        36: r"\.{DIGIT}+", 37: "{DIGIT}", 38: "[Ee]",
        39: "[+-]", 40: "{DIGIT}", 41: "{DIGIT}",
        42: "{DIGIT}", 43: "[FfLl]", 44: "[FfLl]",
        45: "{DIGIT}+", 46: "{DIGIT}", 47: "[Ee]", 48: "[+-]",
        49: "{DIGIT}", 50: "{DIGIT}", 51: "{DIGIT}", 52: "[FfLl]",
        53: "/", 54: "/", 55: r"[^\r\n]", 56: "=",
        57: r"\*", 58: "[^*]", 59: r"\*", 60: r"\*",
        61: "/", 62: "[^*/]", 99: "[(),:;?\\[\\]{}~]",
    }
    for first in (16, 22, 29):
        result.update({first: "[Ll]", first + 1: "[Uu]",
                       first + 2: "[Uu]", first + 3: "[Ll]"})
    source = ET.parse(REFERENCE).getroot()
    cells = {cell.get("id"): cell for cell in source.iter("mxCell")}
    for index in range(63, 99):
        result[index] = re.escape(cells[f"el{index}"].get("value"))
    return result


def accepting():
    """返回接受状态对应的词法类别，固定符号归入同一类。"""
    result = {1: "WHITESPACE", 2: "NEWLINE", 3: "ID",
              5: "STRING_LITERAL", 9: "CHAR_LITERAL", 36: "LINE_COMMENT",
              40: "BLOCK_COMMENT"}
    result.update({state: "INT_LITERAL" for state in (*range(10, 18), *range(19, 23))})
    result.update({state: "FLOAT_LITERAL" for state in (24, 27, 28, 29, 33, 34)})
    result.update({state: "SYMBOL" for state in (35, 37, *range(41, 43), *range(45, 75), 76, 77)})
    return {f"q{state}": name for state, name in result.items()}


def generate():
    """沿用参考图的位置、弧形自循环和局部返回边，返回原生 XML。"""
    root = ET.parse(REFERENCE).getroot()
    root.set("diagramKind", "normal-recognition-with-macro-edges")
    root.set("rulesSha256", hashlib.sha256((ROOT / "src/词法分析/词法规则.json").read_bytes()).hexdigest())
    diagram = root.find("diagram")
    diagram.set("id", "reference")
    diagram.set("name", "词法状态转换总图")
    model = diagram.find("mxGraphModel")
    model.set("pageHeight", "9350")
    parent = model.find("root")
    # 历史图源的头文件上下文不属于当前普通词法规则。
    remove = {f"q{n}" for n in range(78, 83)}
    remove |= {f"innerq{n}" for n in range(78, 83)}
    remove |= {f"e{n}" for n in range(100, 106)}
    remove |= {f"el{n}" for n in range(100, 106)}
    remove |= {f"tx{n}" for n in range(77, 93)}
    # 当前规则已取消预处理符号，保留其余参考图编号以便对照。
    remove |= {"q43", "q44", "innerq43", "innerq44", "e65", "e66",
               "el65", "el66", "tx41", "tx42"}
    for cell in list(parent):
        if cell.get("id") in remove:
            parent.remove(cell)
    regexes, actions = expressions(), accepting()
    for cell in parent:
        ident = cell.get("id", "")
        if ident in actions:
            cell.set("acceptingClass", actions[ident])
        if re.fullmatch(r"q\d+", ident):
            cell.set("chartState", ident)
            cell.set("style", cell.get("style") + "fontFamily=Microsoft YaHei;")
        if cell.get("edge") == "1" and cell.get("source"):
            cell.set("style", cell.get("style") + "rounded=0;")
            cell.set("inputRegex", regexes[int(ident[1:])])
            cell.set("inputLabel", next(c for c in parent if c.get("id") == "el" + ident[1:]).get("value"))
        if re.fullmatch(r"el\d+", ident):
            cell.set("labelFor", "e" + ident[2:])
        if ident == "tx94":
            cell.set("value", "一张总图 · 起始共用主干 · 弧形自循环 · 分支内局部返回")
            cell.find("mxGeometry").set("width", "1900")
        if ident == "tx38":
            cell.set("value", "11  运算符")
        if ident in ("tx29", "tx32"):
            # 浮点后缀的返回箭头从右侧进入，接受说明放在圆圈下方。
            geometry = cell.find("mxGeometry")
            state = next(c for c in parent if c.get("id") == ("q29" if ident == "tx29" else "q34"))
            position = state.find("mxGeometry")
            geometry.set("x", str(float(position.get("x")) - 80))
            geometry.set("y", str(float(position.get("y")) + 75))
            geometry.set("width", "220")
            cell.set("style", cell.get("style").replace("align=left;", "align=center;"))
        upper_notes = {"tx26": "q24", "tx27": "q27", "tx31": "q33", "tx34": "q35"}
        if ident in upper_notes:
            # 圆圈下方有返回或分叉线时，将接受说明移到右上方的空白处。
            position = next(c for c in parent if c.get("id") == upper_notes[ident]).find("mxGeometry")
            geometry = cell.find("mxGeometry")
            geometry.set("x", str(float(position.get("x")) + 79))
            geometry.set("y", str(float(position.get("y")) - 36))
            geometry.set("width", "220")
            cell.set("style", cell.get("style").replace("align=center;", "align=left;"))
    notes = [
        ("读图说明", 23),
        ("圆圈为状态，双圈为接受状态；ID 识别完成后查 32 个关键字。最长匹配及同长优先级按项目规则。", 17),
        ("L? 为可选 L；数字+ 为至少一位数字；0x / 0X、CRLF 等序列写在同一条边上。", 17),
        ("Cs：除双引号、反斜杠、LF、CR 外的字节。Cc：除单引号、反斜杠、LF、CR 外的字节。", 17),
        ("E = 反斜杠后接 T。T：简单转义尾部，或 1–3 位八进制数字，或 x 后接至少一位十六进制数字。", 17),
        ("简单转义尾部：a b f n r t v、反斜杠、单引号、双引号、?。H = 0–9 / A–F / a–f。", 17),
        ("图中按正常识别规则展开，Q 为本图编号；宏边可消耗一个序列，重叠入口表示并行规则候选。", 17),
        ("非法数字、非法转义、未闭合字面量和注释等交错误处理；08、1e+、0x、1uu 仍为词法错误。", 17),
    ]
    for index, (value, size) in enumerate(notes):
        cell = ET.SubElement(parent, "mxCell", id=f"note{index}", value=value,
                             vertex="1", parent="1", style=f"text;html=0;fillColor=none;strokeColor=none;"
                             f"fontFamily=Microsoft YaHei;fontSize={size};align=left;verticalAlign=middle;")
        ET.SubElement(cell, "mxGeometry", x="90", y=str(8750 + index * 50), width="2250", height="40", **{"as": "geometry"})
    ET.indent(root, space="  ")
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def verify(path):
    """核对图形结构，并证明正常完整单词的接受类别与当前规则一致。"""
    root = ET.parse(path).getroot()
    assert len(root.findall("diagram")) == 1
    cells = {cell.get("id"): cell for cell in root.iter("mxCell")}
    expected = ET.fromstring(generate())
    source_cells = {cell.get("id"): cell for cell in expected.iter("mxCell")}
    states = {ident: cell for ident, cell in cells.items() if cell.get("chartState")}
    assert set(states) == {f"q{n}" for n in range(78) if n not in (43, 44)}
    edges = [cell for cell in cells.values() if cell.get("inputRegex")]
    assert len(edges) == 98
    starts, loops = [], 0
    for cell in edges:
        ident = cell.get("id")
        reference = source_cells[ident]
        for attr in ("source", "target", "inputRegex", "inputLabel", "style"):
            assert cell.get(attr) == reference.get(attr), (ident, attr)
        label = cells["el" + ident[1:]]
        assert label.get("value") == cell.get("inputLabel")
        assert label.get("labelFor") == ident
        points = cell.find("mxGeometry/Array")
        expected_points = reference.find("mxGeometry/Array")
        assert [(float(p.get("x")), float(p.get("y"))) for p in ([] if points is None else points)] == [
            (float(p.get("x")), float(p.get("y"))) for p in ([] if expected_points is None else expected_points)]
        if cell.get("source") == "q0":
            starts.append(points[0].attrib)
        loops += cell.get("source") == cell.get("target")
    assert all(point == starts[0] for point in starts), "起始分派必须重叠共用主干"
    actions = accepting()
    for ident, cell in states.items():
        assert cell.get("acceptingClass") == actions.get(ident)
        assert ("inner" + ident in cells) == (ident in actions)
        assert cell.find("mxGeometry").attrib == source_cells[ident].find("mxGeometry").attrib

    # 将宏边展开为字节 NFA，用状态对穷尽比较整个正常识别语言。
    nfa = NFA()
    nodes = {ident: nfa.new() for ident in states}
    for cell in edges:
        begin, end = nfa.build(Parser(expand(cell.get("inputRegex"))).parse())
        nfa.eps[nodes[cell.get("source")]].append(begin)
        nfa.eps[end].append(nodes[cell.get("target")])
    reference = derive(NORMAL_RULES)
    categories = ["SYMBOL" if "literal" in rule else rule["name"] for rule in NORMAL_RULES]
    accepted = {nodes[ident]: category for ident, category in actions.items()}
    initial = (nfa.closure([nodes["q0"]]), 0)
    queue, seen = deque([initial]), {initial}
    witnesses = {initial: b""}
    while queue:
        subset, state = queue.popleft()
        got = {accepted[node] for node in subset if node in accepted}
        index = reference["accept"][state] if state >= 0 else -1
        wanted = {categories[index]} if index >= 0 else set()
        assert got == wanted, f"正常完整单词的接受类别不同: {witnesses[subset, state]!r}: {got} / {wanted}"
        moves = defaultdict(set)
        for node in subset:
            for members, target in nfa.edges[node]:
                for byte in members:
                    moves[byte].add(target)
        for byte in range(256):
            next_state = reference["full"][state][byte] if state >= 0 else -1
            pair = nfa.closure(moves[byte]), next_state
            if pair not in seen:
                seen.add(pair)
                witnesses[pair] = witnesses[subset, state] + bytes([byte])
                queue.append(pair)
    symbols = [rule for rule in NORMAL_RULES if "literal" in rule]
    assert len(symbols) == 46
    print(f"PASS: one canvas / {len(states)} states / {len(edges)} paths / {loops} curved loops; "
          f"{len(starts)} start branches share one stem; normal recognition language matches "
          f"current rules for all bytes ({len(seen)} state pairs, {len(symbols)} fixed symbols)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", type=Path, help="核对网站保存的图稿")
    args = parser.parse_args()
    if args.check:
        verify(args.check)
    else:
        TARGET.parent.mkdir(parents=True, exist_ok=True)
        TARGET.write_bytes(generate())
        verify(TARGET)
