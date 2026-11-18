"""校验用户文法，导出 FIRST/FOLLOW/SELECT 与预测表；不修改编译器。"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

FOLDER = Path(__file__).resolve().parent
ROOT = FOLDER.parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from generate_parser_tables import END, derive, productions
from test_parser_tables import accepts

GRAMMAR = FOLDER / '用户文法整理版.bnf'
OUTPUT = FOLDER / '用户文法验证数据.json'

# 此处只验证文法形状。名字、类型、数组长度值等仍由后续阶段检查。
VALID = [
    ('空程序', ''),
    ('简单变量', 'int a;'),
    ('变量初始化', 'int a=3,b[5],c;'),
    ('数组长度可省略的形状', 'char a[]="hello";'),
    ('三种基本类型', "int a;float b=1.5;char c='a';"),
    ('void与空参数', 'void f(void){return;}int main(){return 0;}'),
    ('多个形参', 'int f(int a,float b,char c,int d[]){return a;}'),
    ('嵌套空块', 'void f(){{}{int a;}return;}'),
    ('空语句和返回', 'void f(){;;return;}'),
    ('完整表达式', 'int f(){return a||b&&c==d<e+f*g;}'),
    ('一元运算', 'int f(){return !-+a;}'),
    ('括号优先级', 'int f(){return (a+b)*(c-d);}'),
    ('左结合运算输入', 'int f(){return 12/3/2+8-3-1;}'),
    ('连等赋值', 'int f(){int a,b;a=b=3;return a;}'),
    ('初始化复用赋值', 'int f(){int b,a=b=3;return a;}'),
    ('空实参调用', 'int f(){return g();}'),
    ('多个实参调用', 'int f(){return g(a,b+3,h());}'),
    ('重复后缀', 'int f(){return a[i].x[j];}'),
    ('if语句', 'int f(){if(a<b){a=b;}return a;}'),
    ('ifelse语句', 'int f(){if(a){x=1;}else{x=2;}return x;}'),
    ('elseif链', 'int f(){if(a){}else if(b){}else if(c){}else{}return 0;}'),
    ('块内嵌套if', 'int f(){if(a){if(b){}else{}}else{}return 0;}'),
    ('while与跳转', 'void f(){while(a){if(b){break;}continue;}return;}'),
    ('for三个表达式', 'void f(){int i;for(i=0;i<3;i=i+1){}return;}'),
    ('for省略表达式', 'void f(){for(;;){break;}return;}'),
    ('外部结构体定义', 'struct S{int x;float y;};'),
    ('多个成员与成员数组', 'struct S{int x,y[5],z;char text[8];};'),
    ('结构体成员类型', 'struct S{int x;};struct T{struct S s;};'),
    ('结构体同时声明变量', 'struct S{int x;} a,b[3];'),
    ('已有结构体类型的对象', 'struct S{int x;};struct S a,b[3];'),
    ('局部定义结构体', 'int f(){struct S{int x;} a;struct S b;a.x=3;return a.x;}'),
    ('结构体数组形参', 'int f(struct S a[]){return a[0].x;}'),
    ('按值结构体形参的形状', 'int f(struct S a){return a.x;}'),
    ('相邻字符串拼接', 'int f(){printf("hello " "world");return 0;}'),
    ('三种scanf地址', 'int f(){scanf("%d%d%d",&a,&a[i],&s.x);return 0;}'),
    ('括号内地址表达式', 'int f(){scanf("%d",&(a));return 0;}'),
    ('不可写目标留给语义', 'int f(){a+b=c;return 0;}'),
    ('循环外跳转留给语义', 'int f(){break;continue;return 0;}'),
    ('未声明名字留给语义', 'int f(){return missing;}'),
    ('零数组长度值留给后续', 'int a[0];'),
    ('非左值地址留给语义', 'int f(){scanf("%d",&(a+b));return 0;}'),
]

INVALID = [
    ('缺少分号', 'int a'),
    ('缺少初始化表达式', 'int a=;'),
    ('缺少赋值右部', 'int f(){a=;return 0;}'),
    ('声明尾逗号', 'int a,;'),
    ('声明缺少名字', 'int;'),
    ('void对象', 'void a;'),
    ('函数原型', 'int f(int a);'),
    ('默认参数', 'int f(int a=3){return a;}'),
    ('形参尾逗号', 'int f(int a,){return a;}'),
    ('形参中间void', 'int f(int a,void){return a;}'),
    ('无名形参', 'int f(int){return 0;}'),
    ('实参尾逗号', 'int f(){return g(a,);}'),
    ('实参头逗号', 'int f(){return g(,a);}'),
    ('实参缺少逗号', 'int f(){return g(a b);}'),
    ('空if条件', 'void f(){if(){}return;}'),
    ('空while条件', 'void f(){while(){}return;}'),
    ('if没有代码块', 'int f(){if(a)return 0;return 1;}'),
    ('else没有代码块', 'void f(){if(a){}else x=1;return;}'),
    ('while没有代码块', 'void f(){while(a)a=a-1;return;}'),
    ('for中声明变量', 'void f(){for(int i=0;i<3;i=i+1){}return;}'),
    ('语句后声明', 'int f(){int a;a=1;int b;return a;}'),
    ('嵌套函数', 'int f(){int g(){return 1;}return 0;}'),
    ('空结构体', 'struct S{};'),
    ('成员初始化', 'struct S{int x=3;};'),
    ('成员尾逗号', 'struct S{int x,;};'),
    ('成员缺分号', 'struct S{int x};'),
    ('成员函数', 'struct S{int f(){return 0;}};'),
    ('匿名结构体', 'struct {int x;} a;'),
    ('结构体前向声明', 'struct S;'),
    ('结构体中定义新结构体', 'struct S{struct T{int x;} t;};'),
    ('返回结构体的函数', 'struct S f(){struct S s;return s;}'),
    ('多维数组', 'int a[2][3];'),
    ('数组长度为标识符', 'int a[n];'),
    ('数组长度为算式', 'int a[2+3];'),
    ('空下标', 'int f(){return a[];}'),
    ('空成员名', 'int f(){return a.;}'),
    ('指针声明', 'int *p;'),
    ('解引用', 'int f(){return *p;}'),
    ('三元运算', 'int f(){return a?b:c;}'),
    ('取地址后缺少操作数', 'int f(){scanf("%d",&);return 0;}'),
    ('取地址后直接负号', 'int f(){return &-a;}'),
]


def load(path):
    text = '\n'.join(line for line in path.read_text(encoding='utf-8').splitlines()
                     if line.strip() and not line.lstrip().startswith('#'))
    return productions(text)


def structural_checks(rows, tokens):
    sets = derive(rows)
    names = set(sets['names'])
    unknown = set(sets['terminals']) - set(tokens)
    if unknown:
        raise ValueError(f'未定义的文法符号：{sorted(unknown)}')
    reachable = {'程序'}
    while True:
        updated = reachable | {s for row in rows if row['lhs'] in reachable
                               for s in row['rhs'] if s in names}
        if updated == reachable:
            break
        reachable = updated
    if names != reachable:
        raise ValueError(f'不可达规则：{sorted(names - reachable)}')
    productive = set()
    while True:
        updated = productive | {row['lhs'] for row in rows
                                if all(s not in names or s in productive for s in row['rhs'])}
        if updated == productive:
            break
        productive = updated
    if names != productive:
        raise ValueError(f'不能生成终结符串的规则：{sorted(names - productive)}')
    left_edges = {name: set() for name in names}
    for row in rows:
        for symbol in row['rhs']:
            if symbol in names:
                left_edges[row['lhs']].add(symbol)
            if symbol not in sets['nullable']:
                break
    for name in names:
        visited, pending = set(), list(left_edges[name])
        while pending:
            target = pending.pop()
            if target == name:
                raise ValueError(f'直接或间接左递归：{name}')
            if target not in visited:
                visited.add(target)
                pending.extend(left_edges[target])
    conflicts = {f'{name}, {token}': choices for (name, token), choices in sets['table'].items()
                 if len(choices) > 1}
    if conflicts:
        raise ValueError(f'预测表冲突：{conflicts}')
    return sets


def predict(tokens, rows, sets):
    stream = tokens + [END]
    by_id = {row['id']: row for row in rows}
    names = set(sets['names'])
    stack, position = [END, '程序'], 0
    while stack:
        symbol = stack.pop()
        if position == len(stream):
            return False
        if symbol not in names:
            if symbol != stream[position]:
                return False
            position += 1
        else:
            choice = sets['table'].get((symbol, stream[position]))
            if not choice:
                return False
            stack.extend(reversed(by_id[choice[0]]['rhs']))
    return position == len(stream)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--lexer-probe', required=True, type=Path,
                        help='项目已有的 lexer_probe 可执行文件')
    parser.add_argument('--check', action='store_true', help='只核对已有验证数据')
    args = parser.parse_args()
    interface = (ROOT / 'include/minic/interface.hpp').read_text(encoding='utf-8')
    enum = re.search(r'enum class TokenType\s*\{(.*?)\};', interface, re.S)[1]
    token_names = re.findall(r'\b[A-Z][A-Z_]+\b', enum)
    rows = load(GRAMMAR)
    sets = structural_checks(rows, token_names)
    current = productions()

    cases = [(name, source, True) for name, source in VALID]
    cases += [(name, source, False) for name, source in INVALID]
    manifest_path = ROOT / 'tests/integration/manifest.json'
    for case in json.loads(manifest_path.read_text(encoding='utf-8'))['cases']:
        source = (manifest_path.parent / case['source']).read_text(encoding='utf-8')
        cases.append(('联调/' + case['id'], source, None))
    wire = ''.join(source.encode('utf-8').hex() + '\n' for _, source, _ in cases)
    probe = subprocess.run([str(args.lexer_probe.resolve())], input=wire, text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True, timeout=30)
    lines = probe.stdout.splitlines()
    if len(lines) != len(cases):
        raise ValueError('词法探针响应数量不匹配')
    results = []
    for (name, source, expected), line in zip(cases, lines):
        fields = [field for field in line.split('|') if field]
        lexical_error = any(field.startswith('E') for field in fields)
        if lexical_error:
            if expected is True:
                raise ValueError('合法样例词法分析失败：' + name)
            results.append({'name': name, 'expected': expected, 'lexical_error': True})
            continue
        tokens = [token_names[int(field[1:].split(',')[0])] for field in fields if field.startswith('T')]
        if not tokens or tokens[-1] != END or END in tokens[:-1]:
            raise ValueError('词法探针未返回正确的末尾 EOF')
        tokens.pop()
        predicted = predict(tokens, rows, sets)
        independent = accepts(tokens, rows)
        if predicted != independent:
            raise ValueError('预测分析与独立 Earley 判定不一致：' + name)
        if expected is not None and predicted != expected:
            raise ValueError(f'样例结果错误：{name}，实际接受={predicted}')
        results.append({'name': name, 'expected': expected, 'lexical_error': False,
                        'accepted': predicted, 'earley': independent,
                        'current_grammar_accepted': accepts(tokens, current)})

    # 两个关键设计选择必须在验证中实际覆盖。
    assert any(item.get('accepted') and item['name'] == '初始化复用赋值' for item in results)
    assert any(item.get('accepted') and item['name'] == '局部定义结构体' for item in results)
    output = {
        'grammar': GRAMMAR.name,
        'production_count': len(rows), 'nonterminal_count': len(sets['names']),
        'prediction_cells': len(sets['table']), 'conflict_count': 0,
        'productions': rows,
        'nullable': sorted(sets['nullable']),
        'first': {name: sorted(sets['first'][name]) for name in sets['names']},
        'follow': {name: sorted(sets['follow'][name]) for name in sets['names']},
        'select': {name: sorted(value) for name, value in sets['select'].items()},
        'table': [{'nonterminal': name, 'lookahead': token, 'production': choice[0]}
                  for (name, token), choice in sorted(sets['table'].items())],
        'cases': results,
    }
    content = json.dumps(output, ensure_ascii=False, indent=2) + '\n'
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text(encoding='utf-8') != content:
            raise SystemExit('用户文法验证数据需要更新')
    else:
        OUTPUT.write_text(content, encoding='utf-8', newline='\n')
    grammar_cases = sum(not item['lexical_error'] for item in results)
    lexical_cases = len(results) - grammar_cases
    print(f'{len(rows)} productions; {len(sets["names"])} nonterminals; '
          f'{len(sets["table"])} prediction cells; 0 conflicts; '
          f'{grammar_cases} LL/Earley cases; {lexical_cases} lexer rejections.')
    for case in results:
        if not case['lexical_error'] and case['accepted'] != case['current_grammar_accepted']:
            print('LANGUAGE DIFFERENCE:', case['name'], 'draft=', case['accepted'],
                  'current=', case['current_grammar_accepted'])


if __name__ == '__main__':
    main()
