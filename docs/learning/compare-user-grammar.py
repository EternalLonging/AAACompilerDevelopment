"""生成独立 C++ 对照版，比较文法和 AST 动作；不改正式解析器。"""
from __future__ import annotations

import argparse
import difflib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import generate_parser_tables as tables

spec = importlib.util.spec_from_file_location('user_grammar_check', HERE / 'check-user-grammar.py')
grammar = importlib.util.module_from_spec(spec)
spec.loader.exec_module(grammar)

# C++ 不能用 if/else/while/for 作为枚举名；只改生成后的内部名字。
CPP_NAMES = {'if': '条件语句', 'else': '否则分支', 'while': '当型循环', 'for': '计数循环'}
RENAMES = {
    '外部声明': '外部定义', '外部声明尾': '外部定义后续', '标量类型': '基本类型',
    '函数尾': '函数定义后续', '结构声明尾': '结构体后续', '定义后': '结构体定义后续',
    '对象尾': '变量声明后续', '对象续': '变量声明后续2', '成员声明': '结构体成员声明',
    '成员序列': '结构体成员声明序列2', '成员类型': '变量类型', '成员续': '结构体成员声明2',
    '数组可选': '数组定义', '界限可选': '数组长度', '形参表可选': '函数形参',
    '形参续': '函数形参2', '块': '代码块', '表达式可选': '表达式2', '否则可选': 'else2',
    '赋值续': '赋值', '逻辑或续': '逻辑或式2', '逻辑与续': '逻辑与式2',
    '相等续': '相等式2', '关系续': '关系式2', '加法续': '加法式2', '乘法续': '乘法式2',
    '表达后缀序列': '后缀序列', '表达后缀': '后缀', '实参可选': '函数实参',
    '实参续': '函数实参2', '字符串续': '字符串2',
}


def rename(text, names):
    return re.sub(r'\b(' + '|'.join(map(re.escape, sorted(names, key=len, reverse=True))) + r')\b',
                  lambda m: names[m[0]], text)


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('正式解析器已变化，需重新核对实验改动：' + old[:100])
    return text.replace(old, new)


