"""用独立小文法验证 LR(1) 展望符、空产生式和冲突检测。"""
import ast
import re

from generate_lr_tables import ACCEPT, build, token_names


def grammar(*rules):
    return [
        {'id': f'P{index:03}', 'lhs': lhs, 'rhs': rhs}
        for index, (lhs, rhs) in enumerate(rules, 1)
    ]


def recognizer(rows):
    header, _, _, _ = build(rows)
    terminals = token_names()
    names = list(dict.fromkeys(row['lhs'] for row in rows))

    def table(name):
        block = re.search(rf'{name}\[\d+\]\[\d+\] = \{{(.*?)\n\}};', header, re.S)[1]
        return ast.literal_eval('[' + block.replace('{', '[').replace('}', ']') + ']')

    actions, gotos = table('action_table'), table('goto_table')

    def accepts(tokens):
        states, pos = [0], 0
        tokens = tokens + ['END_OF_FILE']
        for _ in range(1000):
            action = actions[states[-1]][terminals.index(tokens[pos])]
            if action == ACCEPT:
                return pos == len(tokens) - 1
            if action == 0:
                return False
            if action > 0:
                states.append(action - 1)
                pos += 1
            else:
                row = rows[-action - 1]
                if row['rhs']:
                    del states[-len(row['rhs']):]
                target = gotos[states[-1]][names.index(row['lhs'])]
                assert target >= 0, '归约后必须存在 GOTO'
                states.append(target)
        raise AssertionError('分析循环未结束')

    return accepts


def main():
    # 该文法是 LR(1)，却不是 SLR(1)：不能用 FOLLOW 代替项目的展望符。
    accepts = recognizer(grammar(
        ('程序', ['左', 'ASSIGN', '右']),
        ('程序', ['右']),
        ('左', ['STAR', '右']),
        ('左', ['ID']),
        ('右', ['左']),
    ))
    for tokens in (['ID'], ['ID', 'ASSIGN', 'ID'], ['STAR', 'ID', 'ASSIGN', 'STAR', 'ID']):
        assert accepts(tokens)
    for tokens in ([], ['ASSIGN', 'ID'], ['ID', 'ASSIGN'], ['ID', 'ID']):
        assert not accepts(tokens)

    # 该文法是规范 LR(1)，却不是 LALR(1)：不能直接合并相同 LR(0) 核心。
    accepts = recognizer(grammar(
        ('程序', ['PLUS', '甲', 'STAR']),
        ('程序', ['MINUS', '甲', 'SLASH']),
        ('程序', ['PLUS', '乙', 'SLASH']),
        ('程序', ['MINUS', '乙', 'STAR']),
        ('甲', ['ID']),
        ('乙', ['ID']),
    ))
    for tokens in (['PLUS', 'ID', 'STAR'], ['MINUS', 'ID', 'SLASH'],
                   ['PLUS', 'ID', 'SLASH'], ['MINUS', 'ID', 'STAR']):
        assert accepts(tokens)
    assert not accepts(['PLUS', 'ID', 'ID'])

    accepts = recognizer(grammar(
        ('程序', ['空', 'ID']),
        ('空', []),
    ))
    assert accepts(['ID'])
    assert not accepts([])

    for rows in (
        grammar(('程序', ['程序', 'PLUS', '程序']), ('程序', ['ID'])),
        grammar(('程序', ['甲']), ('程序', ['乙']), ('甲', ['ID']), ('乙', ['ID'])),
    ):
        try:
            build(rows)
        except ValueError as error:
            assert '冲突' in str(error)
        else:
            raise AssertionError('冲突必须报告，不能覆盖表项')
    print('LR(1) 验证通过：独立语言样例、展望符、空产生式及两类冲突')


if __name__ == '__main__':
    main()
