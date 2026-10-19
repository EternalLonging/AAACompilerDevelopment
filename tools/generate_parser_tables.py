"""从当前递归下降实现整理文法骨架，推导集合并核对语法资料。"""
from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET
import zipfile

from generate_lexer_tables import check_workbook

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / '实验设计/语法分析/语法表格'
TARGET = OUTPUT / '语法分析表.xlsx'
GRAMMAR_TARGET = OUTPUT / '文法表.xlsx'
PARSER = ROOT / 'src/语法分析/parser.cpp'
EPSILON = 'ε'
END = 'END_OF_FILE'

# 每组记录实现函数；右部的英文大写名字直接使用 TokenType。
# 这是带约束的文法骨架；限制见 CONSTRAINTS，不把它当成独立识别器。
GRAMMAR = '''
@run
程序 -> 外部序列
外部序列 -> 声明 外部序列 | ε
@declaration
声明 -> 说明符序列 声明尾
声明尾 -> SEMI | 声明符 声明后续
声明后续 -> 块 | 初值可选 声明逗号续 SEMI
声明逗号续 -> COMMA 声明符 初值可选 声明逗号续 | ε
初值可选 -> ASSIGN 初值 | ε
@specs
说明符序列 -> 说明符 说明符续
说明符续 -> 说明符 说明符续 | ε
说明符 -> KW_CONST | KW_VOID | KW_CHAR | KW_INT | KW_FLOAT | 结构说明
结构说明 -> KW_STRUCT 标签可选 结构体可选
标签可选 -> ID | ε
结构体可选 -> LBRACE 成员序列 RBRACE | ε
成员序列 -> 成员声明 成员续
成员续 -> 成员声明 成员续 | ε
@declaration
成员声明 -> 说明符序列 声明符 成员逗号续 SEMI
成员逗号续 -> COMMA 声明符 成员逗号续 | ε
@declarator
声明符 -> 直接声明符 声明后缀序列
直接声明符 -> ID | LPAREN 声明符 RPAREN
声明后缀序列 -> 声明后缀 声明后缀序列 | ε
声明后缀 -> LBRACKET 界限可选 RBRACKET | LPAREN 形参表可选 RPAREN
界限可选 -> INT_LITERAL | ε
形参表可选 -> KW_VOID | 形参序列 | ε
形参序列 -> 形参 形参续
形参续 -> COMMA 形参 形参续 | ε
形参 -> 说明符序列 抽象声明符
抽象声明符 -> 直接声明符 声明后缀序列 | 声明后缀序列
@initializer
初值 -> 赋值式 | LBRACE 初值 初值列表续 RBRACE
初值列表续 -> COMMA 初值逗号后 | ε
初值逗号后 -> 初值 初值列表续 | ε
@block
块 -> LBRACE 局部声明序列 语句序列 RBRACE
局部声明序列 -> 声明 局部声明序列 | ε
语句序列 -> 语句 语句序列 | ε
@statement
语句 -> 块 | SEMI | KW_IF LPAREN 表达式 RPAREN 块 否则可选 | KW_WHILE LPAREN 表达式 RPAREN 块 | KW_FOR LPAREN 表达式可选 SEMI 表达式可选 SEMI 表达式可选 RPAREN 块 | KW_RETURN 表达式可选 SEMI | KW_BREAK SEMI | KW_CONTINUE SEMI | 表达式 SEMI
否则可选 -> KW_ELSE 块 | ε
表达式可选 -> 表达式 | ε
@expression
表达式 -> 赋值式
赋值式 -> 逻辑或式 赋值续
赋值续 -> ASSIGN 赋值式 | ε
逻辑或式 -> 逻辑与式 逻辑或续
逻辑或续 -> OR 逻辑与式 逻辑或续 | ε
逻辑与式 -> 相等式 逻辑与续
逻辑与续 -> AND 相等式 逻辑与续 | ε
相等式 -> 关系式 相等续
相等续 -> EQ 关系式 相等续 | NE 关系式 相等续 | ε
关系式 -> 加法式 关系续
关系续 -> LT 加法式 关系续 | LE 加法式 关系续 | GT 加法式 关系续 | GE 加法式 关系续 | ε
加法式 -> 乘法式 加法续
加法续 -> PLUS 乘法式 加法续 | MINUS 乘法式 加法续 | ε
乘法式 -> 一元式 乘法续
乘法续 -> STAR 一元式 乘法续 | SLASH 一元式 乘法续 | PERCENT 一元式 乘法续 | ε
@unary
一元式 -> PLUS 一元式 | MINUS 一元式 | NOT 一元式 | AMP 一元式 | 后缀式
后缀式 -> 主式 表达后缀序列
主式 -> ID | INT_LITERAL | FLOAT_LITERAL | CHAR_LITERAL | 字符串 | LPAREN 表达式 RPAREN
字符串 -> STRING_LITERAL 字符串续
字符串续 -> STRING_LITERAL 字符串续 | ε
@postfix
表达后缀序列 -> 表达后缀 表达后缀序列 | ε
表达后缀 -> LPAREN 实参可选 RPAREN | LBRACKET 表达式 RBRACKET | DOT ID
实参可选 -> 赋值式 实参续 | ε
实参续 -> COMMA 赋值式 实参续 | ε
'''