def adapted_parser(original):
    text = rename(original, RENAMES)
    text = replace_once(text, '#include "predictive_table.hpp"', '#include "user-predictive-table.hpp"')
    text = replace_once(text, '''        if (symbol == 结构体成员声明) {
            out = std::move(v[3]);
            out.nodes.push_back(object(NodeType::MemberDecl, v[2]));
            out.nodes.back()->name = tokens[v[1].begin].lexeme;
            out.nodes.front()->range.end = tokens[v[4].begin].range.end;
        } else out = std::move(v[2]);''', '''        out = std::move(v[2]);''')
    text = replace_once(text, 'void declaration(Value& out, Value* v, int symbol)',
                        'void declaration(Value& out, Value* v)')
    start = text.index('        switch (symbol) {', text.index('    Value reduce('))
    end = text.index('        if (n) { finish(n);', start)
    text = text[:start] + '''        switch (symbol) {
        case 程序: case 结构体成员声明序列: case 结构体成员声明序列2:
        case 局部声明序列: case 语句序列: case 后缀序列: case 函数形参: case 函数实参:
            if (count == 2) out = sequence(v[0], v[1]);
            break;
        case 函数形参2: case 函数实参2: out = sequence(v[1], v[2]); break;
        case 基本类型: case 变量类型: out.type = v[0].type ? v[0].type : specs(v[0].begin); break;
        case 数组定义: out.type = declarator(v[0].begin, v[2].end); break;
        case 变量声明后续2:
            if (count == 3) { out = std::move(v[2]); out.nodes.back()->name = tokens[v[1].begin].lexeme; }
            break;
        case 变量声明后续:
            out = std::move(v[2]); out.nodes.push_back(object(NodeType::VarDecl, v[0], v[1].nodes.empty() ? nullptr : v[1].nodes[0], v[1].end));
            out.nodes.front()->range.end = tokens[v[2].end - 1].range.end; break;
        case 结构体成员声明2:
            if (count == 4) {
                out = std::move(v[3]); n = object(NodeType::MemberDecl, v[2]);
                n->name = tokens[v[1].begin].lexeme;
            }
            break;
        case 结构体成员声明:
            out = std::move(v[1]);
            for (auto member : out.nodes) {
                bind_type(member, v[0].type); member->range.begin = tokens[v[0].begin].range.begin;
            }
            n = v[0].nodes[0]; n->kind = NodeType::MemberDecl; out.nodes.push_back(n); n = nullptr;
            out.nodes.front()->range.end = tokens[v[1].end - 1].range.end; break;
        case 结构体定义后续:
            if (count == 2) { out = std::move(v[1]); out.nodes.back()->name = tokens[v[0].begin].lexeme; }
            break;
        case 结构体后续:
            if (count == 2) { out = std::move(v[1]); out.nodes.back()->name = tokens[v[0].begin].lexeme; }
            else {
                out = std::move(v[3]);
                n = node(NodeType::StructDef, v[0].begin, out.nodes.empty() ? v[3].end : v[2].end,
                               "", {v[1].nodes.rbegin(), v[1].nodes.rend()});
            }
            break;
        case 函数定义后续:
            n = node(NodeType::FunctionDef, v[0].begin, v[3].end, "", {v[1].nodes.rbegin(), v[1].nodes.rend()});
            n->children.push_back(v[3].nodes[0]); break;
        case 外部定义: case 局部声明:
            if (count == 1) out = std::move(v[0]); else declaration(out, v);
            break;
        case 结构体声明: declaration(out, v); break;
        case 形参:
            n = node(NodeType::ParamDecl, v[0].begin, v[2].end, tokens[v[1].begin].lexeme);
            n->declared_type = v[2].type; out.type = v[0].type;
            bind_type(n, v[0].type); break;
        case 代码块: block(out, v); break;
        case 语句:
            if (count == 1) out = std::move(v[0]); else statement(out, v, count);
            break;
        case 条件语句: case 当型循环: case 计数循环: case 返回语句: statement(out, v, count); break;
        case 赋值: case else2: out.nodes = std::move(v[1].nodes); break;
        case 表达式:
            if (v[1].nodes.empty()) out = std::move(v[0]);
            else n = node(NodeType::Assign, v[0].begin, v[1].end, "=", {v[0].nodes[0], v[1].nodes[0]});
            break;
        case 逻辑或式: case 逻辑与式: case 相等式: case 关系式: case 加法式: case 乘法式:
            expression(out, v); break;
        case 逻辑或式2: case 逻辑与式2: case 相等式2: case 关系式2: case 加法式2: case 乘法式2:
            if (count == 3) out = std::move(v[2]);
            out.nodes.push_back(node(NodeType::BinaryOp,
                                     v[0].begin, v[1].end, tokens[v[0].begin].lexeme, {v[1].nodes[0]})); break;
        case 一元式: case 主式: case 字符串: unary(out, v, symbol, count); break;
        case 后缀式: postfix(out, v); break;
        case 后缀:
            if (count == 2) n = node(NodeType::MemberAccess, v[0].begin, v[1].end, tokens[v[1].begin].lexeme);
            else if (tokens[v[0].begin].type == TT::LPAREN)
                n = node(NodeType::Call, v[0].begin, v[2].end, "", {v[1].nodes.rbegin(), v[1].nodes.rend()});
            else n = node(NodeType::ArrayAccess, v[0].begin, v[2].end, "", {v[1].nodes[0]});
            break;
        default: if (count == 1) out = std::move(v[0]); break;
        }
''' + text[end:]
    return text


