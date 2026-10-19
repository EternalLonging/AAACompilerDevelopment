"""核对集合推导的边界，并用独立 Earley 识别器检查文法骨架覆盖真实语法样例。"""
from __future__ import annotations

from collections import defaultdict
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

from generate_parser_tables import ROOT, TARGET, END, build_payload, derive, productions
from generate_lexer_tables import NS, check_workbook


def set_checks():
    """用手算结果检查可空传播、循环依赖和两种 SELECT 冲突。"""
    result = derive(productions('S -> A B\nA -> a | ε\nB -> b | ε'), 'S', '$')
    assert result['nullable'] == {'S', 'A', 'B'}
    assert result['first'] == {'S': {'a', 'b'}, 'A': {'a'}, 'B': {'b'}}
    assert result['follow'] == {'S': {'$'}, 'A': {'b', '$'}, 'B': {'$'}}
    assert result['select']['P003'] == {'b', '$'}
    assert not any(len(choices) > 1 for choices in result['table'].values())
    cycle = derive(productions('S -> A\nA -> B\nB -> A | b | ε'), 'S', '$')
    assert cycle['nullable'] == {'S', 'A', 'B'}
    assert all(values == {'b'} for values in cycle['first'].values())
    assert all(values == {'$'} for values in cycle['follow'].values())
    common = derive(productions('S -> a | a b'), 'S', '$')
    assert common['table']['S', 'a'] == ['P001', 'P002']
    nullable = derive(productions('S -> A a\nA -> a | ε'), 'S', '$')
    assert nullable['table']['A', 'a'] == ['P002', 'P003']
    print('手算集合核对通过：可空链、循环、FIRST/FIRST、FIRST/FOLLOW')


def accepts(tokens, rules):
    """独立执行 Earley 识别；不使用生成器的 FIRST、FOLLOW 或 SELECT。"""
    by_name = defaultdict(list)
    for rule in rules:
        by_name[rule['lhs']].append(tuple(rule['rhs']))
    chart = [set() for _ in range(len(tokens) + 1)]
    chart[0].add(('开始', ('程序',), 0, 0))
    for position in range(len(chart)):
        changed = True
        while changed:
            before = len(chart[position])
            for lhs, rhs, dot, origin in list(chart[position]):
                if dot == len(rhs):
                    for parent, items, index, start in list(chart[origin]):
                        if index < len(items) and items[index] == lhs:
                            chart[position].add((parent, items, index + 1, start))
                elif rhs[dot] in by_name:
                    for items in by_name[rhs[dot]]:
                        chart[position].add((rhs[dot], items, 0, position))
                elif position < len(tokens) and rhs[dot] == tokens[position]:
                    chart[position+1].add((lhs, rhs, dot + 1, origin))
            changed = len(chart[position]) != before
    return ('开始', ('程序',), 1, 0) in chart[-1]


def workbook_checks():
    """损坏交付文件的副本，确认核对工具确实识别丢行、值变化和公式。"""
    payload, _, _ = build_payload()
    with zipfile.ZipFile(TARGET) as book:
        parts = {name: book.read(name) for name in book.namelist()}
    with tempfile.TemporaryDirectory(prefix='minic-parser-tables-') as folder:
        for corruption in ['missing_row', 'changed_value', 'formula']:
            xml = ET.fromstring(parts['xl/worksheets/sheet1.xml'])
            if corruption == 'missing_row':
                data = xml.find('s:sheetData', NS)
                data.remove(data.find("s:row[@r='8']", NS))
            else:
                cell = xml.find(".//s:c[@r='C8']", NS)
                if corruption == 'changed_value':
                    for child in list(cell):
                        cell.remove(child)
                    cell.set('t', 'str')
                    ET.SubElement(cell, '{' + NS['s'] + '}v').text = '错误产生式'
                else:
                    ET.SubElement(cell, '{' + NS['s'] + '}f').text = '1+1'
            target = Path(folder) / (corruption + '.xlsx')
            with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as output:
                for name, content in parts.items():
                    output.writestr(name, ET.tostring(xml) if name == 'xl/worksheets/sheet1.xml' else content)
            try:
                check_workbook(target, payload)
            except ValueError:
                pass
            else:
                raise AssertionError('错误表格未被拒绝：' + corruption)
    print('表格反向核对通过：丢行、值变化、意外公式')


def corpus_checks(driver):
    """使用真实前端产生的 Token，检查有效源码属于骨架及典型结构错误被排除。"""
    rules = productions()
    fixtures = ROOT / 'tests/integration'
    cases = json.loads((fixtures / 'manifest.json').read_text(encoding='utf-8'))['cases']
    sources = [(case['id'], (fixtures / case['source']).read_text(encoding='utf-8'), None, None) for case in cases]
    sources += [
        ('empty', '', True, True),
        ('grouped_declarator', 'int f(int (x)){return x;} int (main)(void){return f(2);}', True, True),
        ('const_order', 'const int const x=1;', True, True),
        ('trailing_initializer', 'int a[]={1,2,};', True, True),
        ('anonymous_struct', 'struct {int x;} s;', True, False),
        ('forward_struct', 'struct S;', True, False),
        ('conflicting_types', 'int int x;', True, False),
        ('assignment_shape', 'int main(void){a+b=1;}', True, False),
        ('unbraced', 'int main(void){if(1)return 0;}', False, False),
        ('bound_expression', 'int a[2+3];', False, False),
        ('comma_expression', 'int main(void){return (1,2);}', False, False),
        ('for_declaration', 'int main(void){for(int i=0;i<2;i=i+1){}return 0;}', False, False),
        ('unterminated_block', 'int main(void){return 0;', False, False),
    ]
    accepted = 0
    with tempfile.TemporaryDirectory(prefix='minic-grammar-') as folder:
        path = Path(folder) / 'sample.c'
        for name, source, expected_grammar, expected_parser in sources:
            path.write_text(source, encoding='utf-8')
            result = subprocess.run([str(driver), 'inspect', str(path), str(path), '0'],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)
            if result.returncode:
                raise AssertionError(name + ': ' + result.stderr.decode('utf-8', errors='replace'))
            actual = json.loads(result.stdout)
            if any(item['phase'] == '词法' for item in actual['diagnostics']):
                continue
            tokens = [item['kind'] for item in actual['tokens']]
            assert tokens[-1] == END and END not in tokens[:-1]
            grammar_accepts = accepts(tokens[:-1], rules)
            parser_accepts = bool(actual['ast'])
            if parser_accepts:
                assert grammar_accepts, '文法骨架遗漏了实际接受的源码：' + name
                accepted += 1
            if expected_grammar is not None:
                assert grammar_accepts == expected_grammar, '骨架判定不符：' + name
                assert parser_accepts == expected_parser, '语法判定不符：' + name
    assert accepted > 10
    print(f'真实语法样例覆盖通过：{accepted} 个语法接受样例，另核对结构错误与上下文约束边界')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('用法：python tools/test_parser_tables.py <workbench_driver>')
    set_checks()
    workbook_checks()
    corpus_checks(Path(sys.argv[1]).resolve())