# 编号用于把文法骨架中的上下文条件定位到实现，不能由 FIRST/FOLLOW 代替。
CONSTRAINTS = [
    ('C01', '类型说明', 'specs', '必须且只能出现一个基础类型；const 可重复，可在类型前后出现。单独 const 不构成类型。', 'PARSE_SPECIFIERS'),
    ('C02', '仅类型声明', 'declaration', '只有本条实际带完整 struct 定义时才能省略声明符；struct S; 被拒绝。', 'PARSE_RECORD_FORWARD / PARSE_DECLARATOR'),
    ('C03', '函数定义', 'declaration', '声明符为函数且后接块时，只能在顶层并作为本条第一个声明符；函数声明和原型被拒绝。', 'PARSE_FUNCTION / PARSE_FUNCTION_DECL_REMOVED'),
    ('C04', '定义形参', 'declaration', '函数定义中的每个形参都必须具名。空列表或单独 void 表示无形参。', 'PARSE_PARAMETER'),
    ('C05', '结构体', 'specs', '定义必须具名且至少有一个成员；标签解析与先定义要求在语义阶段完成。', 'PARSE_RECORD_ANONYMOUS / PARSE_RECORD'),
    ('C06', '结构体成员', 'declaration', '成员须有名字，不能有初始化、函数或位域；成员类型不能内联定义 struct。', 'PARSE_MEMBER / PARSE_INITIALIZER'),
    ('C07', '数组', 'declarator', '同一声明符最多一层数组；已写出的长度须为正整数字面量、无 U/L 后缀且可表示，省略长度交给语义推导或拒绝。', 'PARSE_ARRAY_DIMENSION / PARSE_ARRAY_BOUND'),
    ('C08', '类型层次', 'build_type', '函数不能返回数组或函数；数组元素不能是函数或 void。', 'PARSE_DECLARATOR'),
    ('C09', '形参类型', 'declarator', '形参不能内联定义记录类型、不能按值传 struct、不能是独立 void；不支持省略号。', 'PARSE_PARAMETER / PARSE_FUNCTION_REMOVED'),
    ('C10', '函数返回', 'declaration', '不支持 struct 返回值。变量声明中的 void 等合法性仍由语义阶段检查。', 'PARSE_FUNCTION_REMOVED'),
    ('C11', '块与控制体', 'block', '同一块中声明在语句前；if、else、while、for 的体必须为块。else if 要嵌入 else 的块。', 'PARSE_DECLARATION_ORDER / PARSE_EXPECTED'),
    ('C12', 'for 初始化', 'statement', '三部分可为空；初始化只能是表达式，不能是声明。', 'PARSE_FOR_DECLARATION'),
    ('C13', '赋值目标', 'expression', '赋值左部须具一元表达式形状或来自括号；a+b=1 语法拒绝，(a+b)=1 通过语法后由语义检查左值。', 'PARSE_ASSIGNMENT_TARGET'),
    ('C14', '表达式树', 'expression', '尾部递归只是消除左递归的文法写法；实现把二元运算折叠为左结合，赋值右结合。', '—'),
    ('C15', '取地址与调用', 'unary', '前缀 & 在语法阶段接受，语义仅允许 scanf 输入寻址；调用与下标先构造 AST，再检查直接调用和一维对象。', '语义检查'),
    ('C16', '字面量', 'unary', '整数 U/L 后缀和浮点 L 后缀语法拒绝；相邻字符串合并原文，宽字符等解码限制由语义检查。', 'PARSE_TYPE_REMOVED'),
    ('C17', '范围外构造', 'statement', '不支持指针、扩展类型、存储类别、typedef、union/enum、do/switch/goto/标签、位运算、移位、复合赋值、三目、自增自减、sizeof 和强制转换。', '见语法诊断表'),
    ('C18', '循环跳转', 'statement', 'break/continue 的位置由语义检查；文法只识别关键字与分号。', '语义检查'),
]