def candidate_parser(original):
    """形参只交付声明信息；参数和成员节点由使用它的外层创建。"""
    text = adapted_parser(original)
    text = replace_once(text, '        TypePtr type = nullptr; // 基础类型或尚未填入基础类型的数组。',
                        '''        TypePtr type = nullptr; // 基础类型或尚未填入基础类型的数组。
        TypePtr array = nullptr; // 形参规则识别的数组部分；普通声明时为空。
        std::size_t name = 0; // 形参规则识别的名字在单词表中的下标。''')
    marker = '    // 完成声明：为对象补名字、基础类型和声明起点，或完成函数与结构体。'
    text = replace_once(text, marker, '''    // 根据声明信息建立指定种类的节点；形参规则本身不建立 AST 节点。
    Node named_object(NodeType kind, Value& item) {
        auto n = node(kind, item.begin, item.end, tokens[item.name].lexeme);
        n->declared_type = item.array;
        bind_type(n, item.type);
        finish(n); return n;
    }
''' + marker)
    text = replace_once(text, '''        case 局部声明序列: case 语句序列: case 后缀序列: case 函数形参: case 函数实参:''',
                        '''        case 局部声明序列: case 语句序列: case 后缀序列: case 函数实参:''')
    text = replace_once(text, '        case 函数形参2: case 函数实参2: out = sequence(v[1], v[2]); break;',
                        '''        case 函数实参2: out = sequence(v[1], v[2]); break;
        case 函数形参: case 函数形参2:
            if (count > 1) {
                out = std::move(v[count - 1]);
                out.nodes.push_back(named_object(NodeType::ParamDecl, v[count - 2]));
            }
            break;''')
    text = replace_once(text, '            n = v[0].nodes[0]; n->kind = NodeType::MemberDecl; out.nodes.push_back(n); n = nullptr;',
                        '            out.nodes.push_back(named_object(NodeType::MemberDecl, v[0]));')
    text = replace_once(text, '''        case 形参:
            n = node(NodeType::ParamDecl, v[0].begin, v[2].end, tokens[v[1].begin].lexeme);
            n->declared_type = v[2].type; out.type = v[0].type;
            bind_type(n, v[0].type); break;''', '''        case 形参:
            out.type = v[0].type; out.array = v[2].type; out.name = v[1].begin; break;''')
    return text


def reused_parser(original):
    """完整类型存入现有 type；名字由当前 MiniC 声明格式定位。"""
    text = candidate_parser(original)
    text = replace_once(text, '''        TypePtr array = nullptr; // 形参规则识别的数组部分；普通声明时为空。
        std::size_t name = 0; // 形参规则识别的名字在单词表中的下标。
''', '')
    text = replace_once(text, '''        auto n = node(kind, item.begin, item.end, tokens[item.name].lexeme);
        n->declared_type = item.array;
        bind_type(n, item.type);''', '''        const auto name = item.begin + (tokens[item.begin].type == TT::KW_STRUCT ? 2 : 1);
        auto n = node(kind, item.begin, item.end, tokens[name].lexeme);
        n->declared_type = item.type;''')
    text = replace_once(text, '                bind_type(member, v[0].type); member->range.begin = tokens[v[0].begin].range.begin;',
                        '                bind_type(member, v[0].type->kind == TypeKind::Array ? v[0].type->base : v[0].type); member->range.begin = tokens[v[0].begin].range.begin;')
    text = replace_once(text, '            out.type = v[0].type; out.array = v[2].type; out.name = v[1].begin; break;',
                        '''            out.type = v[2].type ? v[2].type : v[0].type;
            if (v[2].type) out.type->base = v[0].type;
            break;''')
    return text


def ranged_parser(original):
    """形参只保留范围和数组类型，外层用现有 object() 创建节点。"""
    text = reused_parser(original)
    text = replace_once(text, '''        auto n = node(kind, item.begin, item.end, tokens[name].lexeme);
        n->declared_type = item.type;
        finish(n); return n;''', '''        auto n = object(kind, item);
        n->name = tokens[name].lexeme;
        bind_type(n, specs(item.begin));
        return n;''')
    text = replace_once(text, '                bind_type(member, v[0].type->kind == TypeKind::Array ? v[0].type->base : v[0].type); member->range.begin = tokens[v[0].begin].range.begin;',
                        '                bind_type(member, base); member->range.begin = tokens[v[0].begin].range.begin;')
    text = replace_once(text, '''        case 结构体成员声明:
            out = std::move(v[1]);''', '''        case 结构体成员声明: {
            auto base = specs(v[0].begin);
            out = std::move(v[1]);''')
    text = replace_once(text, '''            out.nodes.front()->range.end = tokens[v[1].end - 1].range.end; break;
        case 结构体定义后续:''', '''            out.nodes.front()->range.end = tokens[v[1].end - 1].range.end; break;
        }
        case 结构体定义后续:''')
    text = replace_once(text, '''            out.type = v[2].type ? v[2].type : v[0].type;
            if (v[2].type) out.type->base = v[0].type;
            break;''', '            out.type = v[2].type; break;')
    return text


