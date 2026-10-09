"""从当前项目正则构造保留输出动作的最小化字节 DFA。"""
from __future__ import annotations
import collections
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SPEC = json.loads((ROOT / 'lexer/patterns.json').read_text(encoding='utf-8'))
BYTESET = frozenset(range(256))

def expand(text, stack=()):
    def sub(m):
        name = m[1]
        if name in stack:
            raise ValueError('recursive macro')
        return '(' + expand(SPEC['definitions'][name], stack + (name,)) + ')'
    return re.sub(r'\{([A-Z][A-Z0-9_]*)\}', sub, text)

class Parser:
    def __init__(self, text):
        self.text, self.i = text, 0
    def peek(self):
        return self.text[self.i] if self.i < len(self.text) else ''
    def take(self):
        c = self.peek()
        if not c:
            raise ValueError('unexpected end')
        self.i += 1
        return c
    def escaped(self):
        c = self.take()
        if c == 'x':
            n = int(self.text[self.i:self.i+2], 16)
            self.i += 2
            return n
        return {'n':10, 'r':13, 't':9, 'v':11, 'f':12}.get(c, ord(c))
    def class_byte(self):
        c = self.take()
        return self.escaped() if c == '\\' else ord(c)
    def atom(self):
        c = self.take()
        if c == '(':
            node = self.alt()
            assert self.take() == ')'
            return node
        if c == '[':
            negate = self.peek() == '^'
            if negate:
                self.take()
            members = set()
            while self.peek() != ']':
                a = self.class_byte()
                if self.peek() == '-' and self.text[self.i+1] != ']':
                    self.take()
                    b = self.class_byte()
                    assert a <= b
                    members.update(range(a, b+1))
                else:
                    members.add(a)
            self.take()
            return ('char', BYTESET - members if negate else frozenset(members))
        if c == '\\':
            return ('char', frozenset([self.escaped()]))
        assert c not in '*+?|)'
        return ('char', frozenset([ord(c)]))
    def concat(self):
        nodes = []
        while self.peek() and self.peek() not in ')|':
            node = self.atom()
            if self.peek() and self.peek() in '*+?':
                node = (self.take(), node)
            nodes.append(node)
        return ('cat', nodes)
    def alt(self):
        nodes = [self.concat()]
        while self.peek() == '|':
            self.take()
            nodes.append(self.concat())
        return ('alt', nodes)
    def parse(self):
        ast = self.alt()
        assert self.i == len(self.text)
        return ast

class NFA:
    def __init__(self):
        self.eps, self.edges, self.accept = [], [], {}
    def new(self):
        i = len(self.eps)
        self.eps.append([])
        self.edges.append([])
        return i
    def build(self, ast):
        op, data = ast
        s, e = self.new(), self.new()
        if op == 'char':
            self.edges[s].append((data, e))
        elif op == 'cat':
            prev = s
            for node in data:
                a, b = self.build(node)
                self.eps[prev].append(a)
                prev = b
            self.eps[prev].append(e)
        elif op == 'alt':
            for node in data:
                a, b = self.build(node)
                self.eps[s].append(a)
                self.eps[b].append(e)
        else:
            a, b = self.build(data)
            self.eps[s].append(a)
            self.eps[b].append(e)
            if op in '*?':
                self.eps[s].append(e)
            if op in '*+':
                self.eps[b].append(a)
        return s, e
    def closure(self, states):
        seen, todo = set(states), list(states)
        while todo:
            for nxt in self.eps[todo.pop()]:
                if nxt not in seen:
                    seen.add(nxt)
                    todo.append(nxt)
        return frozenset(seen)