AST = [
    ('Program', 'run', '—', '顶层声明与定义，按源码顺序', '零个或多个'),
    ('FunctionDef', 'declaration', '函数名；完整函数声明类型', 'ParamDecl 列表，最后一个必须是 Block', '至少一个'),
    ('ParamDecl', 'declarator', '形参名；形参声明类型', '无', '零个'),
    ('VarDecl', 'declaration', '变量名；声明类型', '可选的初始化表达式或 InitList', '零个或一个'),
    ('MemberDecl', 'declaration', '成员名；声明类型', '无；成员初始化被拒绝', '零个'),
    ('StructDef', 'specs', '结构体标签名', 'MemberDecl，按声明顺序', '一个或多个'),
    ('Block', 'block', '—', '先声明再语句，均按源码顺序', '零个或多个'),
    ('ExprStmt', 'statement', '—', '表达式', '一个'),
    ('If', 'statement', '—', '条件、真分支块、可选的假分支块', '两个或三个'),
    ('While', 'statement', '—', '条件、循环体块', '两个'),
    ('For', 'statement', '—', '初始化、条件、更新、循环体块；缺失分量用 Empty', '固定四个'),
    ('Return', 'statement', '—', '可选返回表达式', '零个或一个'),
    ('Empty', 'statement', '—', '无；用于空语句和缺失的 for 分量', '零个'),
    ('Break', 'statement', '—', '无；循环上下文由语义检查', '零个'),
    ('Continue', 'statement', '—', '无；循环上下文由语义检查', '零个'),
    ('Assign', 'expression', '=', '目标表达式、来源表达式', '两个'),
    ('BinaryOp', 'expression', '运算符原文', '左操作数、右操作数', '两个'),
    ('UnaryOp', 'unary', '+、-、! 或 &', '操作数', '一个'),
    ('Call', 'postfix', '直接调用时保留函数名', '被调表达式、实参 0、实参 1、…', '至少一个'),
    ('ArrayAccess', 'postfix', '—', '数组表达式、下标表达式', '两个'),
    ('MemberAccess', 'postfix', '成员名', '对象表达式；只接受点号访问', '一个'),
    ('InitList', 'initializer', '—', '初始化项，按源码顺序', '一个或多个'),
    ('Identifier', 'unary', '标识符原文', '无', '零个'),
    ('IntLiteral', 'unary', '整数字面量原文', '无', '零个'),
    ('FloatLiteral', 'unary', '浮点字面量原文', '无', '零个'),
    ('CharLiteral', 'unary', '带引号和转义的原文', '无', '零个'),
    ('StringLiteral', 'unary', '各段原文以空格拼接，保留引号与转义', '无；解码由语义完成', '零个'),
]

