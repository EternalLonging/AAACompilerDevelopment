"""验证最终词法正则的接受范围、规则竞争和 Token 枚举覆盖。

这里只用 Python 正则验证规则，不代替将来的 C++ DFA 扫描器，
也不负责三字符组、续行、宏展开或 #include 上下文处理。
"""
from __future__ import annotations

import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = json.loads((ROOT / "lexer" / "patterns.json").read_text(encoding="utf-8"))
MACRO = re.compile(r"\{([A-Z][A-Z0-9_]*)\}")


def expand(expression: str, stack: tuple[str, ...] = ()) -> str:
    """把 {NAME} 宏展开成括号包围的普通正则，检测未定义引用和循环。"""
    def substitute(match: re.Match[str]) -> str:
        name = match.group(1)
        if name not in SPEC["definitions"]:
            raise ValueError(f"未定义正则宏：{name}")
        if name in stack:
            raise ValueError(f"正则宏循环引用：{stack + (name,)}")
        return "(" + expand(SPEC["definitions"][name], stack + (name,)) + ")"
    return MACRO.sub(substitute, expression)


# 使用字节模式，避免 Python 的 Unicode 字符计数与项目 UTF-8 字节 DFA 混淆。
RULES = [(rule, re.compile(expand(rule["regex"]).encode("ascii")))
         for rule in SPEC["rules"]]


def scan(source: str) -> tuple[list[tuple[str, str]], list[tuple[str, str]],
                               list[tuple[int, int, int]]]:
    """测试用的规则竞争器：最大长度优先，同长按文件顺序，最后追加 EOF。"""
    data = source.encode("utf-8")
    tokens: list[tuple[str, str]] = []
    errors: list[tuple[str, str]] = []
    locations: list[tuple[int, int, int]] = []
    position, line, col = 0, 1, 1
    while position < len(data):
        best = None
        for rule, pattern in RULES:
            matched = pattern.match(data, position)
            if matched and (best is None or matched.end() > best[1].end()):
                best = (rule, matched)
        if best is None or best[1].end() == position:
            raise AssertionError(f"规则未覆盖字节 {position}")
        rule, matched = best
        lexeme_bytes = matched.group()
        lexeme = lexeme_bytes.decode("utf-8", errors="replace")
        if rule["action"] == "token":
            token = rule["token"]
            if token == "ID":
                token = SPEC["keywords"].get(lexeme, token)
            tokens.append((token, lexeme))
            locations.append((line, col, position))
        elif rule["action"] == "error":
            errors.append((rule["code"], lexeme))
        # 被跳过的空白和注释同样更新行列，CRLF 占两字节但只换一次行。
        index = 0
        while index < len(lexeme_bytes):
            byte = lexeme_bytes[index]
            if byte == 13:
                if index + 1 < len(lexeme_bytes) and lexeme_bytes[index + 1] == 10:
                    index += 1
                line, col = line + 1, 1
            elif byte == 10:
                line, col = line + 1, 1
            else:
                col += 1
            index += 1
        position = matched.end()
    tokens.append((SPEC["eof_token"], ""))
    locations.append((line, col, position))
    return tokens, errors, locations