def derive(rules):
    nfa = NFA()
    start = nfa.new()
    for i, rule in enumerate(rules):
        a, b = nfa.build(Parser(expand(rule['regex'])).parse())
        nfa.eps[start].append(a)
        nfa.accept[b] = i
    startset = nfa.closure([start])
    assert not (set(startset) & set(nfa.accept))
    subsets, index, rows, accepting = [startset], {startset:0}, [], []
    cursor = 0
    while cursor < len(subsets):
        subset = subsets[cursor]
        matched = [nfa.accept[s] for s in subset if s in nfa.accept]
        accepting.append(min(matched) if matched else -1)
        moves = [set() for _ in range(256)]
        for state in subset:
            for chars, target in nfa.edges[state]:
                for byte in chars:
                    moves[byte].add(target)
        row = []
        for targets in moves:
            if not targets:
                row.append(-1)
                continue
            closed = nfa.closure(targets)
            if closed not in index:
                index[closed] = len(subsets)
                subsets.append(closed)
            row.append(index[closed])
        rows.append(row)
        cursor += 1
    raw_count = len(rows)
    # Complete with an absorbing sink for correct output-preserving minimization.
    sink = len(rows)
    complete = [[v if v >= 0 else sink for v in row] for row in rows] + [[sink]*256]
    outputs = accepting + [-1]
    def ids(keys):
        seen = {}
        return [seen.setdefault(key, len(seen)) for key in keys]
    groups = ids(outputs)
    while True:
        updated = ids([(outputs[s], tuple(groups[t] for t in row)) for s, row in enumerate(complete)])
        if updated == groups:
            break
        groups = updated
    reps = {}
    for i, group in enumerate(groups):
        reps.setdefault(group, i)
    deadgroup = groups[sink]
    firstgroup = groups[0]
    reachable, newids, minimized, labels, witnesses = [firstgroup], {firstgroup:0}, [], [], [b'']
    cursor = 0
    while cursor < len(reachable):
        group = reachable[cursor]
        representative = reps[group]
        labels.append(outputs[representative])
        row = []
        for byte, nxt in enumerate(complete[representative]):
            nextgroup = groups[nxt]
            if nextgroup == deadgroup:
                row.append(-1)
            else:
                if nextgroup not in newids:
                    newids[nextgroup] = len(reachable)
                    reachable.append(nextgroup)
                    witnesses.append(witnesses[cursor] + bytes([byte]))
                row.append(newids[nextgroup])
        minimized.append(row)
        cursor += 1
    # Verify all original states map to identical output/transition behavior.
    for i, row in enumerate(rows):
        mapped = newids[groups[i]]
        assert labels[mapped] == accepting[i]
        for byte, target in enumerate(row):
            expected = -1 if target < 0 or groups[target] == deadgroup else newids[groups[target]]
            assert minimized[mapped][byte] == expected
    grouped = collections.defaultdict(list)
    for byte in range(256):
        grouped[tuple(row[byte] for row in minimized)].append(byte)
    classes = sorted(grouped.values(), key=lambda members: members[0])
    compressed = [[row[members[0]] for members in classes] for row in minimized]
    assert sorted(b for members in classes for b in members) == list(range(256))
    for state, row in enumerate(compressed):
        for c, members in enumerate(classes):
            assert all(minimized[state][b] == row[c] for b in members)
    return {'matrix':compressed, 'full':minimized, 'accept':labels, 'classes':classes,
            'witnesses':[repr(w)[2:-1] for w in witnesses],
            'nfa_states':len(nfa.eps), 'unminimized_states':raw_count, 'state_count':len(minimized)}


def generate():
    dfa = derive(SPEC['rules'])
    classes = [0] * 256
    for index, members in enumerate(dfa['classes']):
        for byte in members:
            classes[byte] = index
    lines = ['// 由 tools/generate_lexer_dfa.py 生成；请修改 lexer/patterns.json 后重新生成。',
             '#pragma once', '#include "minic/interface.hpp"',
             'namespace minic::lexer_detail {',
             'enum class Action { Token, Skip, Error };',
             'struct Rule { Action action; TokenType token; const char* code; const char* message; };',
             'inline constexpr unsigned char byte_class[256] = {' + ','.join(map(str, classes)) + '};',
             'inline constexpr short transitions[][{}] = {{'.format(len(dfa['classes']))]
    for row in dfa['matrix']:
        lines.append('    {' + ','.join(map(str, row)) + '},')
    lines += ['};', 'inline constexpr short accepting[] = {' + ','.join(map(str, dfa['accept'])) + '};',
              'inline constexpr Rule rules[] = {']
    for rule in SPEC['rules']:
        action = rule['action'].capitalize()
        token = rule.get('token', 'END_OF_FILE')
        code = json.dumps(rule.get('code', ''), ensure_ascii=False)
        message = json.dumps(rule['description'], ensure_ascii=False)
        lines.append('    {Action::' + action + ',TokenType::' + token + ',' + code + ',' + message + '},')
    lines += ['};', 'struct Keyword { const char* spelling; TokenType token; };',
              'inline constexpr Keyword keywords[] = {']
    for spelling, token in SPEC['keywords'].items():
        lines.append('    {"' + spelling + '",TokenType::' + token + '},')
    lines += ['};', '}']
    return '\n'.join(lines) + '\n'

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description='从当前项目正则生成最小化 DFA')
    parser.add_argument('--check', action='store_true', help='检查已提交状态表是否需要重新生成')
    args = parser.parse_args()
    target = ROOT / 'src/lexer_dfa.hpp'
    expected = generate()
    if args.check:
        if not target.exists() or target.read_text('utf-8') != expected:
            raise SystemExit('DFA 状态表与规则不一致，请重新生成')
        print('DFA 状态表与当前规则一致')
    else:
        target.write_text(expected, 'utf-8')
        print('已生成', target)