def delayed_parser(original):
    """将形参与成员首项的创建合并在同一动作，不新增辅助函数。"""
    text = ranged_parser(original)
    start = text.index('    // 根据声明信息建立指定种类的节点')
    end = text.index('    // 完成声明：', start)
    text = text[:start] + text[end:]
    old = '''        case 函数形参: case 函数形参2:
            if (count > 1) {
                out = std::move(v[count - 1]);
                out.nodes.push_back(named_object(NodeType::ParamDecl, v[count - 2]));
            }
            break;'''
    text = replace_once(text, old, '''        case 函数形参: case 函数形参2: case 结构体成员声明: {
            if (count == 1) break; // 函数形参为 void；空产生式已提前返回。
            const bool member = symbol == 结构体成员声明;
            auto& item = v[count - 2];
            auto base = specs(item.begin);
            out = std::move(v[count - 1]);
            if (member) for (auto child : out.nodes) {
                bind_type(child, base); child->range.begin = tokens[item.begin].range.begin;
            }
            n = object(member ? NodeType::MemberDecl : NodeType::ParamDecl, item);
            n->name = tokens[item.begin + (tokens[item.begin].type == TT::KW_STRUCT ? 2 : 1)].lexeme;
            bind_type(n, base); out.nodes.push_back(n); n = nullptr;
            if (member) out.nodes.front()->range.end = tokens[v[1].end - 1].range.end;
            break;
        }''')
    start = text.index('        case 结构体成员声明: {')
    # 上面合并分支有前缀，因此这里匹配的是原有独立成员动作。
    end = text.index('        case 结构体定义后续:', start)
    text = text[:start] + text[end:]
    return text


def context_parser(original):
    """父产生式传入参数/成员种类，形参可直接建立最终节点。"""
    text = adapted_parser(original)
    text = replace_once(text, '        std::size_t base = 0; // 产生式第一个右部符号在语义值栈中的位置。',
                        '''        std::size_t base = 0; // 产生式第一个右部符号在语义值栈中的位置。
        NodeType declaration_kind = NodeType::ParamDecl; // 外层指定形参规则应创建参数还是成员。''')
    text = replace_once(text, 'Value reduce(int symbol, Value* v, int count)',
                        'Value reduce(int symbol, Value* v, int count, NodeType declaration_kind)')
    text = replace_once(text, '            n = v[0].nodes[0]; n->kind = NodeType::MemberDecl; out.nodes.push_back(n); n = nullptr;',
                        '            out.nodes.push_back(v[0].nodes[0]);')
    text = replace_once(text, '            n = node(NodeType::ParamDecl, v[0].begin, v[2].end, tokens[v[1].begin].lexeme);',
                        '            n = node(declaration_kind, v[0].begin, v[2].end, tokens[v[1].begin].lexeme);')
    text = replace_once(text, 'reduce(frame.symbol, count ? &values[frame.base] : nullptr, count);',
                        'reduce(frame.symbol, count ? &values[frame.base] : nullptr, count, frame.declaration_kind);')
    text = replace_once(text, 'stack.push_back({frame.symbol, choice, values.size()});',
                        'stack.push_back({frame.symbol, choice, values.size(), frame.declaration_kind});')
    text = replace_once(text, '                    for (int i = production.length - 1; i >= 0; --i) stack.push_back({production.rhs[i]});',
                        '''                    for (int i = production.length - 1; i >= 0; --i)
                        stack.push_back({production.rhs[i], -1, 0,
                            frame.symbol == 结构体成员声明 ? NodeType::MemberDecl : NodeType::ParamDecl});''')
    return text