class FinalLexerPatterns(unittest.TestCase):
    def assert_single(self, source: str, token: str) -> None:
        tokens, errors, _ = scan(source)
        self.assertEqual(errors, [], source)
        self.assertEqual(tokens, [(token, source), ("END_OF_FILE", "")], source)

    def test_every_rule_consumes_input(self) -> None:
        self.assertEqual(len({r["name"] for r, _ in RULES}), len(RULES))
        for rule, regex in RULES:
            with self.subTest(rule=rule["name"]):
                self.assertIsNone(regex.match(b""))

    def test_empty_input_and_invalid_byte(self) -> None:
        tokens, errors, locations = scan("")
        self.assertEqual(tokens, [("END_OF_FILE", "")])
        self.assertEqual(errors, [])
        self.assertEqual(locations, [(1, 1, 0)])
        tokens, errors, _ = scan("@int")
        self.assertEqual(errors, [("LEX_INVALID_CHAR", "@")])
        self.assertEqual(tokens, [("KW_INT", "int"), ("END_OF_FILE", "")])

    def test_exact_token_enum_coverage(self) -> None:
        header = (ROOT / "include" / "minic" / "interface.hpp").read_text(encoding="utf-8")
        body = header.split("enum class TokenType {", 1)[1].split("};", 1)[0]
        body = re.sub(r"//[^\n]*", "", body)
        expected = set(re.findall(r"\b[A-Z][A-Z0-9_]*\b", body))
        emitted = {r["token"] for r, _ in RULES if r["action"] == "token"}
        emitted.update(SPEC["keywords"].values())
        emitted.add(SPEC["eof_token"])
        self.assertEqual(emitted, expected)

    def test_all_32_keywords_and_boundaries(self) -> None:
        self.assertEqual(len(SPEC["keywords"]), 32)
        for word, kind in SPEC["keywords"].items():
            with self.subTest(keyword=word):
                self.assert_single(word, kind)
                self.assert_single(word + "x", "ID")
        for identifier in ["main", "printf", "scanf", "include", "_x", "a1", "int32"]:
            self.assert_single(identifier, "ID")

    def test_all_fixed_punctuators(self) -> None:
        for rule, _ in RULES:
            if "literal" in rule:
                with self.subTest(symbol=rule["literal"]):
                    self.assert_single(rule["literal"], rule["token"])

    def test_final_integer_forms(self) -> None:
        for number in ["0", "00", "123", "077", "0xff", "0XAB",
                       "1u", "1L", "1UL", "1LU", "1uL", "1lU",
                       "077uL", "0xFFUL", "18446744073709551615UL"]:
            with self.subTest(number=number):
                self.assert_single(number, "INT_LITERAL")

    def test_final_float_forms(self) -> None:
        for number in ["0.0", "1.", ".5", "3.14159", "1e3", "1E-3",
                       "1.e+2", ".5e-2", "09.0", "1.0f", "1e2F", "2.5L"]:
            with self.subTest(number=number):
                self.assert_single(number, "FLOAT_LITERAL")

    def test_invalid_numbers_are_one_error(self) -> None:
        for number in ["08", "09", "0x", "0xG", "1e", "1e+", "1.2.3",
                       "12abc", "1f", "1UU", "1LL", "0b101", "0x1.fp3",
                       "0x1e+2", "0x1p-2"]:
            with self.subTest(number=number):
                tokens, errors, _ = scan(number)
                self.assertEqual(tokens, [("END_OF_FILE", "")])
                self.assertEqual(errors, [("LEX_INVALID_NUMBER", number)])

    def test_signs_are_operators(self) -> None:
        tokens, errors, _ = scan("-12 + .5 - 1e-2")
        self.assertEqual(errors, [])
        self.assertEqual([t[0] for t in tokens],
                         ["MINUS", "INT_LITERAL", "PLUS", "FLOAT_LITERAL",
                          "MINUS", "FLOAT_LITERAL", "END_OF_FILE"])
        self.assert_single("0x1e", "INT_LITERAL")
        tokens, errors, _ = scan("0x1e + 2")
        self.assertEqual(errors, [])
        self.assertEqual([t[0] for t in tokens],
                         ["INT_LITERAL", "PLUS", "INT_LITERAL", "END_OF_FILE"])

    def test_strings_and_character_escapes(self) -> None:
        strings = ['""', '"中文"', 'L"wide"', r'"a\n\t\\\""',
                   r'"\123\x41"', r'"\a\b\f\r\v\?"', r'"\0"', r'"\1234"']
        chars = ["'a'", "L'a'", "'ab'", "'中'", r"'\n'", r"'\''",
                 r"'\\'", r"'\123'", r"'\x41'"]
        for text in strings:
            with self.subTest(literal=text):
                self.assert_single(text, "STRING_LITERAL")
        for text in chars:
            with self.subTest(literal=text):
                self.assert_single(text, "CHAR_LITERAL")

    def test_closed_invalid_literals(self) -> None:
        for source, code in [(r'"\q"', "LEX_INVALID_STRING"),
                             (r'"\x"', "LEX_INVALID_STRING"),
                             (r'"\u0041"', "LEX_INVALID_STRING"),
                             ("''", "LEX_INVALID_CHAR_LITERAL"),
                             (r"'\8'", "LEX_INVALID_CHAR_LITERAL"),
                             (r"L'\q'", "LEX_INVALID_CHAR_LITERAL")]:
            with self.subTest(literal=source):
                tokens, errors, _ = scan(source)
                self.assertEqual(tokens, [("END_OF_FILE", "")])
                self.assertEqual(errors, [(code, source)])

    def test_unterminated_literals_and_recovery(self) -> None:
        cases = [('"abc', "LEX_UNTERMINATED_STRING"),
                 ('"abc\\', "LEX_UNTERMINATED_STRING"),
                 ('L"abc', "LEX_UNTERMINATED_STRING"),
                 ("'a", "LEX_UNTERMINATED_CHAR"),
                 ("L'a", "LEX_UNTERMINATED_CHAR")]
        for source, code in cases:
            with self.subTest(literal=source):
                tokens, errors, _ = scan(source)
                self.assertEqual(tokens, [("END_OF_FILE", "")])
                self.assertEqual(errors, [(code, source)])
        tokens, errors, locations = scan('"bad\nint x;')
        self.assertEqual(errors, [("LEX_UNTERMINATED_STRING", '"bad')])
        self.assertEqual(tokens[0], ("KW_INT", "int"))
        self.assertEqual(locations[0], (2, 1, 5))

    def test_comments_stop_at_first_closer(self) -> None:
        for comment in ["/**/", "/***/", "/*a**b*/", "/*\r\n中文\n*/", "//中文"]:
            with self.subTest(comment=comment):
                tokens, errors, _ = scan(comment)
                self.assertEqual(tokens, [("END_OF_FILE", "")])
                self.assertEqual(errors, [])
        tokens, errors, _ = scan("/*first*/int/**/x;")
        self.assertEqual(errors, [])
        self.assertEqual(tokens, [("KW_INT", "int"), ("ID", "x"),
                                  ("SEMI", ";"), ("END_OF_FILE", "")])
        tokens, errors, _ = scan("/*outer /*inner*/ x")
        self.assertEqual(errors, [])
        self.assertEqual(tokens[0], ("ID", "x"))  # C 注释不嵌套。

    def test_unterminated_comment_commits_to_comment(self) -> None:
        for comment in ["/*", "/*abc", "/*abc*", "/*abc**", "/*\nint x;"]:
            with self.subTest(comment=comment):
                tokens, errors, _ = scan(comment)
                self.assertEqual(tokens, [("END_OF_FILE", "")])
                self.assertEqual(errors, [("LEX_UNTERMINATED_COMMENT", comment)])

    def test_longest_match_and_positions(self) -> None:
        tokens, errors, _ = scan("a<<=1; b!=2 && c>=3; p->x; ... ##")
        self.assertEqual(errors, [])
        self.assertEqual([t[0] for t in tokens],
                         ["ID", "SHIFT_LEFT_ASSIGN", "INT_LITERAL", "SEMI",
                          "ID", "NE", "INT_LITERAL", "AND", "ID", "GE",
                          "INT_LITERAL", "SEMI", "ID", "ARROW", "ID", "SEMI",
                          "ELLIPSIS", "HASH_HASH", "END_OF_FILE"])
        tokens, errors, locations = scan("a/*x\r\ny*/\tb\r\nc")
        self.assertEqual(errors, [])
        self.assertEqual(locations, [(1, 1, 0), (2, 5, 10), (3, 1, 13), (3, 2, 14)])
        tokens, errors, locations = scan('"中文" x')
        self.assertEqual(errors, [])
        self.assertEqual(locations[1], (1, 10, 9))  # UTF-8 字节列。

    def test_header_context_patterns(self) -> None:
        for rule, sample in zip(SPEC["preprocessing"]["header_rules"],
                                ["<stdio.h>", r'"dir\header.h"']):
            regex = re.compile(expand(rule["regex"]).encode("ascii"))
            self.assertIsNotNone(regex.fullmatch(sample.encode("utf-8")))
            self.assertIsNone(regex.fullmatch(b""))
        # 头文件规则不加入普通单词 DFA，避免吞掉普通比较表达式。
        tokens, errors, _ = scan("a<b>c")
        self.assertEqual(errors, [])
        self.assertEqual([t[0] for t in tokens],
                         ["ID", "LT", "ID", "GT", "ID", "END_OF_FILE"])

    def test_circle_and_scanf(self) -> None:
        source = ('int main(){float r;float c;r=1;c=2*3.14159*r;'
                  'printf("c = %f\\n",c);return 0;}')
        tokens, errors, _ = scan(source)
        self.assertEqual(errors, [])
        self.assertEqual(len(tokens), 35)  # 实际按规则计数，包含 EOF。
        tokens, errors, _ = scan('scanf("%f", &r);')
        self.assertEqual(errors, [])
        self.assertIn(("AMP", "&"), tokens)


if __name__ == "__main__":
    print(f"验证 {len(RULES)} 条普通词法规则、"
          f"{len(SPEC['keywords'])} 个关键字和 2 条头文件上下文规则。",
          flush=True)
    unittest.main(verbosity=2)
