"""Derive output-aware minimized byte DFAs from the pinned project regex rules."""
from __future__ import annotations
import collections
import importlib.util
import io
import json
from pathlib import Path
import random
import re
import unittest

ROOT = Path(__file__).resolve().parent
SPEC = json.loads((ROOT / '项目快照/lexer/patterns.json').read_text(encoding='utf-8'))
SHA = (ROOT / 'commit.txt').read_text(encoding='utf-8').strip()
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

ordinary = derive(SPEC['rules'])
header = derive(SPEC['preprocessing']['header_rules'])

def scan_dfa(source):
    data = source.encode('utf-8')
    tokens, errors, locations = [], [], []
    pos, line, col = 0, 1, 1
    while pos < len(data):
        state, p, last = 0, pos, None
        while p < len(data):
            target = ordinary['full'][state][data[p]]
            if target < 0:
                break
            state, p = target, p+1
            rule_id = ordinary['accept'][state]
            if rule_id >= 0:
                last = (rule_id, p)
        assert last is not None
        rule_id, end = last
        rule = SPEC['rules'][rule_id]
        raw = data[pos:end]
        text = raw.decode('utf-8', errors='replace')
        if rule['action'] == 'token':
            kind = rule['token']
            if kind == 'ID':
                kind = SPEC['keywords'].get(text, kind)
            tokens.append((kind, text))
            locations.append((line, col, pos))
        elif rule['action'] == 'error':
            errors.append((rule['code'], text))
        i = 0
        while i < len(raw):
            if raw[i] == 13:
                if i+1 < len(raw) and raw[i+1] == 10:
                    i += 1
                line, col = line+1, 1
            elif raw[i] == 10:
                line, col = line+1, 1
            else:
                col += 1
            i += 1
        pos = end
    tokens.append((SPEC['eof_token'], ''))
    locations.append((line, col, pos))
    return tokens, errors, locations

module_path = ROOT / '项目快照/tools/check_lexer_patterns.py'
spec = importlib.util.spec_from_file_location('project_checks', module_path)
checks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checks)
original_scan = checks.scan
checks.scan = scan_dfa
suite = unittest.defaultTestLoader.loadTestsFromTestCase(checks.FinalLexerPatterns)
stream = io.StringIO()
result = unittest.TextTestRunner(stream=stream, verbosity=2).run(suite)
if not result.wasSuccessful():
    raise AssertionError(stream.getvalue())
# Independently compare DFA scanning with the project's byte-regex reference scanner.
rng = random.Random(20261008)
alphabet = 'abcintLeEpPxX0123789uUfFlL_+-*/=<>!&|^~(){}[];,.?:# \\"\'\t\r\n中文@'
for i in range(3000):
    source = ''.join(rng.choice(alphabet) for _ in range(rng.randrange(0, 85)))
    assert scan_dfa(source) == original_scan(source), repr(source)
accepted = set(ordinary['accept']) - {-1}
assert accepted == set(range(len(SPEC['rules']))), ('unreachable rules', set(range(len(SPEC['rules']))) - accepted)

examples = ['int32', 'int', '077UL', '0xFF', '1.e+2', '.5', '08', '1e+', '12abc',
            '"中文"', r'"\x41"', r'"\q"', "'ab'", "''", '/*abc', '/**/', '/*a*/int',
            'a<<=1', '...', '##', 'a<b>c', '"bad\nint x;', '0x1e+2']
example_rows = []
for source in examples:
    tokens, errors, _ = scan_dfa(source)
    example_rows.append({'source':source.replace('\n', '\\n'),
                         'tokens':' '.join(t[0] for t in tokens if t[0] != SPEC['eof_token']) or '无',
                         'errors':' '.join(e[0] for e in errors) or '无'})
data = {'commit':SHA, 'rules':SPEC['rules'], 'keywords':SPEC['keywords'],
        'definitions':SPEC['definitions'], 'header_rules':SPEC['preprocessing']['header_rules'],
        'ordinary':ordinary, 'header':header, 'examples':example_rows,
        'verification':{'project_test_groups':result.testsRun, 'random_sources':3000,
                        'all_rule_acceptance':len(accepted), 'byte_classes_complete':True}}
for dfa in (ordinary, header):
    del dfa['full']
(ROOT / 'dfa_data.json').write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
(ROOT / 'validation.txt').write_text(stream.getvalue() + '\n3000 random sources agree with project regex scanner.\n'
    + f'All {len(accepted)} rules reachable. All 256 byte columns preserved.\n'
    + f'Pinned commit: {SHA}\n', encoding='utf-8')
print(json.dumps({'ordinary_states':ordinary['state_count'], 'ordinary_classes':len(ordinary['classes']),
                 'header_states':header['state_count'], 'header_classes':len(header['classes']),
                 'verification':data['verification']}, ensure_ascii=False))
