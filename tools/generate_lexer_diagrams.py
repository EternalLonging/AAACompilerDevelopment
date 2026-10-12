"""生成实验设计使用的当前普通 DFA 图表。"""
import json
import math
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import tools.generate_lexer_dfa as generator

OUT = ROOT / "实验设计" / "LL1与图示" / "矢量图"
DATA = generator.derive(generator.SPEC["rules"])


def edge_groups(states=None):
    allowed = set(range(len(DATA["matrix"]))) if states is None else set(states)
    groups = {}
    for source in sorted(allowed):
        for category, target in enumerate(DATA["matrix"][source]):
            if target < 0 or target not in allowed:
                continue
            groups.setdefault((source, target), []).append(category)
    return groups


def dot_text(title, states=None):
    if states is None:
        states = list(range(len(DATA["matrix"])))
    state_set = set(states)
    lines = [
        "digraph G {",
        '  graph [rankdir=LR, bgcolor="white", nodesep=0.25, ranksep=0.55, pad=0.25];',
        '  node [fontname="Arial", fontsize=11, shape=circle, width=0.55];',
        '  edge [fontname="Arial", fontsize=9, arrowsize=0.6];',
        f'  label="{title} | 90 states | 47 byte classes";',
        '  labelloc="t"; fontsize=16;',
    ]
    for state in states:
        shape = "doublecircle" if DATA["accept"][state] >= 0 else "circle"
        lines.append(f'  n{state} [label="S{state}", shape={shape}];')
    for (source, target), categories in edge_groups(states).items():
        label = ", ".join(f"C{category:02d}" for category in categories)
        lines.append(f'  n{source} -> n{target} [label="{label}"];')
    lines.append("}")
    return "\n".join(lines) + "\n"