PROBE = r'''#include "minic/lexer.hpp"
#include "minic/parser.hpp"
#include "minic/display.hpp"
#include <iostream>
#include <iomanip>
#include <string>
using namespace minic;
void type(TypePtr t) {
    if (!t) { std::cout << "null"; return; }
    std::cout << '[' << static_cast<int>(t->kind) << ',' << std::quoted(t->name) << ',';
    if (t->array_length) std::cout << *t->array_length; else std::cout << "null";
    std::cout << ','; type(t->base); std::cout << ",[";
    for (std::size_t i = 0; i < t->params.size(); ++i) { if (i) std::cout << ','; type(t->params[i]); }
    std::cout << "]]";
}
void emit(const ASTNode* n) {
    if (!n) { std::cout << "null"; return; }
    std::cout << '[' << static_cast<int>(n->kind) << ',' << std::quoted(n->name) << ',';
    type(n->declared_type);
    std::cout << ',' << n->range.begin.offset << ',' << n->range.end.offset << ",[";
    for (std::size_t i = 0; i < n->children.size(); ++i) { if (i) std::cout << ','; emit(n->children[i]); }
    std::cout << "]]";
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::string source;
        for (std::size_t i = 0; i < line.size(); i += 2)
            source += static_cast<char>(std::stoul(line.substr(i, 2), nullptr, 16));
        TypeArena arena; TypeArenaScope scope(arena);
        auto lexed = lex(source);
        if (!lexed.ok()) { std::cout << "{\"lexer\":false}\n"; continue; }
        auto parsed = parse(lexed.tokens);
        std::cout << "{\"lexer\":true,\"ast\":"; emit(parsed.root);
        std::cout << ",\"diagnostics\":[";
        for (std::size_t i = 0; i < parsed.diagnostics.size(); ++i) {
            if (i) std::cout << ',';
            const auto& d = parsed.diagnostics[i];
            std::cout << '[' << std::quoted(d.code) << ',' << d.range.begin.offset << ']';
        }
        std::cout << "]}\n";
    }
}
'''


def metrics(text):
    lines = text.splitlines()
    return {'lines': len(lines),
            'code_lines': sum(bool(line.strip()) and not line.lstrip().startswith('//') for line in lines)}


def function_metrics(text):
    result = {}
    for name in ('declaration', 'named_object', 'statement', 'expression', 'reduce', 'run'):
        match = re.search(r'^    [^\n]*\b' + name + r'\([^\n]*\) \{', text, re.M)
        if not match:
            continue
        start = match.start()
        end = text.index('\n    }', start) + len('\n    }')
        result[name] = metrics(text[start:end])
    return result


def grammar_metrics(rows, terminals):
    data = tables.derive(rows)
    return {'productions': len(rows), 'nonterminals': len(data['names']),
            'rhs_symbols': sum(len(row['rhs']) for row in rows),
            'prediction_cells': len(data['table']),
            'dense_table_cells': len(data['names']) * terminals}


def run(command, **kwargs):
    result = subprocess.run(list(map(str, command)), encoding='utf-8',
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120, **kwargs)
    if result.returncode:
        raise RuntimeError(result.stderr or result.stdout)
    if result.stderr:
        print(result.stderr, file=sys.stderr)
    return result


