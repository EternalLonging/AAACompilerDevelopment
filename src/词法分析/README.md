# 词法分析

将源码扫描为 Token，保留位置和中文错误。

- `lexer.cpp`
- `lexer_dfa.hpp`
- `词法规则.json`

[开发说明](../../docs/词法分析/README.md) · [公共接口](../../include/minic/)

---

## 白话说明（给组员看）

### 这个模块做什么

输入一段源码字符串，输出「单词表 + 报错表」：

```
"int a; a = 1;"   ──lex()──▶   [ int ] [ a ] [ ; ] [ a ] [ = ] [ 1 ] [ ; ] [ EOF ]
                              每个单词都带：类型、原文、行号、列号
```

### 文件清单

| 文件 | 是什么 | 能不能手改 |
|---|---|---|
| `lexer.cpp` | 词法分析实现：驱动 DFA 扫描源码 | 可以改（一般不用改） |
| `lexer_dfa.hpp` | 由脚本生成的 DFA 表格（转移表、接受态表、规则表、关键字表） | 不要手改，会被覆盖 |
| `词法规则.json` | 词法规则的数据源（正则、关键字、错误规则） | 要改词法规则就改这里 |

### 对外接口

只有一个函数，声明在 `include/minic/lexer.hpp`：

```cpp
LexResult lex(const std::string& source, const std::string& filename = "<input>");
```

- `source`：源码全文。
- `filename`：只写进报错信息，用于定位。
- 返回 `LexResult`：`tokens` 是单词表（末尾固定是 `END_OF_FILE`），
  `diagnostics` 是词法阶段的报错表（带行号、列号）。

### Token 长什么样

每个 Token 记四件事：类型（`TokenType`）、原文（`lexeme`）、行号、列号。
例如 `int a;` 会切成：`KW_INT("int")`、`ID("a")`、`SEMI(";")`、`END_OF_FILE`。

### 怎么读懂生成的 DFA 表（`lexer_dfa.hpp`）

这个文件由 `tools/generate_lexer_dfa.py` 读 `词法规则.json` 生成，
所以**不给它写注释**（写了也会被下次生成覆盖）。它里面只有 5 张表：

| 名字 | 含义 |
|---|---|
| `byte_class[256]` | 把 256 种字节各自归到一个小类别号；DFA 只按"类别"转移，表能小很多 |
| `transitions[状态][类别]` | 状态转移表：当前状态 + 字节类别 → 下一个状态；`-1` 表示走不通 |
| `accepting[状态]` | 这个状态是不是"接受态"；是的话存对应规则编号，`-1` 表示不是 |
| `rules[规则号]` | 规则表：命中后是产出 Token、跳过（空白）、还是报错，以及对应的类型/错误信息 |
| `keywords[]` | 关键字表：扫出来是标识符时，再查这张表看它是不是关键字（如 `int`） |

一句话：`lexer.cpp` 负责"跑"这些表，`lexer_dfa.hpp` 只是"数据"。

### 怎么改词法规则

1. 编辑 `词法规则.json`（正则、关键字、错误规则都在这里）；
2. 运行 `python tools/generate_lexer_dfa.py` 重新生成 `lexer_dfa.hpp`；
3. 运行 `python tools/check_lexer_patterns.py` 校验规则；
4. 跑一遍词法对照测试，确认没有退化。