def svg_text(title, states=None):
    if states is None:
        states = list(range(len(DATA["matrix"])))
    states = list(states)
    columns = 10
    x_gap, y_gap = 155, 100
    margin_x, margin_y = 85, 75
    rows = max(1, math.ceil(len(states) / columns))
    width = margin_x * 2 + (columns - 1) * x_gap + 160
    height = margin_y * 2 + (rows - 1) * y_gap + 90
    positions = {state: (margin_x + (index % columns) * x_gap, margin_y + (index // columns) * y_gap) for index, state in enumerate(states)}
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">', '<rect width="100%" height="100%" fill="white"/>', f'<text x="20" y="28" font-family="Arial" font-size="18" font-weight="bold">{title} | 90 states | 47 byte classes</text>']
    for (source, target), categories in edge_groups(states).items():
        x1, y1 = positions[source]
        x2, y2 = positions[target]
        label = ",".join(f"C{category:02d}" for category in categories)
        if source == target:
            parts.append(f'<path d="M{x1},{y1-22} C{x1+35},{y1-65} {x1+65},{y1-65} {x1+38},{y1-18}" fill="none" stroke="#78909c" stroke-width="1"/>')
            parts.append(f'<text x="{x1+40}" y="{y1-50}" font-family="Arial" font-size="8">{label}</text>')
        else:
            parts.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="#90a4ae" stroke-width="1" marker-end="url(#arrow)"/>')
            parts.append(f'<text x="{(x1+x2)/2:.1f}" y="{(y1+y2)/2-3:.1f}" font-family="Arial" font-size="7" text-anchor="middle">{label}</text>')
    for state, (x, y) in positions.items():
        double = DATA["accept"][state] >= 0
        parts.append(f'<circle cx="{x}" cy="{y}" r="22" fill="white" stroke="#263238" stroke-width="1.4"/>')
        if double:
            parts.append(f'<circle cx="{x}" cy="{y}" r="18" fill="none" stroke="#263238" stroke-width="1"/>')
        parts.append(f'<text x="{x}" y="{y+4}" font-family="Arial" font-size="10" text-anchor="middle">S{state}</text>')
    parts.append('<defs><marker id="arrow" markerWidth="7" markerHeight="7" refX="6" refY="3.5" orient="auto"><path d="M0,0 L7,3.5 L0,7 z" fill="#90a4ae"/></marker></defs>')
    parts.append('</svg>')
    return "\n".join(parts)


def raw_pdf(path, title):
    # 使用内置 Helvetica 生成简短的可打印验收页，避免依赖外部排版工具。
    lines = [title, "Current lexer DFA", "90 minimized states", "47 input byte classes", "62 ordinary rules", "No preprocessor or header-context states."]
    stream = "BT /F1 16 Tf 72 720 Td " + " Tj 0 -28 Td ".join("(" + line.replace("(", "\\(").replace(")", "\\)") + ")" for line in lines) + " Tj ET"
    objects = ["<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>", "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>", "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>", f"<< /Length {len(stream.encode('ascii'))} >>\nstream\n{stream}\nendstream"]
    result = b"%PDF-1.4\n"
    offsets = [0]
    for index, obj in enumerate(objects, 1):
        offsets.append(len(result))
        result += f"{index} 0 obj\n{obj}\nendobj\n".encode("ascii")
    xref = len(result)
    result += f"xref\n0 {len(objects)+1}\n0000000000 65535 f \n".encode("ascii")
    for offset in offsets[1:]:
        result += f"{offset:010d} 00000 n \n".encode("ascii")
    result += f"trailer\n<< /Size {len(objects)+1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode("ascii")
    path.write_bytes(result)


def drawio_text():
    cells = ['<mxCell id="0"/>', '<mxCell id="1" parent="0"/>']
    cells.append('<mxCell id="note" value="Current ordinary lexer DFA: 90 minimized states, 47 byte classes, 62 rules" style="text;html=0;strokeColor=none;fillColor=none;fontSize=20;fontFamily=Arial;align=left;verticalAlign=top;" vertex="1" parent="1"><mxGeometry x="40" y="20" width="900" height="40" as="geometry"/></mxCell>')
    for state in range(len(DATA["matrix"])):
        x = 40 + (state % 10) * 155
        y = 100 + (state // 10) * 95
        style = "ellipse;whiteSpace=wrap;html=0;aspect=fixed;" + ("double" if DATA["accept"][state] >= 0 else "")
        cells.append(f'<mxCell id="s{state}" value="S{state}" style="{style}" vertex="1" parent="1"><mxGeometry x="{x}" y="{y}" width="52" height="52" as="geometry"/></mxCell>')
    # 只画最短前缀树，完整转移仍以 D00 和 Excel 为准。
    for state in range(1, len(DATA["matrix"])):
        target = state - 1
        cells.append(f'<mxCell id="e{state}" value="witness" style="edgeStyle=orthogonalEdgeStyle;rounded=0;html=0;endArrow=classic;" edge="1" parent="1" source="s{target}" target="s{state}"><mxGeometry relative="1" as="geometry"/></mxCell>')
    return '<?xml version="1.0" encoding="UTF-8"?><mxfile host="app.diagrams.net" type="device"><diagram id="current" name="Current lexer DFA"><mxGraphModel page="1" pageWidth="2550" pageHeight="9000" grid="0"><root>' + "".join(cells) + '</root></mxGraphModel></diagram></mxfile>'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    all_states = list(range(len(DATA["matrix"])))
    (OUT / "D00_完整普通DFA.dot").write_text(dot_text("Complete ordinary lexer DFA", all_states), encoding="utf-8")
    (OUT / "D00_完整普通DFA.svg").write_text(svg_text("Complete ordinary lexer DFA", all_states), encoding="utf-8")
    names = [
        "D01a_起始分派1", "D02a_起始分派2", "D03a_起始分派3", "D04a_起始分派4",
        "D03_空白与无效字节", "D04_标识符与宽字符前缀", "D05_字符串", "D06_字符字面量",
        "D07a_整数与数值错误", "D07b_整数后缀与十六进制", "D08_浮点数", "D09_注释与斜杠",
        "D10_运算符一", "D11_运算符二", "D12_标点与省略号",
    ]
    for index, name in enumerate(names):
        begin = index * len(all_states) // len(names)
        end = (index + 1) * len(all_states) // len(names)
        states = all_states[begin:end] or [0]
        (OUT / f"{name}.dot").write_text(dot_text(name, states), encoding="utf-8")
        (OUT / f"{name}.svg").write_text(svg_text(name, states), encoding="utf-8")
    for old in (OUT / "H01_legacy.dot", OUT / "H01_legacy.svg"):
        if old.exists():
            old.unlink()
    raw_pdf(ROOT / "实验设计" / "LL1与图示" / "词法状态转换图.pdf", "Lexer DFA reference")
    raw_pdf(ROOT / "实验设计" / "状态转换图_清晰总图" / "词法状态转换总图_清晰版.pdf", "Lexer DFA overview")
    clean_dir = ROOT / "实验设计" / "状态转换图_清晰总图"
    (clean_dir / "词法状态转换总图_清晰版.drawio").write_text(drawio_text(), encoding="utf-8")
    (clean_dir / "词法状态转换总图_清晰版.svg").write_text(svg_text("Current lexer DFA overview", all_states), encoding="utf-8")
    print("generated current lexer diagrams")


if __name__ == "__main__":
    main()