RECOVERY = [
    ('输入协议', 'parse', '必须且只能有一个末尾 END_OF_FILE；缺失、提前或重复均拒绝。', 'PARSE_TOKEN_STREAM'),
    ('类型生命周期', 'parse', '直接调用前建立 TypeArenaScope，类型管理器保留到 AST 用完；节点由 ParseResult.arena 统一释放。', '接口约定'),
    ('普通语法错误', 'fail', '记录当前位置、错误码和中文消息后抛出 ParseFailure，由顶层或块捕获。', 'Error'),
    ('块内恢复', 'recover', '跳过分号前的错误内容；跟踪内嵌花括号，在当前块右花括号前停下，由 block 消费。', 'external=false'),
    ('顶层恢复', 'recover', '可消费孤立右花括号；跳过错误内容后，遇到下一个顶层说明符也可停止。', 'external=true'),
    ('错误上限', 'stopped', '累计 20 条诊断后停止；第 20 条追加“错误过多，停止本阶段”。', '20 条'),
    ('递归与树高', 'finish', '递归深度与 AST 高度均限制为 128；声明符层数也有限制；超限立即停止。', 'PARSE_DEPTH / Fatal'),
    ('失败产物', 'run', '只要存在 Error 或 Fatal，root 为空；恢复所得部分树不作为成功结果交给后续阶段。', 'ParseResult'),
    ('成功产物', 'run', 'Program 按源码顺序保存顶层节点；声明类型已填写，绑定、类型标注和隐式转换交给语义。', 'ParseResult'),
]


def productions(text=GRAMMAR):
    """读取文法分组和候选式，返回按顺序编号的产生式。"""
    rows, function = [], 'run'
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith('@'):
            function = line[1:]
            continue
        lhs, rhs = line.split(' -> ')
        for alternative in rhs.split(' | '):
            symbols = [] if alternative == EPSILON else alternative.split()
            rows.append({'id': f'P{len(rows)+1:03}', 'lhs': lhs, 'rhs': symbols, 'function': function})
    return rows


def derive(rows, start='程序', end=END):
    """用不动点算法计算 nullable、FIRST、FOLLOW、SELECT 和全部预测格。"""
    names = list(dict.fromkeys(row['lhs'] for row in rows))
    nonterminals = set(names)
    if start not in nonterminals:
        raise ValueError('开始符号未定义')
    terminals = {item for row in rows for item in row['rhs']} - nonterminals
    first = {name: set() for name in names}
    follow = {name: set() for name in names}
    nullable = set()

    def sequence(items):
        result = set()
        for item in items:
            result.update(first[item] if item in nonterminals else {item})
            if item not in nullable:
                return result, False
        return result, True

    changed = True
    while changed:
        changed = False
        for row in rows:
            values, empty = sequence(row['rhs'])
            old = len(first[row['lhs']])
            first[row['lhs']].update(values)
            changed |= old != len(first[row['lhs']])
            if empty and row['lhs'] not in nullable:
                nullable.add(row['lhs'])
                changed = True
    follow[start].add(end)
    changed = True
    while changed:
        changed = False
        for row in rows:
            for index, item in enumerate(row['rhs']):
                if item not in nonterminals:
                    continue
                values, empty = sequence(row['rhs'][index+1:])
                if empty:
                    values.update(follow[row['lhs']])
                old = len(follow[item])
                follow[item].update(values)
                changed |= old != len(follow[item])
    select, table = {}, defaultdict(list)
    for row in rows:
        values, empty = sequence(row['rhs'])
        select[row['id']] = values | (follow[row['lhs']] if empty else set())
        for token in sorted(select[row['id']]):
            table[row['lhs'], token].append(row['id'])
    return {'names': names, 'terminals': sorted(terminals | {end}), 'nullable': nullable,
            'first': first, 'follow': follow, 'select': select, 'table': dict(table)}


