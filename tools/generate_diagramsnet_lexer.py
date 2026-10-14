"""生成单张词法总图，核对全部字节转移及共用出边。"""
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


def text(root, ident, value, x, y, width, height, size=16, bold=False, color="#42526E"):
    cell = ET.SubElement(root, "mxCell", id=ident, value=value, vertex="1", parent="1",
                         style=f"text;html=0;whiteSpace=wrap;align=left;verticalAlign=middle;"
                         f"fontFamily=Microsoft YaHei;fontSize={size};fontColor={color};"
                         f"fontStyle={int(bold)};strokeColor=none;fillColor=none;")
    ET.SubElement(cell, "mxGeometry", x=str(x), y=str(y), width=str(width), height=str(height), **{"as": "geometry"})
    return cell


def generate():
    """返回单张完整 DFA 总图的可编辑 XML。"""
    from tools.lexer_diagram_layout import generate_single
    return generate_single(DATA, SPEC, SHORT, text, class_text, category_label)


def verify(path):
    """核对单张图的状态、字节转移、接受动作及共用出口。"""
    root = ET.parse(path).getroot()
    assert len(root.findall("diagram")) == 1, "图稿必须只有一张画布"
    actual, states, grouped_count = {}, set(), 0
    stems, outgoing, node_boxes, label_boxes = {}, defaultdict(int), [], []
    edge_labels = {}
    for cell in root.iter("mxCell"):
        if cell.get("labelFor"):
            edge_labels[cell.get("labelFor")] = cell.get("value")
            geometry = cell.find("mxGeometry")
            label_boxes.append((cell.get("id"), *(float(geometry.get(k)) for k in ("x", "y", "width", "height"))))
    for diagram in root.findall("diagram"):
        ids = {node.get("id") for node in diagram.iter() if node.get("id")}
        for obj in diagram.iter("object"):
            if "dfaState" in obj.attrib:
                state = int(obj.get("dfaState"))
                assert state not in states, f"S{state} 不能重复出现"
                states.add(state)
                index = DATA["accept"][state]
                rule = SPEC["rules"][index] if index >= 0 else None
                assert obj.get("acceptingRule") == (rule["name"] if rule else "")
                assert obj.get("action") == (rule["action"] if rule else "")
                style = obj.find("mxCell").get("style")
                assert ("shape=doubleEllipse;" in style) == bool(rule)
                geometry = obj.find("mxCell/mxGeometry")
                node_boxes.append((obj.get("id"), *(float(geometry.get(k)) for k in ("x", "y", "width", "height"))))
            if "dfaSource" not in obj.attrib:
                continue
            grouped_count += 1
            source, target = int(obj.get("dfaSource")), int(obj.get("dfaTarget"))
            cell = obj.find("mxCell")
            assert cell.get("source") == f"s{source}" and cell.get("target") == f"s{target}"
            assert cell.get("source") in ids and cell.get("target") in ids
            assert "exitX=1;exitY=0.5;exitPerimeter=0;" in cell.get("style")
            first = cell.find("mxGeometry/Array/mxPoint")
            stem = float(first.get("x")), float(first.get("y"))
            assert stems.setdefault(source, stem) == stem, f"S{source} 的出边主干必须重合"
            outgoing[source] += 1
            assert edge_labels[obj.get("id")] == obj.get("label")
            for category in map(int, obj.get("dfaClasses").split(",")):
                for byte in DATA["classes"][category]:
                    key = source, byte
                    assert key not in actual, f"重复转移: {key}"
                    actual[key] = target
    expected = {(s, b): target for s, row in enumerate(DATA["full"]) for b, target in enumerate(row) if target >= 0}
    assert actual == expected, f"转移不一致: {len(actual)} / {len(expected)}"
    assert states == set(range(DATA["state_count"]))
    def overlap(a, b):
        return max(a[1], b[1]) < min(a[1] + a[3], b[1] + b[3]) and max(a[2], b[2]) < min(a[2] + a[4], b[2] + b[4])
    for index, box in enumerate(label_boxes):
        assert not any(overlap(box, node) for node in node_boxes), f"标签 {box[0]} 遮住状态"
        assert not any(overlap(box, other) for other in label_boxes[index + 1:]), f"标签 {box[0]} 与其他标签重叠"
    multi = sum(count > 1 for count in outgoing.values())
    print(f"PASS: 1 张图 / {len(states)} 唯一状态 / {grouped_count} 组箭头 / {len(actual)} 条有效字节转移；{multi} 个多出边状态共用主干，标签无重叠")


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
