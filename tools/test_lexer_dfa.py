"""将真实 C++ 扫描器与逐规则 Python 字节正则进行独立对照。"""
from pathlib import Path
import random
import re
import subprocess
import sys

from check_lexer_patterns import RULES, SPEC

ROOT = Path(__file__).resolve().parents[1]
enum = re.search(r'enum class TokenType\s*\{([^}]+)',
                 (ROOT / 'include/minic/interface.hpp').read_text('utf-8')).group(1)
KINDS = {name.strip(): i for i, name in enumerate(enum.split(','))}


def expected(data):
    tokens, errors = [], []
    pos, line, col = 0, 1, 1
    previous_cr = False
    while pos < len(data):
        begin = (line, col, pos)
        best = None
        for rule, pattern in RULES:
            matched = pattern.match(data, pos)
            if matched and (best is None or matched.end() > best[1].end()):
                best = rule, matched
        assert best is not None
        rule, match = best
        raw = match.group()
        while pos < match.end():
            byte = data[pos]
            pos += 1
            if byte == 13:
                line, col = line + 1, 1
            elif byte == 10:
                if not previous_cr:
                    line += 1
                col = 1
            else:
                col += 1
            previous_cr = byte == 13
        location = ','.join(map(str, (*begin, line, col, pos)))
        if rule['action'] == 'token':
            kind = rule['token']
            if kind == 'ID':
                kind = SPEC['keywords'].get(raw.decode('ascii'), kind)
            tokens.append(f'T{KINDS[kind]},{location},{raw.hex()}|')
        elif rule['action'] == 'error':
            errors.append(f'E{rule["code"]},{location}|')
            if len(errors) == 20:
                break
    eof = ','.join(map(str, (line, col, pos, line, col, pos)))
    tokens.append(f'T{KINDS["END_OF_FILE"]},{eof},|')
    return ''.join(tokens + errors)


def main():
    samples = [b'', bytes(range(256))]
    samples += [bytes([byte]) for byte in range(256)]
    samples += [name.encode() for name in SPEC['keywords']]
    samples += [name.encode() + b'32' for name in SPEC['keywords']]
    samples += [r['literal'].encode() for r in SPEC['rules'] if 'literal' in r]
    atoms = [b'077UL', b'0xff', b'0x1e+2', b'1e+', b'1UU', b'08', b'1.0f', b'.5e-2',
             b'"\\x41"', b'"\\q"', b'L"wide"', b"'ab'", b"''", b'"bad\nint x;',
             b'/*a*/int/**/x;', b'/*unfinished***', b'//comment\r\n', b'a<<=1;', b'... ##',
             '"中文"'.encode(), b'int32', b'\x00', b'\r\n', b'\r', b'\n', b'@']
    samples += atoms
    rng = random.Random(20261009)
    for i in range(3000):
        if i % 3 == 0:
            samples.append(bytes(rng.randrange(256) for _ in range(rng.randrange(80))))
        elif i % 3 == 1:
            samples.append(b' '.join(rng.choice(atoms) for _ in range(rng.randrange(12))))
        else:
            alphabet = b'abcL012789xEe+-*/\\\'"()[]{};=<>!&|?., \r\n\t'
            samples.append(bytes(rng.choice(alphabet) for _ in range(rng.randrange(100))))
    run = subprocess.run([sys.argv[1]], input=''.join(s.hex() + '\n' for s in samples),
                         text=True, encoding='ascii', capture_output=True, check=True)
    actual = run.stdout.splitlines()
    assert len(actual) == len(samples)
    for i, (sample, output) in enumerate(zip(samples, actual)):
        assert output == expected(sample), (i, sample, output, expected(sample))
    print(f'lexer differential: {len(samples)} cases passed (tokens, spelling, diagnostics, begin/end locations)')


if __name__ == '__main__':
    main()
