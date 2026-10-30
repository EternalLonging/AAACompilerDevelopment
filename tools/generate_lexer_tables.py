"""导出词法表格数据，或核对 Excel 与当前规则、C++ DFA 是否一致。"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import posixpath
import xml.etree.ElementTree as ET
import zipfile

from generate_lexer_dfa import ROOT, SPEC, derive, expand, generate


TARGET = ROOT / '实验设计/词法分析/词法表格/词法状态转换表.xlsx'
RULES_PATH = ROOT / 'src/词法分析/词法规则.json'
HEADER_ROW = 7
ACTIONS = {'token': '输出单词', 'skip': '跳过', 'error': '报告错误'}
NS = {'s': 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}


def byte_label(value):
    """返回一个字节的可读写法，不把控制字节写入 XML。"""
    special = {0: r'\0', 9: r'\t', 10: r'\n', 11: r'\v', 12: r'\f', 13: r'\r',
               32: '空格', 92: r'\\'}
    return special.get(value, chr(value) if 33 <= value <= 126 else f'\\x{value:02X}')


def intervals(members):
    """将有序字节合并为十六进制闭区间，返回完整类别定义。"""
    groups = []
    begin = end = members[0]
    for value in members[1:]:
        if value == end + 1:
            end = value
        else:
            groups.append((begin, end))
            begin = end = value
    groups.append((begin, end))
    return '，'.join(f'{a:02X}' if a == b else f'{a:02X}–{b:02X}' for a, b in groups)


def header_content(text):
    """忽略生成说明注释，返回真正参与编译的 DFA 数据。"""
    return '\n'.join(line for line in text.splitlines() if not line.startswith('// 由 '))


def build_payload():
    """从唯一规则源推导全部表格，返回可交给 Excel 生成器的数据。"""
    dfa = derive(SPEC['rules'])
    current_header = (ROOT / 'src/词法分析/lexer_dfa.hpp').read_text(encoding='utf-8')
    if header_content(current_header) != header_content(generate()):
        raise ValueError('C++ DFA 数据与规则不一致，请先运行 tools/generate_lexer_dfa.py')
    rules = SPEC['rules']
    classes = dfa['classes']
    sha = hashlib.sha256(RULES_PATH.read_bytes()).hexdigest()
    n = dfa['state_count']
    # 编号与 transitions、accepting 的数组下标相同；-1 保留为数字。
    matrix_rows = [[state, dfa['accept'][state], *row]
                   for state, row in enumerate(dfa['matrix'])]
    states = []
    for state, rule_id in enumerate(dfa['accept']):
        rule = rules[rule_id] if rule_id >= 0 else None
        witness = dfa['witnesses'][state] if state else 'ε（尚未读取字节）'
        states.append([state, '起始态' if state == 0 else '接受态' if rule else '中间态',
                       rule_id, rule['name'] if rule else '—',
                       ACTIONS[rule['action']] if rule else '继续匹配',
                       rule.get('token', '—') if rule else '—',
                       rule.get('code', '—') if rule else '—', witness,
                       rule['description'] if rule else '没有接受动作，继续读取后续字节'])
    input_rows = []
    byte_classes = [None] * 256
    for index, members in enumerate(classes):
        for byte in members:
            if byte_classes[byte] is not None:
                raise ValueError('输入类别重叠')
            byte_classes[byte] = index
        display = '、'.join(byte_label(byte) for byte in members) if len(members) <= 25 else '完整字节范围见前列'
        input_rows.append([f'C{index:02}', len(members), intervals(members), display])
    assert all(item is not None for item in byte_classes)
    rule_rows = [[index, rule['name'], ACTIONS[rule['action']], rule.get('token', '—'),
                  rule.get('code', '—'), rule['regex'], rule['description']]
                 for index, rule in enumerate(rules)]
    definition_rows = [[name, value, expand(value)] for name, value in SPEC['definitions'].items()]
    keyword_rows = [[spelling, token] for spelling, token in SPEC['keywords'].items()]
    symbol_rows = [[rule['literal'], rule['token'], index, rule['description']]
                   for index, rule in enumerate(rules) if 'literal' in rule]

    def sheet(name, title, context, headers, rows, widths, notes=()):
        return {'name': name, 'title': title, 'context': context, 'headers': headers,
                'rows': rows, 'widths': widths, 'notes': list(notes)}

    return {'schema': 1, 'rules_sha256': sha, 'header_row': HEADER_ROW, 'sheets': [
        sheet('状态转换矩阵', '词法状态转换表',
              f'{n} 个 DFA 状态，{len(classes)} 类输入，覆盖全部 256 种字节。',
              ['状态编号', '接受规则', *[f'C{i:02}' for i in range(len(classes))]],
              matrix_rows, [12, 12, *[8] * len(classes)], [
                  '状态 0 为起始态。转换值 -1 表示无法继续，扫描器回退到最后接受位置。',
                  '接受规则 -1 表示非接受态。C 编号见“输入分类”，规则编号见“正则规则”。',
                  '状态编号对应 C++ 数组下标，与展示图的 Q 编号不是一套编号。']),
        sheet('状态说明', '状态与接受动作',
              '最短到达串使用字节转义。到达状态可继续读取，最终动作取决于最长匹配。',
              ['状态编号', '状态类型', '规则编号', '规则名称', '接受动作', 'Token 种别',
               '错误代码', '最短到达串', '规则说明'], states,
              [12, 12, 12, 36, 14, 26, 30, 28, 78]),
        sheet('输入分类', '输入字节分类',
              '按所有状态的转移行为分组。十六进制范围均为闭区间，各类互斥并覆盖 00–FF。',
              ['类别编号', '字节数量', '完整字节范围（十六进制）', '可读字符'], input_rows,
              [12, 12, 70, 75], ['非 ASCII 字符按 UTF-8 字节扫描，不按 Unicode 字符直接分类。']),
        sheet('字节映射', '256 种字节到输入类别的映射',
              '以十进制或十六进制字节查类别，再使用状态转换矩阵。',
              ['字节（十进制）', '字节（十六进制）', '可读写法', '类别编号'],
              [[byte, f'0x{byte:02X}', byte_label(byte), f'C{byte_classes[byte]:02}'] for byte in range(256)],
              [20, 22, 20, 14]),
        sheet('正则规则', '词法规则与优先级',
              '来源：src/词法分析/词法规则.json',
              ['规则编号', '规则名称', '动作', 'Token 种别', '错误代码', '正则表达式', '中文说明'],
              rule_rows, [12, 36, 14, 26, 30, 85, 78], [
                  f'规则文件 SHA-256：{sha}',
                  '最长匹配优先，同长时规则编号小的优先。EOF 由驱动追加，不占 DFA 状态。']),
        sheet('正则定义', '正则定义与展开结果',
              '花括号引用是正则模式复用，不是源语言宏预处理。',
              ['定义名称', '原始正则', '展开结果'], definition_rows, [25, 62, 140]),
        sheet('关键字', '关键字识别表',
              '完整识别 ID 后查表。词法识别不代表语法支持。',
              ['关键字', 'Token 种别'], keyword_rows, [20, 28]),
        sheet('固定符号', '运算符与分隔符',
              '保留实际词法种别。=、==、>>、>>= 等按最长匹配识别。',
              ['源代码拼写', 'Token 种别', '规则编号', '中文说明'], symbol_rows, [18, 30, 14, 70]),
    ]}


def cell_address(row, column):
    """将从 1 起的行列转为 Excel 地址。"""
    letters = ''
    while column:
        column, rest = divmod(column - 1, 26)
        letters = chr(65 + rest) + letters
    return f'{letters}{row}'


def read_workbook(path):
    """用标准库读取已导出的单元格，核验实际交付文件。"""
    result = {}
    with zipfile.ZipFile(path) as archive:
        strings = []
        if 'xl/sharedStrings.xml' in archive.namelist():
            for item in ET.fromstring(archive.read('xl/sharedStrings.xml')):
                strings.append(''.join(node.text or '' for node in item.iterfind('.//s:t', NS)))
        rels = ET.fromstring(archive.read('xl/_rels/workbook.xml.rels'))
        targets = {item.get('Id'): posixpath.normpath(posixpath.join('xl', item.get('Target'))).lstrip('/')
                   if not item.get('Target').startswith('/') else item.get('Target').lstrip('/') for item in rels}
        book = ET.fromstring(archive.read('xl/workbook.xml'))
        for sheet in book.find('s:sheets', NS):
            rid = sheet.get('{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id')
            xml = ET.fromstring(archive.read(targets[rid]))
            cells = {}
            for cell in xml.findall('.//s:sheetData/s:row/s:c', NS):
                kind = cell.get('t')
                value = cell.findtext('s:v', default='', namespaces=NS)
                if cell.find('s:f', NS) is not None:
                    raise ValueError('静态表格数据中不应出现公式：' + cell.get('r'))
                if kind == 'e':
                    raise ValueError('Excel 错误单元格：' + cell.get('r'))
                if kind == 's':
                    value = strings[int(value)]
                elif kind == 'inlineStr':
                    value = ''.join(node.text or '' for node in cell.iterfind('.//s:t', NS))
                elif value and kind not in ['str', 'b']:
                    value = float(value)
                    value = int(value) if value.is_integer() else value
                if value != '':
                    cells[cell.get('r')] = value
            result[sheet.get('name')] = cells
    return result


def check_workbook(path, payload):
    """逐单元格核对表格数据和来源，返回核对数量。"""
    actual = read_workbook(path)
    expected_names = [sheet['name'] for sheet in payload['sheets']]
    if list(actual) != expected_names:
        raise ValueError('工作表名称或顺序不一致，请重新生成 Excel')
    checked = 0
    for sheet in payload['sheets']:
        expected = {'A2': sheet['title'], 'A3': sheet['context']}
        for row, note in enumerate(sheet['notes'], 4):
            expected[f'A{row}'] = note
        for row, values in enumerate([sheet['headers'], *sheet['rows']], HEADER_ROW):
            for column, value in enumerate(values, 1):
                expected[cell_address(row, column)] = value
        # 不允许额外数据、丢行、缺列、空白转换或文字形式的数字。
        if actual[sheet['name']] != expected:
            for address in expected.keys() | actual[sheet['name']].keys():
                want, have = expected.get(address), actual[sheet['name']].get(address)
                if want != have or (isinstance(want, int) and not isinstance(have, int)):
                    raise ValueError(f'{sheet["name"]}!{address} 不一致：预期 {want!r}，实际 {have!r}')
        checked += len(expected)
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--data', type=Path, help='将 Excel 生成所需数据写到指定 JSON 文件')
    mode.add_argument('--check', action='store_true', help='核对已提交的 Excel 文件，无额外依赖')
    parser.add_argument('--workbook', type=Path, default=TARGET, help='待核对的 Excel 文件')
    args = parser.parse_args()
    try:
        payload = build_payload()
        if args.data:
            args.data.parent.mkdir(parents=True, exist_ok=True)
            args.data.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
            print('已导出词法表格数据：', args.data)
        else:
            if args.workbook.exists():
                count = check_workbook(args.workbook, payload)
                print(f'词法表格与规则及 C++ DFA 一致：8 张表，{count} 个单元格')
            else:
                print('未提交静态词法表格；已跳过 Excel 核对，规则和 DFA 仍由源码检查。')
    except (ValueError, OSError, zipfile.BadZipFile, ET.ParseError) as error:
        parser.exit(1, f'词法表格检查失败：{error}\n')


if __name__ == '__main__':
    main()