def source_reference(function, source):
    """返回实现函数的源码位置，函数未找到时拒绝生成。"""
    pattern = rf'^[ \t]*(?:\[\[noreturn\]\]\s*)?(?:static\s+)?[\w:<>&*]+\s+{re.escape(function)}\([^;{{}}]*?\)\s*(?:const\s*)?\{{'
    match = re.search(pattern, source, re.M)
    if not match:
        raise ValueError('实现函数不存在：' + function)
    line = source.count('\n', 0, match.start()) + 1
    return f'src/语法分析/parser.cpp:{line}'


def conflict_reason(name):
    """说明骨架文法的预测冲突如何由实际递归下降分支处理。"""
    if name == '标签可选':
        return 'specs 总是先消费 struct 后的 ID 作为标签；不把它解释成无标签对象声明。', 'specs'
    if name == '形参表可选':
        return 'declarator 查看 void 后一项：只有 void 后紧接右括号时才作为空形参列表。', 'declarator'
    if name in {'抽象声明符', '声明后缀序列'}:
        return 'declarator 查看左括号后的种别：ID、左括号或星号走分组分支；其余按函数后缀处理，星号随后拒绝。', 'declarator'
    raise ValueError('新增预测冲突需要解释：' + name)


def build_payload():
    """结合当前源码生成中文工作表、文法文件和范围说明。"""
    rows = productions()
    dfa = derive(rows)
    source = PARSER.read_text(encoding='utf-8')
    interface = (ROOT / 'include/minic/interface.hpp').read_text(encoding='utf-8')
    enum = re.search(r'enum class TokenType\s*\{(.*?)\};', interface, re.S)[1]
    token_names = re.findall(r'\b[A-Z][A-Z_]+\b', enum)
    if set(dfa['terminals']) - set(token_names):
        raise ValueError('文法含未定义符号：' + str(set(dfa['terminals']) - set(token_names)))
    reachable = {'程序'}
    while True:
        updated = reachable | {symbol for row in rows if row['lhs'] in reachable
                               for symbol in row['rhs'] if symbol in dfa['names']}
        if updated == reachable:
            break
        reachable = updated
    if reachable != set(dfa['names']):
        raise ValueError('文法含不可达非终结符')
    lexical = json.loads((ROOT / 'src/词法分析/词法规则.json').read_text(encoding='utf-8'))
    spelling = {token: word for word, token in lexical['keywords'].items()}
    spelling.update({rule['token']: rule['literal'] for rule in lexical['rules'] if 'literal' in rule})
    spelling.update({END: '输入结束', 'ID': '标识符', 'INT_LITERAL': '整数字面量',
                     'FLOAT_LITERAL': '浮点字面量', 'CHAR_LITERAL': '字符常量', 'STRING_LITERAL': '字符串'})
    order = {name: index for index, name in enumerate(token_names)}

    def tokens(values):
        return '、'.join(sorted(values, key=lambda name: order[name])) or '∅'

    references = {function: source_reference(function, source) for function in
                  {row['function'] for row in rows} | {row[2] for row in CONSTRAINTS}
                  | {row[1] for row in AST} | {row[1] for row in RECOVERY} | {'precedence'}}
    conflicts, table_rows = [], []
    for (name, token), choices in sorted(dfa['table'].items(), key=lambda pair: (dfa['names'].index(pair[0][0]), order[pair[0][1]])):
        table_rows.append([name, token, spelling[token], ' / '.join(choices), len(choices)])
        if len(choices) > 1:
            reason, function = conflict_reason(name)
            conflicts.append([name, token, ' / '.join(choices), reason, references[function]])
    priority_rows = []
    precedence = source.split('static int precedence(TT type)', 1)[1].split('default: return 0;', 1)[0]
    for cases, value in re.findall(r'((?:\s*case TT::[A-Z_]+:)+) return (\d+);', precedence):
        names = re.findall(r'TT::([A-Z_]+)', cases)
        priority_rows.append([int(value), ' '.join(spelling[name] for name in names), '、'.join(names),
                              '右结合' if value == '2' else '左结合', 'Assign' if value == '2' else 'BinaryOp',
                              references['precedence']])
    if len(priority_rows) != 7:
        raise ValueError('运算符优先级分组改变，需要核对文法')
    priority_rows += [['—', '+ - ! &', 'PLUS、MINUS、NOT、AMP', '前缀递归，右结合', 'UnaryOp', references['unary']],
                      ['—', '() [] .', 'LPAREN、LBRACKET、DOT', '后缀连续，左侧逐层包装', 'Call / ArrayAccess / MemberAccess', references['postfix']]]
    active_nodes = set(re.findall(r'NodeType::(\w+)', source))
    if active_nodes != {row[0] for row in AST}:
        raise ValueError('AST 节点变化，需要核对孩子顺序')
    diagnostic_rows = []
    codes = sorted(set(re.findall(r'PARSE_[A-Z_]+', source)))
    for code in codes:
        line_numbers = [index for index, line in enumerate(source.splitlines(), 1) if code in line]
        messages = []
        for line in line_numbers:
            text = source.splitlines()[line-1]
            literals = [json.loads('"' + value + '"') for value in re.findall(r'"((?:\\.|[^"\\])*)"', text)]
            messages.extend(value for value in literals if value != code and not value.startswith('PARSE_'))
        diagnostic_rows.append([code, 'Fatal' if code == 'PARSE_DEPTH' else 'Error',
                                '；'.join(dict.fromkeys(messages)) or ('单词表须恰好一个末尾 EOF' if code == 'PARSE_TOKEN_STREAM' else '动态消息，包含实际遇到的单词'),
                                '、'.join(map(str, line_numbers))])
    sources = ['src/语法分析/parser.cpp', 'include/minic/parser.hpp', 'include/minic/interface.hpp',
               'src/词法分析/词法规则.json', 'docs/公共约定/语言支持范围.md', 'docs/公共约定/数据结构与AST约定.md']
    # 统一换行为 LF，避免 Windows 与 Linux 检出格式不同导致快照失效。
    source_hashes = {name: hashlib.sha256((ROOT / name).read_text(encoding='utf-8').encode('utf-8')).hexdigest()
                     for name in sources}
    count = f'{len(rows)} 条产生式，{len(dfa["names"])} 个非终结符，{len(conflicts)} 个预测冲突格。'

    def sheet(name, title, context, headers, values, widths, notes=()):
        return {'name': name, 'title': title, 'context': context, 'headers': headers,
                'rows': values, 'widths': widths, 'notes': list(notes)}

    sheets = [
        sheet('文法产生式', '当前语法文法骨架', count,
              ['编号', '左部', '右部', '实现函数', '源码位置'],
              [[row['id'], row['lhs'], ' '.join(row['rhs']) or EPSILON, row['function'], references[row['function']]] for row in rows],
              [12, 24, 100, 23, 43], ['约束见“语法约束”。实际使用递归下降和优先级分析，预测表用于教学推导。',
                                     '开始符号为“程序”，ε 表示空串；END_OF_FILE 为输入结束标记，不放入产生式右部。']),
        sheet('FIRST与FOLLOW', '可空性与 FIRST / FOLLOW', '集合按 TokenType 顺序显示。FIRST 的 ε 单列为“可空”，FOLLOW 不含 ε。',
              ['非终结符', '可空', 'FIRST（不含 ε）', 'FOLLOW'],
              [[name, '是' if name in dfa['nullable'] else '否', tokens(dfa['first'][name]), tokens(dfa['follow'][name])] for name in dfa['names']],
              [24, 10, 90, 110]),
        sheet('SELECT集', '每条产生式的 SELECT', '右部可空时，将左部 FOLLOW 加入 SELECT。多个候选式可命中同一预测格。',
              ['编号', '左部', 'SELECT'], [[row['id'], row['lhs'], tokens(dfa['select'][row['id']])] for row in rows], [12, 24, 125]),
        sheet('预测分析表', '预测分析表（非空格）', '每行是一格 M[非终结符, 向前看 Token]；未列出的组合均为错误格。',
              ['非终结符', '向前看 Token', '拼写或类别', '候选产生式', '候选数'], table_rows, [24, 27, 22, 28, 12],
              ['候选数大于 1 表示预测冲突，详见“冲突与处理”；不能直接作为无冲突表驱动解析器。']),
        sheet('冲突与处理', '预测冲突与实际消歧', '保留全部冲突候选式。这里解释递归下降怎样决定，不改造 Token 类别掩盖冲突。',
              ['非终结符', '向前看 Token', '候选产生式', '实际处理', '源码位置'], conflicts, [24, 27, 28, 105, 43]),
        sheet('终结符', '当前文法使用的 Token', '只列当前文法和结束标记；词法识别但语法拒绝的种别不作为可用文法终结符。',
              ['Token 种别', '拼写或类别', '用途'], [[name, spelling[name], '输入结束标记' if name == END else '文法终结符'] for name in sorted(dfa['terminals'], key=lambda name: order[name])], [28, 27, 24]),
        sheet('运算符优先级', '表达式优先级与结合性', '二元优先级数值直接读取 precedence()，数值越大越先结合。前缀、后缀不虚构代码数值。',
              ['代码优先级', '运算符', 'Token 种别', '结合方式', 'AST 节点', '源码位置'], priority_rows, [16, 20, 57, 40, 45, 43],
              ['一般后缀先于前缀，前缀先于二元运算；括号中的表达式单独解析。逗号仅分隔参数、声明或初值，不是表达式运算符。']),
        sheet('AST孩子顺序', '语法阶段实际生成的 AST', '只列 parser.cpp 使用的节点；ImplicitCast 由语义生成，旧预留枚举不代表源码支持。',
              ['节点', '名字或声明类型', '孩子顺序', '孩子数量', '源码位置'], [[name, text, children, number, references[function]] for name, function, text, children, number in AST], [27, 57, 84, 18, 43]),
        sheet('语法约束', '文法之外的上下文条件', '骨架文法需与这些条件一起阅读；单独接受骨架不能说明源码已通过语法或语义。',
              ['编号', '类别', '条件', '诊断或阶段', '源码位置'], [[i, title, text, codes, references[function]] for i, title, function, text, codes in CONSTRAINTS], [12, 22, 115, 62, 43]),
        sheet('语法诊断', '当前语法错误代码', '错误码与出现行号从源码提取。消息列保留所在行的字符串片段；动态消息按实际 Token 拼接。',
              ['错误代码', '级别', '消息片段', 'parser.cpp 行号'], diagnostic_rows, [42, 12, 150, 32]),
        sheet('恢复与接口', '输入、错误恢复与阶段交接', '错误恢复用于继续收集诊断；存在语法错误时不向后续阶段交付成功 AST。',
              ['项目', '实现函数', '行为', '结果或限制', '源码位置'], [[title, function, text, limit, references[function]] for title, function, text, limit in RECOVERY], [22, 22, 120, 30, 43]),
    ]
    bnf = '# 当前语法文法骨架\n# 开始符号：程序；结束标记：END_OF_FILE；空串：ε\n# 约束见《当前文法与约束.md》，本文件不是独立解析器。\n'
    bnf += ''.join(f'{row["id"]}: {row["lhs"]} -> {" ".join(row["rhs"]) or EPSILON}\n' for row in rows)
    document = '# 当前文法与约束\n\n' + count + '\n\n'
    document += '依据当前 `parser.cpp` 整理。正式算法是递归下降和表达式优先级攀升；FIRST/FOLLOW/SELECT 与预测表只对本文的文法骨架计算。上下文约束、Token 原文限制和 AST 折叠规则仍由实现检查，不能仅凭预测表认定源码合法。\n\n'
    document += '开始符号为“程序”；空源码允许，`END_OF_FILE` 是输入结束标记，不在产生式右部。FIRST 的可空性单独显示，FOLLOW 不含 ε。预测表采用非空格清单，遗漏的组合为错误格；冲突格保存所有候选式。\n\n'
    document += '## 文法\n\n```bnf\n' + bnf + '```\n\n## 上下文约束\n\n| 编号 | 类别 | 条件 | 实现 |\n|---|---|---|---|\n'
    document += ''.join(f'| {i} | {title} | {text} | `{function}()` |\n' for i, title, function, text, _ in CONSTRAINTS)
    document += '\n## 预测冲突\n\n| 非终结符 | 向前看 | 候选式 | 实际处理 |\n|---|---|---|---|\n'
    document += ''.join(f'| {name} | {token} | {choices} | {reason} |\n' for name, token, choices, reason, _ in conflicts)
    document += '\n## 来源快照\n\n修改这些文件后须重新核对文法与约束并生成资料。哈希按 UTF-8、统一 LF 换行计算，用于发现来源改动，不能代替文法正确性的人工核对和回归测试。\n\n| 来源 | SHA-256 |\n|---|---|\n'
    document += ''.join(f'| [{name}](../../../{name}) | `{sha}` |\n' for name, sha in source_hashes.items())
    return {'schema': 1, 'header_row': 7, 'sheets': sheets}, bnf, document


