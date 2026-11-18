"""从 MiniC 文法生成规范 LR(1) ACTION/GOTO 表。"""
from __future__ import annotations

import argparse
from collections import defaultdict, deque
import json
from pathlib import Path
import re

import generate_parser_tables as ll

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'src/语法分析/lr_table.hpp'
GRAMMAR_OUTPUT = ROOT / '实验设计/语法分析/语法表格/LR文法.bnf'
ACCEPT = 32767


def productions():
    """保留语言范围，将表达式改为可直接建 AST 的 LR 产生式。"""
    rows = []
    replaced = {'逻辑或式', '逻辑或续', '逻辑与式', '逻辑与续', '相等式', '相等续',
                '关系式', '关系续', '加法式', '加法续', '乘法式', '乘法续',
                '表达式', '赋值续', '后缀式', '表达后缀序列', '表达后缀'}
    for row in ll.productions():
        if row['lhs'] not in replaced:
            rows.append(dict(row))

    left_recursive = [
        ('表达式', ['逻辑或式', 'ASSIGN', '表达式']),
        ('表达式', ['逻辑或式']),
        ('逻辑或式', ['逻辑或式', 'OR', '逻辑与式']),
        ('逻辑或式', ['逻辑与式']),
        ('逻辑与式', ['逻辑与式', 'AND', '相等式']),
        ('逻辑与式', ['相等式']),
        ('相等式', ['相等式', 'EQ', '关系式']),
        ('相等式', ['相等式', 'NE', '关系式']),
        ('相等式', ['关系式']),
        ('关系式', ['关系式', 'LT', '加法式']),
        ('关系式', ['关系式', 'LE', '加法式']),
        ('关系式', ['关系式', 'GT', '加法式']),
        ('关系式', ['关系式', 'GE', '加法式']),
        ('关系式', ['加法式']),
        ('加法式', ['加法式', 'PLUS', '乘法式']),
        ('加法式', ['加法式', 'MINUS', '乘法式']),
        ('加法式', ['乘法式']),
        ('乘法式', ['乘法式', 'STAR', '一元式']),
        ('乘法式', ['乘法式', 'SLASH', '一元式']),
        ('乘法式', ['乘法式', 'PERCENT', '一元式']),
        ('乘法式', ['一元式']),
        ('后缀式', ['后缀式', 'LPAREN', '实参可选', 'RPAREN']),
        ('后缀式', ['后缀式', 'LBRACKET', '表达式', 'RBRACKET']),
        ('后缀式', ['后缀式', 'DOT', 'ID']),
        ('后缀式', ['主式']),
    ]
    for lhs, rhs in left_recursive:
        rows.append({'lhs': lhs, 'rhs': rhs, 'function': 'expression'})
    for index, row in enumerate(rows, 1):
        row['id'] = f'P{index:03}'
    return rows


def token_names():
    interface = (ROOT / 'include/minic/interface.hpp').read_text(encoding='utf-8')
    enum = re.search(r'enum class TokenType\s*\{(.*?)\};', interface, re.S)[1]
    return re.findall(r'\b[A-Z][A-Z_]+\b', enum)


