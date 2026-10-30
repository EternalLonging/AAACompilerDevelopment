"""核对集合推导的边界，并用独立 Earley 识别器检查教学文法覆盖真实语法样例。"""
from __future__ import annotations

from collections import defaultdict
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

from generate_parser_tables import ROOT, TARGET, GRAMMAR_TARGET, END, build_payload, grammar_payload, derive, productions
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


def predicts(tokens, rules):
    """用生成的 LL(1) 表独立识别 Token，核对 SELECT 和预测格。"""
    grammar = derive(rules)
    assert all(len(choices) == 1 for choices in grammar['table'].values())
    by_id = {rule['id']: rule for rule in rules}
    stack, stream, cursor = [END, '程序'], [*tokens, END], 0
    while stack:
        symbol = stack.pop()
        if symbol not in grammar['names']:
            if cursor >= len(stream) or symbol != stream[cursor]:
                return False
            cursor += 1
            continue
        choices = grammar['table'].get((symbol, stream[cursor]))
        if not choices:
            return False
        stack.extend(reversed(by_id[choices[0]]['rhs']))
    return cursor == len(stream)


def workbook_checks():
    """损坏交付文件的副本，确认核对工具确实识别丢行、值变化和公式。"""
    source = TARGET if TARGET.exists() else GRAMMAR_TARGET
    if not source.exists():
        print('未提交静态语法表格；已跳过 Excel 反向核对。')
        return
    payload, _, _ = build_payload()
    if source == GRAMMAR_TARGET:
        payload = grammar_payload(payload)
    with zipfile.ZipFile(source) as book:
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
    """使用真实前端产生的 Token，检查有效源码属于文法及典型结构错误被排除。"""
    rules = productions()
    fixtures = ROOT / 'tests/integration'
    cases = json.loads((fixtures / 'manifest.json').read_text(encoding='utf-8'))['cases']
    sources = [(case['id'], (fixtures / case['source']).read_text(encoding='utf-8'), None, None) for case in cases]
    sources += [('empty', '', True, True)]
    # 文法自身须表达这些结构限制，不能只依赖解析器的事后拒绝。
    sources += [(name, source, False, False) for name, source in [
        ('bare_type', 'int;'),
        ('bare_local_type', 'int main(void){float;return 0;}'),
        ('prototype', 'int f(int x);'),
        ('local_prototype', 'int main(void){int f(int x);return 0;}'),
        ('array_dimensions', 'int a[2][3];'),
        ('parameter_dimensions', 'int f(int a[2][3]){return 0;}'),
        ('empty_record', 'struct S{};'),
        ('record_forward', 'struct S;'),
        ('anonymous_record', 'struct {int x;} s;'),
        ('record_function', 'struct S{int f();};'),
        ('inline_member_record', 'struct S{struct T{int x;} t;};'),
        ('unnamed_parameter', 'int f(int){return 0;}'),
        ('function_parameter', 'int f(int callback()){return 0;}'),
        ('record_parameter', 'struct S{int x;};int f(struct S s){return 0;}'),
        ('void_variable', 'void x;'),
        ('void_member', 'struct S{void x;};'),
    ]]
    sources += [(name, source, True, True) for name, source in [
        ('declaration_lists', 'int a[3],b=2;float f(float x){return x;}'),
        ('record_objects', 'struct S{int x;char c[4];} a,b[2];'),
        ('local_records', 'int main(void){struct S{int x;};struct S s;s.x=1;return s.x;}'),
        ('record_array_parameter', 'struct S{int x;};int f(struct S a[]){return a[0].x;}'),
        ('void_function', 'void f(void){return;}'),
        ('empty_parameters', 'int f(){return 1;}'),
        ('assignment_chain', 'int main(void){int a,b;a=b=3;return a+b;}'),
    ]]
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
            assert predicts(tokens[:-1], rules) == grammar_accepts, 'LL(1) 表与独立识别器不一致：' + name
            parser_accepts = bool(actual['ast'])
            if parser_accepts:
                assert grammar_accepts, '教学文法遗漏了实际接受的源码：' + name
                accepted += 1
            if expected_grammar is not None:
                assert grammar_accepts == expected_grammar, '文法判定不符：' + name
                assert parser_accepts == expected_parser, '语法判定不符：' + name
    assert accepted > 10
    print(f'真实语法样例覆盖通过：{accepted} 个语法接受样例，文法、LL(1) 表和解析器的声明边界一致')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('用法：python tools/test_parser_tables.py <workbench_driver>')
    set_checks()
    workbook_checks()
    corpus_checks(Path(sys.argv[1]).resolve())
