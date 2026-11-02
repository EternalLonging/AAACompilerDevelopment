"""读取当前预测表，生成覆盖每条产生式的语义合并动画；不修改编译器。"""
from pathlib import Path
from copy import deepcopy
import hashlib
import json
import re

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
header = (ROOT / 'src/语法分析/predictive_table.hpp').read_text('utf-8')
parser_source = (ROOT / 'src/语法分析/parser.cpp').read_text('utf-8')
interface = (ROOT / 'include/minic/interface.hpp').read_text('utf-8')
rules = json.loads((ROOT / 'src/词法分析/词法规则.json').read_text('utf-8'))
enum = re.search(r'enum class TokenType\s*\{(.*?)\};', interface, re.S)[1]
terminals = re.findall(r'\b[A-Z][A-Z_]+\b', enum)
names = terminals + re.findall(r'^    (\S+) = \d+,', header, re.M)
productions = []
for length, rhs, pid, lhs in re.findall(r'\{(\d+), \{([\d, ]+)\}\}, // (P\d+): (\S+) ->', header):
    productions.append(dict(lhs=lhs, rhs=[names[int(x)] for x in rhs.split(',')][:int(length)], id=pid))
table = {}
table_text = header.split('inline constexpr int table')[1].split('};')[0]
for row, lhs in re.findall(r'\{([\d, \-]+)\}, // (\S+)', table_text):
    table[lhs] = dict(zip(terminals, map(int, row.split(','))))
spellings = {v: k for k, v in rules['keywords'].items()}
spellings.update({r['token']: r['literal'] for r in rules['rules'] if 'literal' in r})
spellings['END_OF_FILE'] = 'EOF'

# 固定教学片段只为覆盖语法动作；不要求它们通过语义分析。
sources = [
    ('声明与类型', '''struct S { int x,y; char text[4]; struct T nested; };
struct S obj,items[2];
int g=1,h=2,k;
char label[]="a" "b";
float z=1.5;
struct T {int a;} instance;
'''),
    ('函数与控制流', '''int f(int a,float b,char c,struct S s[]) {
 int x=1,y[3],z;
 struct S local;
 struct U { int p; } u;
 ;
 {}
 if(a) {break;} else {continue;}
 if(a) {}
 while(a) {break;}
 for(x=0; x<2; x=x+1) {continue;}
 for(;;) {}
 f(a,b,c,s);
 noop();
 return (x);
}
void noop(void) {return;}
int empty() {return 0;}
'''),
    ('运算与后缀', '''int calc() {
 x = a = 2;
 x = +a - -a * 2 / 3 % 2 + 4 - 5;
 x = !a || a && b || c;
 x = a == b != c;
 x = a < b <= c > d >= e;
 &x;
 s[0].x;
 f(a,b)[0].x;
 'a';
 "a" "b" "c";
 return 1.5;
}
'''),
    ('空程序', ''),
]

token_pattern = re.compile(r'''\s+|//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|(?:\d+\.\d*|\.\d+)(?:[eE][+-]?\d+)?[fF]?|\d+[eE][+-]?\d+|0[xX][0-9a-fA-F]+|\d+|[A-Za-z_]\w*|<=|>=|==|!=|&&|\|\||[+\-*/%=<>!&(){}\[\];,.]''')
literal_tokens = {r['literal']: r['token'] for r in rules['rules'] if 'literal' in r}


def lex(source):
    tokens, end = [], 0
    for m in token_pattern.finditer(source):
        assert m.start() == end, (end, source[end:m.start()])
        end = m.end()
        text = m[0]
        if text.isspace() or text.startswith(('//', '/*')):
            continue
        if text in rules['keywords']:
            kind = rules['keywords'][text]
        elif text in literal_tokens:
            kind = literal_tokens[text]
        elif text.startswith('"'):
            kind = 'STRING_LITERAL'
        elif text.startswith("'"):
            kind = 'CHAR_LITERAL'
        elif text[0].isdigit() or text[0] == '.':
            kind = 'FLOAT_LITERAL' if '.' in text or ('e' in text.lower() and not text.lower().startswith('0x')) else 'INT_LITERAL'
        else:
            kind = 'ID'
        tokens.append(dict(kind=kind, text=text, start=m.start(), end=m.end()))
    assert end == len(source)
    tokens.append(dict(kind='END_OF_FILE', text='EOF', start=end, end=end))
    return tokens