def main():
    cli = argparse.ArgumentParser()
    cli.add_argument('--build-dir', type=Path, required=True, help='已有 CMake 构建目录；MinGW 需英文路径')
    cli.add_argument('--cxx', default='g++')
    args = cli.parse_args()
    build = args.build_dir.resolve()
    experiment = build / 'user-grammar-comparison'
    experiment.mkdir(exist_ok=True)
    original = grammar.baseline_source('src/语法分析/parser.cpp')
    adapted = adapted_parser(original)
    candidate = candidate_parser(original)
    variants = {'original': original, 'adapted': adapted, 'candidate': candidate,
                'reused': reused_parser(original), 'ranged': ranged_parser(original),
                'delayed': delayed_parser(original), 'context': context_parser(original)}
    current_rows = grammar.baseline_productions()
    # 保留当时六种实现的比较输入，不随之后的正式文法改名而变化。
    experiment_bnf = run(['git', 'show', '5b50939:docs/learning/用户文法整理版.bnf'], cwd=ROOT).stdout
    user_rows = tables.productions('\n'.join(line for line in experiment_bnf.splitlines()
                                          if line.strip() and not line.lstrip().startswith('#')))
    cpp_rows = [{**row, 'lhs': CPP_NAMES.get(row['lhs'], row['lhs']),
                 'rhs': [CPP_NAMES.get(s, s) for s in row['rhs']]} for row in user_rows]
    original_productions = tables.productions
    try:
        tables.productions = lambda: cpp_rows
        generated_table = tables.runtime_table()
    finally:
        tables.productions = original_productions
    (experiment / 'user-predictive-table.hpp').write_text(generated_table, encoding='utf-8')
    (experiment / 'predictive_table.hpp').write_text(
        grammar.baseline_source('src/语法分析/predictive_table.hpp'), encoding='utf-8')
    for name, source in variants.items():
        (experiment / (name + '.cpp')).write_text(source, encoding='utf-8')
    (experiment / 'probe.cpp').write_text(PROBE, encoding='utf-8')
    executables = []
    for source in variants:
        exe = experiment / (source + '.exe')
        run([args.cxx, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-finput-charset=UTF-8',
             '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'src/语法分析'),
             experiment / (source + '.cpp'), experiment / 'probe.cpp',
             build / 'libminic_compiler.a', build / 'libminic_backend.a', '-o', exe])
        executables.append(exe)

    cases = [(name, source) for name, source in grammar.VALID + grammar.INVALID]
    manifest = ROOT / 'tests/integration/manifest.json'
    cases += [('联调/' + c['id'], (manifest.parent / c['source']).read_text(encoding='utf-8'))
              for c in json.loads(manifest.read_text(encoding='utf-8'))['cases']]
    # 覆盖声明复用、顺序和各种类型，并用多运算符输入检查左结合。
    for count in (1, 2, 8, 24):
        names = ','.join(f'x{i}[{i + 1}]' for i in range(count))
        cases += [(f'成员长度{count}', f'struct S{{float {names};char c;}};'),
                  (f'声明长度{count}', f'int {names};'),
                  (f'表达式长度{count}', 'int main(){return ' + '-'.join(['99'] + ['1'] * count) + ';}')]
    cases += [('完整类型与范围', 'struct S{int a,b[2],c;};int f(struct S x[],int a,float b,char c){struct S y[3],z;int i=0,j=1,k[2];for(i=0;i<2;i=i+1){x[i].a=j;}return a=j=3;}')]
    for base in ('int', 'float', 'char', 'struct S'):
        for first in ('a', 'a[2]', 'a[]'):
            source = f'struct S{{int x;}};struct T{{{base} {first},b[3],c;}};'
            cases.append((f'成员组合/{base}/{first}', source))
            source = f'struct S{{int x;}};int f({base} {first},int b[],char c){{return 0;}}'
            cases.append((f'形参组合/{base}/{first}', source))
    cases += [('局部结构体交接', 'int f(int a){struct S{int x[2],y;float z;}s;{struct T{char c,d[3];}t;}return a;}'),
              ('后缀与字符串范围', 'int f(){int a[2];scanf("%d",&(a[0]));printf("a" "b",f(a[0]));return a[0];}'),
              ('右结合与左结合', 'int f(){int a,b,c;a=b=c=20-8-4/2/2;return a;}')]
    wire = ''.join(source.encode('utf-8').hex() + '\n' for _, source in cases)
    outputs = [[json.loads(line) for line in run([exe], input=wire).stdout.splitlines()]
               for exe in executables]
    if any(len(items) != len(cases) for items in outputs):
        raise ValueError('C++ 探针响应数量不匹配')
    for variant, results in zip(list(variants)[2:], outputs[2:]):
        for (name, _), before, after in zip(cases, outputs[1], results):
            if before != after:
                raise AssertionError(('改造后 AST 或诊断不一致', variant, name, before, after))
    outputs = [outputs[0], outputs[2]]
    expected = {item['name']: item for item in json.loads(grammar.OUTPUT.read_text(encoding='utf-8'))['cases']}
    expected['形参组合/struct S/a'] = {'accepted': True, 'current_grammar_accepted': False}
    matches, rejections, differences = [], [], []
    for (name, source), old, new in zip(cases, *outputs):
        if name in expected:
            result = expected[name]
            for output, key in ((old, 'current_grammar_accepted'), (new, 'accepted')):
                syntax_accepted = result.get(key, False)
                if syntax_accepted and output.get('ast') is None:
                    # 原解析器仍会检查字面量和数组长度，这些样例应保持同样诊断。
                    if not output.get('diagnostics') or output['diagnostics'][0][0] not in ('PARSE_ARRAY_BOUND', 'PARSE_LITERAL'):
                        raise AssertionError((name, key, output))
                elif not syntax_accepted and output.get('ast') is not None:
                    raise AssertionError((name, key, output))
        elif old.get('ast') is None or new.get('ast') is None:
            raise AssertionError(('额外有效样例解析失败', name, old, new))
        if old.get('ast') is not None and new.get('ast') is not None:
            if old != new:
                raise AssertionError(('AST 不一致', name, old, new))
            matches.append(name)
        elif old.get('ast') is None and new.get('ast') is None:
            if old.get('diagnostics') != new.get('diagnostics'):
                raise AssertionError(('报错代码或位置不一致', name, old, new))
            rejections.append(name)
        else:
            differences.append(name)
    if sorted(differences) != sorted(['elseif链', '按值结构体形参的形状', '取地址后直接负号', '形参组合/struct S/a']):
        raise AssertionError(('出现未预期的接受范围差别', differences))
    enum = re.search(r'enum class TokenType\s*\{(.*?)\};', (ROOT / 'include/minic/interface.hpp').read_text(encoding='utf-8'), re.S)[1]
    terminals = len(re.findall(r'\b[A-Z][A-Z_]+\b', enum))
    output = {'comparison_baseline': grammar.BASELINE_REF,
              'current': {**grammar_metrics(current_rows, terminals), **metrics(original),
                          'functions': function_metrics(original)},
              'adapted': {**metrics(adapted), 'functions': function_metrics(adapted)},
              'candidate': {**grammar_metrics(user_rows, terminals), **metrics(candidate),
                            'functions': function_metrics(candidate)},
              'ast_matches': len(matches), 'both_rejected': len(rejections), 'language_differences': differences,
              'cases': len(cases), 'matched_case_names': matches, 'rejected_case_names': rejections}
    output['neutral_matches_adapted'] = len(cases)
    output['variants'] = {name: {**metrics(source), 'functions': function_metrics(source),
                                 'matches_adapted': len(cases)}
                          for name, source in variants.items() if name != 'original'}
    (experiment / 'comparison.json').write_text(json.dumps(output, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    normalized = rename(original, RENAMES).replace('#include "predictive_table.hpp"', '#include "user-predictive-table.hpp"')
    patch = ''.join(difflib.unified_diff(normalized.splitlines(True), candidate.splitlines(True),
                                        fromfile='原版（只统一名字）', tofile='用户版（独立实验）'))
    (experiment / 'ast-actions.diff').write_text(patch, encoding='utf-8')
    neutral_patch = ''.join(difflib.unified_diff(adapted.splitlines(True), candidate.splitlines(True),
                                                fromfile='用户版（首次适配）', tofile='用户版（声明信息）'))
    (experiment / 'declaration-info.diff').write_text(neutral_patch, encoding='utf-8')
    for name in ('reused', 'ranged', 'delayed', 'context'):
        variant_patch = ''.join(difflib.unified_diff(adapted.splitlines(True), variants[name].splitlines(True),
                                                   fromfile='用户版（首次适配）', tofile=name))
        (experiment / (name + '.diff')).write_text(variant_patch, encoding='utf-8')
    summary = {'cases': len(cases), 'ast_matches_original': len(matches),
               'both_rejected': len(rejections), 'known_language_differences': differences,
               'code_lines': {name: metrics(source)['code_lines'] for name, source in variants.items()}}
    print(json.dumps(summary, ensure_ascii=True, indent=2))
    print('Generated experiment: ' + str(experiment))


if __name__ == '__main__':
    main()