def build(rows=None):
    rows = productions() if rows is None else rows
    names = list(dict.fromkeys(row['lhs'] for row in rows))
    nonterminals = set(names)
    terminals = token_names()
    used = {symbol for row in rows for symbol in row['rhs'] if symbol not in nonterminals}
    unknown = used - set(terminals)
    if unknown:
        raise ValueError(f'文法含未定义终结符：{sorted(unknown)}')

    nullable = set()
    first = {name: set() for name in names}
    changed = True
    while changed:
        changed = False
        for row in rows:
            all_nullable = True
            for symbol in row['rhs']:
                values = first[symbol] if symbol in nonterminals else {symbol}
                size = len(first[row['lhs']])
                first[row['lhs']].update(values)
                changed |= size != len(first[row['lhs']])
                if symbol not in nullable:
                    all_nullable = False
                    break
            if all_nullable and row['lhs'] not in nullable:
                nullable.add(row['lhs'])
                changed = True

    def first_with_lookahead(sequence, lookahead):
        result = set()
        for symbol in sequence:
            result.update(first[symbol] if symbol in nonterminals else {symbol})
            if symbol not in nullable:
                return result
        result.add(lookahead)
        return result

    augmented = {'lhs': '$accept', 'rhs': ['程序']}
    all_rows = rows + [augmented]
    augmented_index = len(rows)
    by_lhs = defaultdict(list)
    for index, row in enumerate(rows):
        by_lhs[row['lhs']].append(index)

    def closure(items):
        result = set(items)
        pending = list(items)
        while pending:
            production, dot, lookahead = pending.pop()
            rhs = all_rows[production]['rhs']
            if dot == len(rhs) or rhs[dot] not in nonterminals:
                continue
            symbol = rhs[dot]
            lookaheads = first_with_lookahead(rhs[dot + 1:], lookahead)
            for child in by_lhs[symbol]:
                for next_lookahead in lookaheads:
                    item = (child, 0, next_lookahead)
                    if item not in result:
                        result.add(item)
                        pending.append(item)
        return frozenset(result)

    start = closure({(augmented_index, 0, 'END_OF_FILE')})
    states = [start]
    state_ids = {start: 0}
    transitions = {}
    queue = deque([0])
    symbol_order = {name: index for index, name in enumerate(terminals + names)}
    while queue:
        state_id = queue.popleft()
        symbols = {all_rows[p]['rhs'][dot]
                   for p, dot, _ in states[state_id]
                   if dot < len(all_rows[p]['rhs'])}
        for symbol in sorted(symbols, key=symbol_order.__getitem__):
            moved = {(p, dot + 1, lookahead)
                     for p, dot, lookahead in states[state_id]
                     if dot < len(all_rows[p]['rhs']) and all_rows[p]['rhs'][dot] == symbol}
            target = closure(moved)
            if target not in state_ids:
                state_ids[target] = len(states)
                states.append(target)
                queue.append(state_ids[target])
            transitions[state_id, symbol] = state_ids[target]

    actions = {}
    gotos = {}

    def add_action(state, terminal, action):
        key = (state, terminal)
        previous = actions.get(key)
        if previous is not None and previous != action:
            raise ValueError(f'LR(1) 冲突：状态 {state}，{terminal}：{previous} 与 {action}')
        actions[key] = action

    for state_id, items in enumerate(states):
        for production, dot, lookahead in items:
            row = all_rows[production]
            if dot < len(row['rhs']):
                symbol = row['rhs'][dot]
                target = transitions[state_id, symbol]
                if symbol in nonterminals:
                    gotos[state_id, symbol] = target
                else:
                    add_action(state_id, symbol, ('shift', target))
            elif production == augmented_index:
                add_action(state_id, 'END_OF_FILE', ('accept', 0))
            else:
                add_action(state_id, lookahead, ('reduce', production))

    if len(states) >= ACCEPT or len(rows) >= ACCEPT:
        raise ValueError('LR 状态或产生式数量超出运行表编码范围')
    symbol_ids = {terminal: index for index, terminal in enumerate(terminals)}
    symbol_ids.update({name: len(terminals) + index for index, name in enumerate(names)})
    grammar_lines = [
        '# MiniC 的规范 LR(1) 文法',
        '# 开始符号：程序；结束标记：END_OF_FILE；空串：ε',
        '# 左结合运算层采用左递归产生式；赋值仍按右结合。',
    ]
    for row in rows:
        rhs = ' '.join(json.dumps(ll.LITERALS[symbol], ensure_ascii=False)
                       if symbol in ll.LITERALS else symbol for symbol in row['rhs']) or 'ε'
        grammar_lines.append(f'{row["id"]}: {row["lhs"]} -> {rhs}')

    lines = [
        '// 自动生成：python tools/generate_lr_tables.py --runtime。',
        '#pragma once',
        '#include "minic/interface.hpp"',
        '',
        'namespace minic { namespace lr {',
        f'inline constexpr int terminal_count = {len(terminals)};',
        f'inline constexpr int nonterminal_count = {len(names)};',
        f'inline constexpr int state_count = {len(states)};',
        f'inline constexpr short accept_action = {ACCEPT};',
        'enum Nonterminal {',
    ]
    lines.extend(f'    {name} = {len(terminals) + index},' for index, name in enumerate(names))
    lines.extend([
        '};',
        'struct Production {',
        '    int lhs; // 产生式左部的非终结符编号。',
        '    int length; // 右部符号个数，也是归约时的弹栈数量。',
        '};',
        'inline constexpr Production productions[] = {',
    ])
    for row in rows:
        lines.append(f'    {{{symbol_ids[row["lhs"]]}, {len(row["rhs"])}}}, // {row["id"]}: {row["lhs"]} -> {" ".join(row["rhs"]) or "ε"}')
    lines.extend([
        '};',
        '// 0 表示错误；正数为移进状态加一；负数为归约产生式下标加一；accept_action 表示接受。',
        f'inline constexpr short action_table[{len(states)}][{len(terminals)}] = {{',
    ])
    for state_id in range(len(states)):
        cells = [0] * len(terminals)
        for (state, terminal), (kind, value) in actions.items():
            if state != state_id:
                continue
            index = symbol_ids[terminal]
            cells[index] = value + 1 if kind == 'shift' else -(value + 1) if kind == 'reduce' else ACCEPT
        lines.append('    {' + ', '.join(map(str, cells)) + '},')
    lines.extend(['};', f'inline constexpr short goto_table[{len(states)}][{len(names)}] = {{'])
    for state_id in range(len(states)):
        cells = [gotos.get((state_id, name), -1) for name in names]
        lines.append('    {' + ', '.join(map(str, cells)) + '},')
    lines.extend(['};', 'inline constexpr const char* names[] = {'])
    lines.extend('    ' + json.dumps(ll.LITERALS.get(name, name), ensure_ascii=False) + ','
                 for name in terminals)
    lines.extend('    ' + json.dumps(name, ensure_ascii=False) + ',' for name in names)
    lines.extend(['};'])
    for index, terminal in enumerate(terminals):
        lines.append(f'static_assert(static_cast<int>(TokenType::{terminal}) == {index}, "请重新生成 LR 表");')
    lines.extend(['} }', ''])
    return '\n'.join(lines), '\n'.join(grammar_lines) + '\n', len(states), len(actions)


def main():
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--runtime', action='store_true', help='生成 C++ LR 运行表')
    mode.add_argument('--check', action='store_true', help='检查运行表是否为最新')
    args = parser.parse_args()
    content, grammar, states, action_count = build()
    if args.runtime:
        OUTPUT.write_text(content, encoding='utf-8', newline='\n')
        GRAMMAR_OUTPUT.write_text(grammar, encoding='utf-8', newline='\n')
    elif args.check:
        if (not OUTPUT.exists() or OUTPUT.read_text(encoding='utf-8') != content or
                not GRAMMAR_OUTPUT.exists() or GRAMMAR_OUTPUT.read_text(encoding='utf-8') != grammar):
            raise SystemExit('LR 运行表与生成器不一致')
    else:
        print(f'规范 LR(1)：{states} 个状态，{action_count} 个 ACTION 项')
        return
    print(f'规范 LR(1)：{states} 个状态，{action_count} 个 ACTION 项')


if __name__ == '__main__':
    main()