def value(begin=0, end=0, typ=None, nodes=None, unary=False):
    return dict(begin=begin, end=end, type=typ, nodes=nodes or [], unary=unary)


SEQUENCES = {'程序', '成员序列', '局部声明序列', '语句序列', '表达后缀序列', '形参表可选', '实参可选'}
EXPRESSIONS = {'表达式', '逻辑或式', '逻辑与式', '相等式', '关系式', '加法式', '乘法式'}
TAILS = {'赋值续', '逻辑或续', '逻辑与续', '相等续', '关系续', '加法续', '乘法续'}


def category(lhs):
    if lhs == '收尾': return '程序收尾'
    if lhs in SEQUENCES or lhs in {'形参续', '实参续'}: return '列表合并'
    if lhs in EXPRESSIONS or lhs in TAILS or lhs in {'一元式', '后缀式', '主式', '字符串', '字符串续', '表达后缀'}: return '表达式'
    if lhs in {'块', '语句', '否则可选', '表达式可选'}: return '块与语句'
    return '声明与类型'


class Simulator:
    def __init__(self, source, title):
        self.source, self.title = source, title
        self.tokens = lex(source)
        self.nodes, self.scenes = {}, []
        self.pos = 0

    def node(self, kind, begin, end, name='', children=None, typ=None):
        nid = f'n{len(self.nodes) + 1}'
        self.nodes[nid] = dict(id=nid, kind=kind, name=name, children=list(children or []), type=deepcopy(typ), begin=begin, end=end)
        return nid

    def specs(self, begin):
        kind = self.tokens[begin]['kind']
        return {'kind': {'KW_VOID': 'void', 'KW_CHAR': 'char', 'KW_INT': 'int', 'KW_FLOAT': 'float'}.get(kind, 'struct'),
                **({'name': self.tokens[begin + 1]['text']} if kind == 'KW_STRUCT' else {})}

    def array(self, begin, end):
        if begin == end: return None
        text = self.tokens[begin + 1]['text']
        return dict(kind='array', length=None if text == ']' else int(text, 0), base=None)

    def graph(self, roots):
        seen = {}
        def visit(nid):
            if nid in seen: return
            seen[nid] = deepcopy(self.nodes[nid])
            for child in self.nodes[nid]['children']: visit(child)
        for root in roots: visit(root)
        return seen

    def bind(self, nid, base):
        n = self.nodes[nid]
        if n['type'] is None: n['type'] = deepcopy(base)
        else: n['type']['base'] = deepcopy(base)

    def reduce(self, p, v, base, stack):
        lhs, count = p['lhs'], len(v)
        out, steps = value(), []
        input_values = deepcopy(v)
        input_roots = list(dict.fromkeys(n for x in v for n in x['nodes']))
        before = self.graph(input_roots)

        def mark(note, code, working=None, slot='out'):
            steps.append(dict(note=note, code=code, out=deepcopy(out), working=working, slot=slot,
                              graph=self.graph(input_roots + out['nodes'] + ([working] if working else []))))

        def sequence(head, tail, code='out = sequence(v[0], v[1]);'):
            nonlocal out
            out = deepcopy(tail)
            out['nodes'] += head['nodes']
            mark('先接过尾表，再把前项追加到末尾；列表按源码逆序保存，节点编号保持不变。', code)

        def object_node(kind, arr, init=None, end=0):
            return self.node(kind, arr['begin'], end or arr['end'], children=[init] if init else [], typ=arr['type'])

        def node_result(n, note, code):
            out['nodes'].append(n)
            mark(note, code)

        if not count:
            mark('空产生式仍返回一个空 Value：nodes=[]，type=nullptr。它也占语义值栈的一格。', 'if (!count) return out;')
        elif lhs in SEQUENCES:
            if count == 2:
                sequence(v[0], v[1])
            else:
                mark('void 表示没有形参，返回空结果；不创建 void 节点。', 'if (count == 2) out = sequence(v[0], v[1]);')
        elif lhs in {'形参续', '实参续'}:
            sequence(v[1], v[2], 'out = sequence(v[1], v[2]);')
            steps[-1]['note'] += ' v[0] 是逗号；它只参与匹配，不进入节点列表。'
        elif lhs in {'标量类型', '成员类型'}:
            out['type'] = deepcopy(v[0]['type'] or self.specs(v[0]['begin']))
            mark('只返回类型信息；nodes 为空，类型信息存在 out.type。', 'out.type = v[0].type ? v[0].type : specs(v[0].begin);')
        elif lhs == '数组可选':
            out['type'] = self.array(v[0]['begin'], v[2]['end'])
            mark('建立数组类型并记录长度；此时元素类型 base 为空，所属声明随后补上。', 'out.type = declarator(v[0].begin, v[2].end);')
        elif lhs in {'对象续', '成员续'}:
            member = lhs == '成员续'
            i = 3 if member else 4
            out = deepcopy(v[i])
            mark('接过后面的对象列表；即将追加当前逗号后面的对象。', f'out = std::move(v[{i}]);')
            init = None if member or not v[3]['nodes'] else v[3]['nodes'][0]
            n = object_node('MemberDecl' if member else 'VarDecl', v[2], init, v[2]['end'] if member else v[3]['end'])
            self.nodes[n]['name'] = self.tokens[v[1]['begin']]['text']
            node_result(n, '创建当前对象，填入名字和数组信息；有初值时把初值节点接为孩子。基础类型留给声明补齐。', 'auto n = object(...);\nn->name = tokens[v[1].begin].lexeme;\nout.nodes.push_back(n);')
        elif lhs == '对象尾':
            out = deepcopy(v[2])
            mark('接过对象续的列表，其中已经有后面的变量。', 'out = std::move(v[2]);')
            n = object_node('VarDecl', v[0], v[1]['nodes'][0] if v[1]['nodes'] else None, v[1]['end'])
            node_result(n, '把第一个变量追加到逆序列表；它的名字和基础类型稍后由声明补齐。', 'out.nodes.push_back(object(NodeType::VarDecl, v[0], ..., v[1].end));')
            self.nodes[out['nodes'][0]]['end'] = v[3]['end']
        elif lhs in {'定义后', '结构声明尾'}:
            if count == 2:
                out = deepcopy(v[1])
                self.nodes[out['nodes'][-1]]['name'] = self.tokens[v[0]['begin']]['text']
                mark('传递对象尾的列表，并给其中第一个源码对象补名字。', 'out = std::move(v[1]);\nout.nodes.back()->name = tokens[v[0].begin].lexeme;')
            elif lhs == '结构声明尾':
                sequence(v[1], v[2], 'auto members = sequence(v[1], v[2]);')
                steps[-1]['slot'] = 'members（临时列表）'
                members = deepcopy(out)
                out = deepcopy(v[4])
                mark('保留定义后声明的对象列表，随后把结构体定义追加进去。', 'out = std::move(v[4]);')
                n = self.node('StructDef', v[0]['begin'], v[4]['end'] if not out['nodes'] else v[3]['end'], children=list(reversed(members['nodes'])))
                node_result(n, '新建 StructDef，成员列表倒回源码顺序后成为 children；结构体名稍后补上。', 'n = node(NodeType::StructDef, ...,\n    {members.nodes.rbegin(), members.nodes.rend()});\nout.nodes.push_back(n);')
            else:
                mark('这里只有分号，返回空结果；结构体本身由上一层建立。', 'if (count == 2) { ... } // 此处分支不进入')
        elif lhs == '函数尾':
            children = list(reversed(v[1]['nodes'])) + v[3]['nodes']
            n = self.node('FunctionDef', v[0]['begin'], v[3]['end'], children=children)
            node_result(n, '形参倒回源码顺序，函数体 Block 接在最后；函数名和返回类型由外部声明补上。', 'n = node(NodeType::FunctionDef, ...,\n    {v[1].nodes.rbegin(), v[1].nodes.rend()});\nn->children.push_back(v[3].nodes[0]);\nout.nodes.push_back(n);')
        elif lhs in {'外部声明', '局部声明', '成员声明'}:
            if lhs == '成员声明':
                out = deepcopy(v[3])
                n = object_node('MemberDecl', v[2])
                self.nodes[n]['name'] = self.tokens[v[1]['begin']]['text']
                out['nodes'].append(n)
                self.nodes[out['nodes'][0]]['end'] = v[4]['end']
            else: out = deepcopy(v[2])
            mark('接过声明尾的节点列表；成员声明还要把首个成员追加进去。', 'declaration(out, v, symbol);\n// out = std::move(v[2]);（成员声明使用 v[3]）')
            typ = v[0]['type'] or self.specs(v[0]['begin'])
            for nid in out['nodes']:
                n = self.nodes[nid]
                n['begin'] = v[0]['begin']
                if n['kind'] == 'FunctionDef':
                    n['name'] = self.tokens[v[1]['begin']]['text']
                    n['type'] = dict(kind='function', base=deepcopy(typ), params=[deepcopy(self.nodes[c]['type']) for c in n['children'][:-1]])
                elif n['kind'] == 'StructDef':
                    n['name'] = self.tokens[v[1]['begin']]['text']
                else:
                    if not n['name']: n['name'] = self.tokens[v[1]['begin']]['text']
                    self.bind(nid, typ)
            mark('给原节点补名字、基础类型及源码起点；数组补元素类型，函数补返回类型和形参类型。节点编号不变。', 'for (auto n : out.nodes) {\n    // 补名字；bind_type(n, base)；或完成 Function 类型\n}')
        elif lhs == '形参':
            typ = v[2]['type'] if count == 3 else self.array(v[3]['begin'], v[5]['end'])
            n = self.node('ParamDecl', v[0]['begin'], v[-1]['end'], self.tokens[v[1 if count == 3 else 2]['begin']]['text'], typ=typ)
            self.bind(n, v[0]['type'] or self.specs(v[0]['begin']))
            node_result(n, '创建一个完整形参，填名字和类型；结构体数组的类型为 array → struct。', 'n = node(NodeType::ParamDecl, ...);\nn->declared_type = ...;\nbind_type(n, ...);\nout.nodes.push_back(n);')
        elif lhs == '块':
            n = self.node('Block', v[0]['begin'], v[3]['end'], children=list(reversed(v[1]['nodes'])) + list(reversed(v[2]['nodes'])))
            node_result(n, '局部声明和语句分别倒回源码顺序，先声明、后语句，全部接到一个 Block 下。', 'block(out, v);\n// children = reverse(v[1].nodes) + reverse(v[2].nodes)')
        elif lhs == '语句':
            token = self.tokens[v[0]['begin']]['kind']
            if token == 'LBRACE':
                out['nodes'] = list(v[0]['nodes'])
                mark('这个语句就是块，直接返回已有 Block，不再套一层语句节点。', 'out.nodes = std::move(v[0].nodes); return;')
            else:
                kind = {'SEMI': 'Empty', 'KW_IF': 'If', 'KW_WHILE': 'While', 'KW_FOR': 'For', 'KW_RETURN': 'Return', 'KW_BREAK': 'Break', 'KW_CONTINUE': 'Continue'}.get(token, 'ExprStmt')
                children = []
                if kind in {'If', 'While'}:
                    children = [v[2]['nodes'][0], v[4]['nodes'][0]]
                    if kind == 'If' and v[5]['nodes']: children += v[5]['nodes']
                elif kind == 'For':
                    for i in (2, 4, 6):
                        children.append(v[i]['nodes'][0] if v[i]['nodes'] else self.node('Empty', v[i]['begin'], v[i]['begin'] + 1))
                    children.append(v[8]['nodes'][0])
                elif kind == 'Return': children = list(v[1]['nodes'])
                elif kind == 'ExprStmt': children = list(v[0]['nodes'])
                n = self.node(kind, v[0]['begin'], v[-1]['end'], children=children)
                order = {'If': '条件、then 块、可选 else 块', 'While': '条件、循环体', 'For': '初始化、条件、更新、循环体', 'Return': '可选返回表达式', 'ExprStmt': '表达式'}.get(kind, '没有孩子')
                node_result(n, f'建立 {kind}，孩子顺序是：{order}。' + ('缺省的三个表达式各补一个 Empty 占位节点。' if kind == 'For' and any(not v[i]['nodes'] for i in (2,4,6)) else ''), 'statement(out, v, count);\n// 建立节点，按固定顺序填写 children\nout.nodes.push_back(n);')
        elif lhs in {'初值可选', '否则可选'}:
            out['nodes'] = list(v[1]['nodes'])
            mark('丢开用于匹配的 = 或 else，只传递第二个右部符号的节点结果。', 'out.nodes = std::move(v[1].nodes);')
        elif lhs in EXPRESSIONS:
            n = v[0]['nodes'][0]
            out['unary'] = v[0]['unary']
            mark('先取左边已经完成的表达式；尾表按源码逆序保存，接下来反向遍历。', 'auto n = v[0].nodes[0];\nout.unary_form = v[0].unary_form;', working=n)
            for op in reversed(v[1]['nodes']):
                assert self.nodes[op]['kind'] != 'Assign' or out['unary'], '赋值左部形状错误'
                self.nodes[op]['children'].insert(0, n)
                self.nodes[op]['begin'] = self.nodes[n]['begin']
                n = op
                out['unary'] = False
                mark('把当前左子树插入运算节点的第一个孩子位置；右孩子原先已经存在。这个运算节点成为新结果。', 'op->children.insert(op->children.begin(), n);\nn = op; out.unary_form = false;', working=n)
            if not v[1]['nodes']: steps[-1]['note'] = '尾表为空，直接沿用左边的根节点；这一层不新建节点。'
            node_result(n, '循环结束，把工作根 n 放进 out.nodes；返回结果只有这一个根。', 'out.nodes.push_back(n);')
        elif lhs in TAILS:
            if count == 3: out = deepcopy(v[2])
            n = self.node('Assign' if lhs == '赋值续' else 'BinaryOp', v[0]['begin'], v[1]['end'], self.tokens[v[0]['begin']]['text'], v[1]['nodes'])
            node_result(n, '建立暂缺左孩子的运算节点，先接上右孩子，再追加到逆序尾表。上层 expression() 才接入左孩子。' + ('赋值的右边已是完整表达式，所以赋值按右结合构树。' if lhs == '赋值续' else ''), 'if (count == 3) out = std::move(v[2]);\nout.nodes.push_back(node(..., {v[1].nodes[0]}));')
        elif lhs in {'一元式', '主式', '字符串'}:
            begin, end = v[0]['begin'], v[-1]['end']
            token = self.tokens[begin]
            out['unary'] = True
            if lhs == '一元式':
                n = v[0]['nodes'][0] if count == 1 else self.node('UnaryOp', begin, end, token['text'], v[1]['nodes'])
                note = '沿用后缀式的节点。' if count == 1 else '新建一元运算节点，把操作数接成唯一孩子。'
            elif lhs == '字符串':
                n = self.node('StringLiteral', begin, end, ' '.join(t['text'] for t in self.tokens[begin:end]))
                note = '用整个 Token 区间建立一个字符串节点；字符串续只提供区间，不建立自己的节点。'
            elif token['kind'] == 'LPAREN':
                n = v[1]['nodes'][0]
                self.nodes[n].update(begin=begin, end=end)
                note = '沿用括号内的根，扩大源码范围；不创建括号节点。'
            elif v[0]['nodes']:
                n = v[0]['nodes'][0]
                note = '沿用字符串已经创建的节点。'
            else:
                n = self.node({'INT_LITERAL': 'IntLiteral', 'FLOAT_LITERAL': 'FloatLiteral', 'CHAR_LITERAL': 'CharLiteral'}.get(token['kind'], 'Identifier'), begin, end, token['text'])
                note = '把 Token 建成叶节点，名字保存 Token 原文。'
            node_result(n, note + ' out.unary_form=true，供赋值左部形状检查。', 'unary(out, v, symbol, count);\n// 新建或沿用节点；out.unary_form = true')
        elif lhs == '后缀式':
            n = v[0]['nodes'][0]
            out['unary'] = True
            mark('先取主式根，再按源码顺序接入调用、下标、成员访问后缀。', 'auto n = v[0].nodes[0]; out.unary_form = true;', working=n)
            for suffix in reversed(v[1]['nodes']):
                s = self.nodes[suffix]
                if s['kind'] == 'Call' and self.nodes[n]['kind'] == 'Identifier': s['name'] = self.nodes[n]['name']
                s['begin'] = self.nodes[n]['begin']
                s['children'].insert(0, n)
                n = suffix
                mark('把前一步结果插成后缀节点的第一个孩子；新根变成这个后缀节点。', 'suffix->children.insert(suffix->children.begin(), n);\nn = suffix;', working=n)
            if not v[1]['nodes']: steps[-1]['note'] = '没有后缀，直接返回主式根；不新建节点。'
            node_result(n, '把最终的后缀式根放进 out.nodes。', 'out.nodes.push_back(n);')
        elif lhs == '表达后缀':
            if count == 2:
                kind, name, children = 'MemberAccess', self.tokens[v[1]['begin']]['text'], []
                note = '先建成员访问节点，记录成员名；被访问的对象由后缀式补成第一个孩子。'
            elif self.tokens[v[0]['begin']]['kind'] == 'LPAREN':
                kind, name, children = 'Call', '', list(reversed(v[1]['nodes']))
                note = '实参倒回源码顺序，先接在 Call 下；被调用对象由后缀式补成第一个孩子。'
            else:
                kind, name, children = 'ArrayAccess', '', list(v[1]['nodes'])
                note = '先建下标访问节点，接上下标表达式；数组对象由后缀式补成第一个孩子。'
            n = self.node(kind, v[0]['begin'], v[-1]['end'], name, children)
            node_result(n, note, 'n = node(NodeType::' + kind + ', ...);\nout.nodes.push_back(n);')
        else:
            if count == 1:
                out = deepcopy(v[0])
                mark('单个右部结果直接向上传递；节点、类型与一元式标记都随它传递。', 'default: if (count == 1) out = std::move(v[0]);')
            else:
                mark('字符串续只负责匹配后续字符串；nodes 仍为空，run() 会统一记录起止 Token 区间。', 'default: if (count == 1) out = std::move(v[0]);\n// count == 2，保留空 out')

        begin = v[0]['begin'] if count else self.pos
        out.update(begin=begin, end=self.pos)
        # run() 填区间发生在 reduce() 返回后；单独展示，避免把它误当 helper 的工作。
        mark('reduce() 返回后，run() 给结果填写完整 Token 区间 [begin, pos)。', 'value.begin = begin; value.end = pos;')
        a = self.tokens[begin]['start']
        b = self.tokens[self.pos - 1]['end'] if self.pos > begin else a
        snippet = self.source[a:b]
        scene = dict(pid=p['id'], lhs=lhs, rhs=p['rhs'], category=category(lhs), source=self.title,
                     snippet=snippet, tokenStart=begin, pos=self.pos, base=base, count=count,
                     inputs=input_values, before=before, steps=steps, output=deepcopy(out),
                     stack=stack[-9:], inputNext=self.tokens[self.pos]['text'])
        self.scenes.append(scene)
        return out

    def run(self):
        stack = [('END_OF_FILE', None, 0), ('程序', None, 0)]
        values = []
        while stack:
            symbol, production, base = stack.pop()
            if production is not None:
                p = productions[production]
                assert len(values) - base == len(p['rhs'])
                remaining = [f'完成 {productions[x[1]]["id"]}' if x[1] is not None else spellings.get(x[0], x[0]) for x in stack]
                out = self.reduce(p, values[base:], base, remaining)
                values[base:] = [out]
            elif symbol in terminals:
                assert self.tokens[self.pos]['kind'] == symbol, (symbol, self.tokens[self.pos], self.title)
                values.append(value(self.pos, self.pos + (symbol != 'END_OF_FILE')))
                if symbol != 'END_OF_FILE': self.pos += 1
            else:
                choice = table[symbol][self.tokens[self.pos]['kind']]
                assert choice >= 0, (self.title, symbol, self.tokens[self.pos], self.source[max(0,self.tokens[self.pos]['start']-60):self.tokens[self.pos]['start']+40])
                stack.append((symbol, choice, len(values)))
                stack.extend((s, None, 0) for s in reversed(productions[choice]['rhs']))
        assert len(values) == 2 and values[1]['begin'] == values[1]['end']
        input_roots = values[0]['nodes']
        before = self.graph(input_roots)
        root = self.node('Program', 0, self.pos, children=list(reversed(input_roots)))
        output = value(0, self.pos, nodes=[root])
        self.scenes.append(dict(pid='FINAL', lhs='收尾', rhs=['程序', 'END_OF_FILE'], category='程序收尾', source=self.title,
            snippet=self.source, tokenStart=0, pos=self.pos, base=0, count=2, inputs=deepcopy(values), before=before,
            steps=[dict(note='EOF 匹配后分析栈已空。values[0] 保存程序的逆序顶层列表，values[1] 是 EOF 的空 Value。', code='while (!stack.empty()) { ... }', out=value(), graph=before),
                   dict(note='创建 Program，把 values[0].nodes 倒回源码顺序作为 children，返回整棵 AST。这里不再 resize/push。', code='auto root = node(NodeType::Program, 0, pos, "",\n    {values[0].nodes.rbegin(), values[0].nodes.rend()});\nresult.root = root;', out=output, graph=self.graph([root]))],
            output=output, stack=[], inputNext='EOF'))
        return self.scenes