def grammar_payload(payload):
    """从综合资料提取文法工作表，返回单表导出数据。"""
    grammar = dict(payload['sheets'][0])
    grammar['title'] = 'MiniC 文法产生式表'
    names = {row[1] for row in grammar['rows']}
    grammar['context'] = f'{len(grammar["rows"])} 条产生式，{len(names)} 个非终结符；编号与当前文法.bnf 一致。'
    grammar['notes'] = [
        '开始符号：程序。ε 表示空串；END_OF_FILE 是输入结束标记，不放入产生式右部。',
        '这是当前解析器的文法骨架；使用时须结合《当前文法与约束.md》中的上下文条件。',
    ]
    return {**payload, 'sheets': [grammar]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--data', type=Path, help='写入 Excel 数据并更新文法与说明文件')
    mode.add_argument('--grammar-data', type=Path, help='仅写入独立文法表的 Excel 数据')
    mode.add_argument('--check', action='store_true', help='检查已交付文件与当前推导及来源是否一致')
    args = parser.parse_args()
    try:
        payload, bnf, document = build_payload()
        if args.grammar_data:
            args.grammar_data.parent.mkdir(parents=True, exist_ok=True)
            args.grammar_data.write_text(json.dumps(grammar_payload(payload), ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
            print('已导出独立文法表数据：', args.grammar_data)
        elif args.data:
            args.data.parent.mkdir(parents=True, exist_ok=True)
            args.data.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
            OUTPUT.mkdir(parents=True, exist_ok=True)
            (OUTPUT / '当前文法.bnf').write_text(bnf, encoding='utf-8')
            (OUTPUT / '当前文法与约束.md').write_text(document, encoding='utf-8')
            print(payload['sheets'][0]['context'])
        else:
            for name, expected in [('当前文法.bnf', bnf), ('当前文法与约束.md', document)]:
                if (OUTPUT / name).read_text(encoding='utf-8') != expected:
                    raise ValueError(name + ' 需要重新核对并生成')
            count = check_workbook(TARGET, payload)
            print(f'语法资料一致：{len(payload["sheets"])} 张表，{count} 个单元格')
            grammar_count = check_workbook(GRAMMAR_TARGET, grammar_payload(payload))
            print(f'独立文法表一致：1 张表，{grammar_count} 个单元格')
    except (ValueError, OSError, KeyError, zipfile.BadZipFile, ET.ParseError) as error:
        parser.exit(1, f'语法资料检查失败：{error}\n')


if __name__ == '__main__':
    main()
