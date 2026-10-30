"""验证当前词法规则、Token 覆盖和规则竞争。"""
from __future__ import annotations
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = json.loads((ROOT / "src/词法分析/词法规则.json").read_text(encoding="utf-8"))
MACRO = re.compile(r"\{([A-Z][A-Z0-9_]*)\}")
def expand(expression: str, stack=()):
    def sub(match):
        name = match.group(1)
        if name not in SPEC["definitions"]: raise ValueError(name)
        if name in stack: raise ValueError(stack + (name,))
        return "(" + expand(SPEC["definitions"][name], stack + (name,)) + ")"
    return MACRO.sub(sub, expression)
RULES = [(rule, re.compile(expand(rule["regex"]).encode("ascii"))) for rule in SPEC["rules"]]
def scan(source):
    data=source.encode("utf-8"); tokens=[]; errors=[]; pos=0
    while pos<len(data):
        best=None
        for rule, pattern in RULES:
            m=pattern.match(data,pos)
            if m and (best is None or m.end()>best[1].end()): best=(rule,m)
        if best is None or best[1].end()==pos: raise AssertionError(pos)
        rule,m=best; text=m.group().decode("utf-8",errors="replace")
        if rule["action"]=="token": tokens.append((SPEC["keywords"].get(text,rule["token"]) if rule["token"]=="ID" else rule["token"],text))
        elif rule["action"]=="error": errors.append((rule["code"],text))
        pos=m.end()
    tokens.append((SPEC["eof_token"],"")); return tokens,errors
class CurrentLexer(unittest.TestCase):
    def test_rules_consume(self):
        self.assertEqual(len({r["name"] for r,_ in RULES}),len(RULES))
        for rule,regex in RULES: self.assertIsNone(regex.match(b""),rule["name"])
    def test_token_coverage(self):
        body=(ROOT/"include/minic/interface.hpp").read_text(encoding="utf-8").split("enum class TokenType {",1)[1].split("};",1)[0]
        expected=set(re.findall(r"\b[A-Z][A-Z0-9_]*\b",re.sub(r"//[^\n]*","",body)))
        emitted={r["token"] for r,_ in RULES if r["action"]=="token"}|set(SPEC["keywords"].values())|{SPEC["eof_token"]}
        self.assertEqual(expected,emitted)
    def test_keywords_and_boundaries(self):
        self.assertEqual(len(SPEC["keywords"]),12)
        for word,kind in SPEC["keywords"].items():
            self.assertEqual(scan(word)[0][0],(kind,word)); self.assertEqual(scan(word+"x")[0][0],("ID",word+"x"))
    def test_literals_and_operators(self):
        for text,kind in [("0","INT_LITERAL"),("077","INT_LITERAL"),("0xff","INT_LITERAL"),("1.5","FLOAT_LITERAL"),(".5","FLOAT_LITERAL"),("\"x\"","STRING_LITERAL"),("'a'","CHAR_LITERAL")]:
            self.assertEqual(scan(text)[0][0],(kind,text))
        self.assertEqual(scan("a!=1 && b>=2;.")[0][0:7],[('ID','a'),('NE','!='),('INT_LITERAL','1'),('AND','&&'),('ID','b'),('GE','>='),('INT_LITERAL','2')])
    def test_invalid_and_comments(self):
        self.assertEqual(scan("08")[1][0][0],"LEX_INVALID_NUMBER")
        self.assertEqual(scan("@int")[1],[('LEX_INVALID_CHAR','@')])
        self.assertEqual(scan("/*x*/int x;")[0],[("KW_INT","int"),("ID","x"),("SEMI",";"),("END_OF_FILE","")])
if __name__ == "__main__": unittest.main(verbosity=2)