all_scenes = []
for title, source in sources:
    all_scenes.extend(Simulator(source, title).run())
covered = {s['pid'] for s in all_scenes}
missing = {p['id'] for p in productions} - covered
assert not missing, f'未覆盖: {sorted(missing)}'

# 每条产生式保留多种有差异的场景：空/非空尾表、不同孩子、已有类型等。
groups = []
for pid in [p['id'] for p in productions] + ['FINAL']:
    candidates = [s for s in all_scenes if s['pid'] == pid]
    def score(s):
        return sum(len(v['nodes']) for v in s['inputs']) + sum(len(n['children']) for n in s['before'].values()) * .05
    ranked = sorted(candidates, key=score, reverse=True)
    chosen, signatures = [], set()
    for s in ranked:
        sig = tuple((min(3,len(v['nodes'])), str(v['type']), tuple(s['before'][n]['kind'] for n in v['nodes'][:3])) for v in s['inputs'])
        # For / Return / If 的缺省分支即使节点种类相同也要保留。
        if s['lhs'] == '语句': sig += (tuple(s['before'][n]['kind'] for v in s['inputs'] for n in v['nodes']),)
        if sig not in signatures:
            chosen.append(s); signatures.add(sig)
        if len(chosen) >= 4: break
    first = chosen[0]
    groups.append(dict(pid=pid, lhs=first['lhs'], rhs=first['rhs'], category=first['category'], scenes=chosen))

data = dict(groups=groups, spellings=spellings, totalProductions=len(productions),
            parserHash=hashlib.sha256(parser_source.encode()).hexdigest()[:12],
            grammarHash=hashlib.sha256(header.encode()).hexdigest()[:12])
template = (HERE / 'reduce-actions.template.html').read_text('utf-8')
(HERE / 'reduce-actions.html').write_text(template.replace('__REDUCE_DATA__', json.dumps(data, ensure_ascii=False, separators=(',', ':')).replace('</', '<\\/')), 'utf-8')
print(f'覆盖 {len(covered)-1}/{len(productions)} 条产生式；{len(all_scenes)} 次真实查表归约；保留 {sum(len(g["scenes"]) for g in groups)} 个教学场景。')
print(f'parser snapshot: {data["parserHash"]}')
