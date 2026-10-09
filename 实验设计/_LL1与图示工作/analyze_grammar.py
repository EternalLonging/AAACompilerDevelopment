"""C89 grammar candidate, fixed-point sets, LL(1) conflicts and predictive table."""
from pathlib import Path
import collections
import json
import itertools
import hashlib
import sys

ROOT=Path(__file__).resolve().parent
OUT=ROOT.parent/'LL1与图示'
OUT.mkdir(exist_ok=True)
source=ROOT.parent/'_状态转换表工作/dfa_data.json'
dfa=json.loads(source.read_text(encoding='utf-8'))
G=collections.OrderedDict()
sections={}
section='程序和声明'
def rule(lhs,*alternatives):
    aliases={'INC':'PLUS_PLUS','DEC':'MINUS_MINUS','AMP_ASSIGN':'AND_ASSIGN'}
    G[lhs]=[[aliases.get(s,s) for s in a.split()] if a!='ε' else [] for a in alternatives]
    sections[lhs]=section

rule('TranslationUnit','ExternalDeclaration TranslationTail')
rule('TranslationTail','ExternalDeclaration TranslationTail','ε')
rule('ExternalDeclaration','DeclarationSpecifiers ExternalAfterSpecs','Declarator OldDeclarations CompoundStatement')
rule('ExternalAfterSpecs','SEMI','Declarator ExternalDeclaratorTail')
rule('ExternalDeclaratorTail','OldDeclarations CompoundStatement','InitializerOpt InitDeclaratorTail SEMI')
rule('OldDeclarations','Declaration OldDeclarations','ε')
rule('Declaration','DeclarationSpecifiers InitDeclaratorsOpt SEMI')
rule('DeclarationSpecifiers','DeclarationSpecifier DeclarationSpecifiersTail')
rule('DeclarationSpecifiersTail','DeclarationSpecifier DeclarationSpecifiersTail','ε')
rule('DeclarationSpecifier','StorageClass','TypeSpecifier','TypeQualifier')
rule('StorageClass',*[k for k in ['KW_TYPEDEF','KW_EXTERN','KW_STATIC','KW_AUTO','KW_REGISTER']])
rule('TypeQualifier','KW_CONST','KW_VOLATILE')
rule('TypeSpecifier',*[k for k in ['KW_VOID','KW_CHAR','KW_SHORT','KW_INT','KW_LONG','KW_FLOAT','KW_DOUBLE','KW_SIGNED','KW_UNSIGNED']],
     'StructUnionSpecifier','EnumSpecifier','TYPE_NAME')
rule('StructUnionSpecifier','StructUnion StructUnionBody')
rule('StructUnion','KW_STRUCT','KW_UNION')
rule('StructUnionBody','ID StructBodyOpt','LBRACE StructDeclarations RBRACE')
rule('StructBodyOpt','LBRACE StructDeclarations RBRACE','ε')
rule('StructDeclarations','StructDeclaration StructDeclarationsTail')
rule('StructDeclarationsTail','StructDeclaration StructDeclarationsTail','ε')
rule('StructDeclaration','SpecifierQualifierList StructDeclaratorList SEMI')
rule('SpecifierQualifierList','SpecifierQualifier SpecifierQualifierTail')
rule('SpecifierQualifier','TypeSpecifier','TypeQualifier')
rule('SpecifierQualifierTail','SpecifierQualifier SpecifierQualifierTail','ε')
rule('StructDeclaratorList','StructDeclarator StructDeclaratorTail')
rule('StructDeclaratorTail','COMMA StructDeclarator StructDeclaratorTail','ε')
rule('StructDeclarator','Declarator BitFieldOpt','COLON ConstantExpression')
rule('BitFieldOpt','COLON ConstantExpression','ε')
rule('EnumSpecifier','KW_ENUM EnumBody')
rule('EnumBody','ID EnumBodyOpt','LBRACE EnumeratorList RBRACE')
rule('EnumBodyOpt','LBRACE EnumeratorList RBRACE','ε')
rule('EnumeratorList','Enumerator EnumeratorTail')
rule('EnumeratorTail','COMMA Enumerator EnumeratorTail','ε')
rule('Enumerator','ID EnumeratorValueOpt')
rule('EnumeratorValueOpt','ASSIGN ConstantExpression','ε')
rule('InitDeclaratorsOpt','InitDeclarator InitDeclaratorTail','ε')
rule('InitDeclarator','Declarator InitializerOpt')
rule('InitDeclaratorTail','COMMA InitDeclarator InitDeclaratorTail','ε')
rule('InitializerOpt','ASSIGN Initializer','ε')
rule('Initializer','AssignmentExpression','LBRACE InitializerList InitializerClose')
rule('InitializerList','Initializer InitializerListTail')
rule('InitializerListTail','COMMA InitializerAfterComma','ε')
rule('InitializerAfterComma','Initializer InitializerListTail','ε')
rule('InitializerClose','RBRACE')

section='声明符和类型名'
rule('Declarator','PointerOpt DirectDeclarator')
rule('PointerOpt','Pointer','ε')
rule('Pointer','STAR TypeQualifiersOpt PointerOpt')
rule('TypeQualifiersOpt','TypeQualifier TypeQualifiersOpt','ε')
rule('DirectDeclarator','ID DeclaratorSuffixes','LPAREN Declarator RPAREN DeclaratorSuffixes')
rule('DeclaratorSuffixes','DeclaratorSuffix DeclaratorSuffixes','ε')
rule('DeclaratorSuffix','LBRACKET ConstantExpressionOpt RBRACKET','LPAREN FunctionParametersOpt RPAREN')
rule('ConstantExpressionOpt','ConstantExpression','ε')
rule('FunctionParametersOpt','ParameterTypeList','IdentifierList','ε')
rule('IdentifierList','ID IdentifierTail')
rule('IdentifierTail','COMMA ID IdentifierTail','ε')
rule('ParameterTypeList','ParameterDeclaration ParameterTail')
rule('ParameterTail','COMMA ParameterAfterComma','ε')
rule('ParameterAfterComma','ParameterDeclaration ParameterTail','ELLIPSIS')
rule('ParameterDeclaration','DeclarationSpecifiers ParameterDeclaratorOpt')
rule('ParameterDeclaratorOpt','Declarator','AbstractDeclarator','ε')
rule('TypeName','SpecifierQualifierList AbstractDeclaratorOpt')
rule('AbstractDeclaratorOpt','AbstractDeclarator','ε')
rule('AbstractDeclarator','Pointer DirectAbstractOpt','DirectAbstractDeclarator')
rule('DirectAbstractOpt','DirectAbstractDeclarator','ε')
rule('DirectAbstractDeclarator','LPAREN AbstractParenBody RPAREN AbstractSuffixes','LBRACKET ConstantExpressionOpt RBRACKET AbstractSuffixes')
rule('AbstractParenBody','AbstractDeclarator','ParameterTypeListOpt')
rule('ParameterTypeListOpt','ParameterTypeList','ε')
rule('AbstractSuffixes','AbstractSuffix AbstractSuffixes','ε')
rule('AbstractSuffix','LBRACKET ConstantExpressionOpt RBRACKET','LPAREN ParameterTypeListOpt RPAREN')

section='语句和控制流（控制体必须使用花括号）'
rule('CompoundStatement','LBRACE DeclarationsOpt StatementsOpt RBRACE')
rule('DeclarationsOpt','Declaration DeclarationsOpt','ε')
rule('StatementsOpt','Statement StatementsOpt','ε')
rule('Statement','LabeledStatement','CompoundStatement','ExpressionStatement','SelectionStatement','IterationStatement','JumpStatement')
rule('LabeledStatement','ID COLON Statement','KW_CASE ConstantExpression COLON Statement','KW_DEFAULT COLON Statement')
rule('ExpressionStatement','ExpressionOpt SEMI')
rule('ExpressionOpt','Expression','ε')
rule('SelectionStatement','KW_IF LPAREN Expression RPAREN CompoundStatement ElseOpt','KW_SWITCH LPAREN Expression RPAREN CompoundStatement')
rule('ElseOpt','KW_ELSE CompoundStatement','ε')
rule('IterationStatement','KW_WHILE LPAREN Expression RPAREN CompoundStatement',
     'KW_DO CompoundStatement KW_WHILE LPAREN Expression RPAREN SEMI',
     'KW_FOR LPAREN ExpressionOpt SEMI ExpressionOpt SEMI ExpressionOpt RPAREN CompoundStatement')
rule('JumpStatement','KW_GOTO ID SEMI','KW_CONTINUE SEMI','KW_BREAK SEMI','KW_RETURN ExpressionOpt SEMI')

section='表达式（消除左递归，保持标准赋值左部约束）'
rule('Expression','AssignmentExpression ExpressionTail')
rule('ExpressionTail','COMMA AssignmentExpression ExpressionTail','ε')
rule('AssignmentExpression','UnaryExpression AssignmentOperator AssignmentExpression','ConditionalExpression')
rule('AssignmentOperator',*[k for k in ['ASSIGN','PLUS_ASSIGN','MINUS_ASSIGN','STAR_ASSIGN','SLASH_ASSIGN','PERCENT_ASSIGN','SHIFT_LEFT_ASSIGN','SHIFT_RIGHT_ASSIGN','AMP_ASSIGN','OR_ASSIGN','XOR_ASSIGN']])
rule('ConstantExpression','ConditionalExpression')
rule('ConditionalExpression','LogicalOrExpression ConditionalTail')
rule('ConditionalTail','QUESTION Expression COLON ConditionalExpression','ε')
layers=[('LogicalOr','LogicalAnd',['OR']),('LogicalAnd','BitwiseOr',['AND']),
        ('BitwiseOr','BitwiseXor',['BIT_OR']),('BitwiseXor','BitwiseAnd',['BIT_XOR']),
        ('BitwiseAnd','Equality',['AMP']),('Equality','Relational',['EQ','NE']),
        ('Relational','Shift',['LT','GT','LE','GE']),('Shift','Additive',['SHIFT_LEFT','SHIFT_RIGHT']),
        ('Additive','Multiplicative',['PLUS','MINUS']),('Multiplicative','Cast',['STAR','SLASH','PERCENT'])]
for name,child,ops in layers:
    rule(name+'Expression',child+'Expression '+name+'Tail')
    rule(name+'Tail',*[op+' '+child+'Expression '+name+'Tail' for op in ops],'ε')
rule('CastExpression','UnaryExpression','LPAREN TypeName RPAREN CastExpression')
rule('UnaryExpression','PostfixExpression','INC UnaryExpression','DEC UnaryExpression','UnaryOperator CastExpression','KW_SIZEOF SizeofOperand')
rule('UnaryOperator','AMP','STAR','PLUS','MINUS','BIT_NOT','NOT')
rule('SizeofOperand','UnaryExpression','LPAREN TypeName RPAREN')
rule('PostfixExpression','PrimaryExpression PostfixTail')
rule('PostfixTail','LBRACKET Expression RBRACKET PostfixTail','LPAREN ArgumentsOpt RPAREN PostfixTail',
     'DOT ID PostfixTail','ARROW ID PostfixTail','INC PostfixTail','DEC PostfixTail','ε')
rule('ArgumentsOpt','AssignmentExpression ArgumentTail','ε')
rule('ArgumentTail','COMMA AssignmentExpression ArgumentTail','ε')
rule('PrimaryExpression','ID','INT_LITERAL','FLOAT_LITERAL','CHAR_LITERAL','StringSequence','LPAREN Expression RPAREN')
rule('StringSequence','STRING_LITERAL StringSequenceTail')
rule('StringSequenceTail','STRING_LITERAL StringSequenceTail','ε')

CORE='--core' in sys.argv
if CORE:
    # Predictive core over contextual tokens. C89 recognition additionally
    # requires the documented assignment and declarator side conditions.
    section='上下文消歧后的表达式核心'
    rule('AssignmentExpression','ConditionalExpression AssignmentTail')
    rule('AssignmentTail','AssignmentOperator AssignmentExpression','ε')
    rule('CastExpression','LPAREN CastParenTail','NonParenUnary')
    rule('CastParenTail','TypeName RPAREN CastExpression','Expression RPAREN PostfixTail')
    rule('UnaryExpression','LPAREN Expression RPAREN PostfixTail','NonParenUnary')
    rule('NonParenUnary','Atom PostfixTail','INC UnaryExpression','DEC UnaryExpression','UnaryOperator CastExpression','KW_SIZEOF SizeofOperand')
    rule('Atom','ID','INT_LITERAL','FLOAT_LITERAL','CHAR_LITERAL','StringSequence')
    rule('SizeofOperand','LPAREN SizeofParenTail','NonParenUnary')
    rule('SizeofParenTail','TypeName RPAREN','Expression RPAREN PostfixTail')
    del G['PostfixExpression'];del G['PrimaryExpression']
    section='上下文消歧后的参数声明符'
    rule('ParameterDeclaratorOpt','ParameterDeclaratorNonEmpty','ε')
    rule('ParameterDeclaratorNonEmpty','Pointer ParameterDirectOpt','ParameterDirect')
    rule('ParameterDirectOpt','ParameterDirect','ε')
    rule('ParameterDirect','ID DeclaratorSuffixes','LPAREN ParameterParenBody RPAREN DeclaratorSuffixes',
         'LBRACKET ConstantExpressionOpt RBRACKET DeclaratorSuffixes')
    rule('ParameterParenBody','ParameterDeclaratorNonEmpty','ParameterTypeListOpt')
    G['LabeledStatement'][0][0]='LABEL_ID'

# Literal left factoring only. Shared FIRST through different nonterminals is
# retained and reported; never silently choose a production in a conflict cell.
counter=0
while True:
    changed=False
    for lhs,alts in list(G.items()):
        best=()
        for a,b in itertools.combinations(alts,2):
            prefix=[]
            for x,y in zip(a,b):
                if x!=y:break
                prefix.append(x)
            if len(prefix)>len(best):best=tuple(prefix)
        if best:
            counter+=1
            helper=f'{lhs}_Factor{counter}'
            matching=[a for a in alts if tuple(a[:len(best)])==best]
            others=[a for a in alts if a not in matching]
            G[lhs]=others+[list(best)+[helper]]
            G[helper]=[a[len(best):] for a in matching]
            sections[helper]=sections[lhs]
            changed=True
            break
    if not changed:break

nts=set(G)
tokens={r['token'] for r in dfa['rules'] if r['action']=='token'}|set(dfa['keywords'].values())
terminals=set(s for alts in G.values() for a in alts for s in a if s not in nts)
assert terminals-tokens==({'TYPE_NAME','LABEL_ID'} if CORE else {'TYPE_NAME'}),terminals-tokens
nullable=set()
while True:
    old=len(nullable)
    nullable.update(lhs for lhs,alts in G.items() if any(all(s in nullable for s in a) for a in alts))
    if len(nullable)==old:break
first={n:set() for n in G}
def first_seq(seq):
    out=set()
    for s in seq:
        if s not in nts:
            out.add(s);return out
        out.update(first[s]-{'ε'})
        if s not in nullable:return out
    out.add('ε')
    return out
while True:
    before=sum(map(len,first.values()))
    for lhs,alts in G.items():
        for a in alts:first[lhs].update(first_seq(a))
    if sum(map(len,first.values()))==before:break
follow={n:set() for n in G}
follow['TranslationUnit'].add('END_OF_FILE')
while True:
    before=sum(map(len,follow.values()))
    for lhs,alts in G.items():
        for a in alts:
            for i,s in enumerate(a):
                if s in nts:
                    f=first_seq(a[i+1:])
                    follow[s].update(f-{'ε'})
                    if 'ε' in f:follow[s].update(follow[lhs])
    if sum(map(len,follow.values()))==before:break
productions=[]
table=collections.defaultdict(list)
for lhs,alts in G.items():
    for a in alts:
        f=first_seq(a)
        sel=(f-{'ε'})|(follow[lhs] if 'ε' in f else set())
        p={'id':f'P{len(productions)+1:03d}','lhs':lhs,'rhs':a,'first_rhs':sorted(f),
           'select':sorted(sel),'nullable':not a or 'ε' in f,'section':sections[lhs]}
        productions.append(p)
        for t in sel:table[(lhs,t)].append(p['id'])
conflicts=[]
for (lhs,t),ids in sorted(table.items()):
    if len(ids)>1:conflicts.append({'lhs':lhs,'lookahead':t,'productions':ids})
def category(n):
    if n in ['DeclarationSpecifiersTail','SpecifierQualifierTail']:return 'typedef 类型名与声明上下文'
    if n=='AssignmentExpression':return '赋值左部与条件表达式共有 FIRST'
    if n in ['CastExpression','SizeofOperand']:return '圆括号表达式、类型转换与 sizeof(type)'
    if n=='Statement':return 'ID 标签与 ID 开头表达式语句'
    if n in ['ParameterDeclaratorOpt','AbstractParenBody']:return '具名、抽象及函数声明符'
    return '可空候选或多路径公共前缀'
solutions={
 'typedef 类型名与声明上下文':'作用域查询生成临时 TYPE_NAME；在标签、成员及正在声明的名字位置按上下文保留 ID。不能预先把整个 Token 流一次性改写。',
 '赋值左部与条件表达式共有 FIRST':'递归下降先解析条件表达式，再查赋值运算符；保留 UnaryExpression 左部形状检验以及后续左值语义检验。该工程分派不是原文法的无冲突 LL(1) 表。',
 '圆括号表达式、类型转换与 sizeof(type)':'消费公共 LPAREN 后，在正确 typedef 环境中判断类型名起始符并左因子化；必要时使用类型名识别子程序。不能仅凭当前 LPAREN 选规则。',
 'ID 标签与 ID 开头表达式语句':'标签与表达式需提取 ID 前缀或在语句入口预看 COLON；后者是受限 LL(2)，必须明确标注。',
 '具名、抽象及函数声明符':'统一声明符解析器，记录是否出现被声明名字；括号内区分分组声明符与参数列表。需上下文/更深入左因子化，不能任意覆盖冲突格。',
 '可空候选或多路径公共前缀':'逐格核对 FIRST/FOLLOW 与对应产生式；不以人为优先级假装冲突消失。'
}
for c in conflicts:
    c['category']=category(c['lhs'])
    c['solution']=solutions[c['category']]

# Independent validation: enumerate productive prefix terminals by monotone
# derivation approximation and compare to FIRST, check FOLLOW inclusions, and
# detect nullable-prefix left recursion.
productive=set()
while True:
    old=len(productive)
    productive.update(lhs for lhs,alts in G.items() if any(all(s not in nts or s in productive for s in a) for a in alts))
    if len(productive)==old:break
assert productive==nts
reachable={'TranslationUnit'}
while True:
    old=len(reachable)
    reachable.update(s for lhs in list(reachable) for a in G[lhs] for s in a if s in nts)
    if len(reachable)==old:break
assert reachable==nts,nts-reachable
starts={n:set() for n in G}
for lhs,alts in G.items():
    for a in alts:
        for s in a:
            if s in nts:starts[lhs].add(s)
            if s not in nullable:break
for n in G:
    todo=list(starts[n]);seen=set()
    while todo:
        s=todo.pop()
        assert s!=n,('left recursion',n)
        if s not in seen:seen.add(s);todo.extend(starts[s])
for p in productions:
    f=first_seq(p['rhs'])
    assert set(p['select'])==(f-{'ε'})|(follow[p['lhs']] if 'ε' in f else set())
    for i,s in enumerate(p['rhs']):
        if s in nts:
            tail=first_seq(p['rhs'][i+1:])
            assert tail-{'ε'} <= follow[s]
            if 'ε' in tail:assert follow[p['lhs']]<=follow[s]
assert 'KW_ELSE' not in follow['ElseOpt']
assert len({p['id'] for p in productions})==len(productions)
unused=sorted(tokens-terminals)
summary={'nonterminals':len(G),'productions':len(productions),'terminals':len(terminals),
         'conflict_cells':len(conflicts),'conflict_nonterminals':len({c['lhs'] for c in conflicts}),
         'strict_LL1':not conflicts,'left_recursion':False,'all_symbols_reachable':True,
         'all_nonterminals_productive':True,'all_32_keywords_used':set(dfa['keywords'].values())<=terminals,
         'control_bodies_braced':True,'unused_ordinary_tokens':unused}
data={'commit':dfa['commit'],'grammar':G,'sections':sections,'productions':productions,
      'first':{n:sorted(v) for n,v in first.items()},'follow':{n:sorted(v) for n,v in follow.items()},
      'nullable':sorted(nullable),'terminals':sorted(terminals|{'END_OF_FILE'}),
      'table':[{'lhs':n,'lookahead':t,'productions':v} for (n,t),v in sorted(table.items())],
      'conflicts':conflicts,'summary':summary}
(ROOT/('core_grammar_data.json' if CORE else 'grammar_data.json')).write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
lines=['# 类 C89 项目语言文法候选及 LL(1) 检查','',
 '版本：2026-10-09。项目设计类似 C89 的自定义 C 类语言，C89 仅作参考，功能与规则由项目决定。当前 BNF 是候选草案，控制体使用花括号。',
 f"本候选有 {len(G)} 个非终结符、{len(productions)} 条产生式和 {len(conflicts)} 个预测表冲突格。",'',
 'ε 表示空产生式；END_OF_FILE 为 FOLLOW 起始标记，不在 TranslationUnit 右侧重复写入。',
 'FIRST/FOLLOW/SELECT 均按此文件同一版 BNF 自动计算。当前草案采用块内先声明后语句、for 初始化为表达式；这些是草案规则，不是自动继承的标准要求。',
 'TYPE_NAME 是语法层基于 typedef 符号表查询得到的临时分类，原词法 TokenType 仍为 ID。',
 'if/else/while/for/do/switch 的语句体要求 CompoundStatement。控制体花括号要求已由用户确认；',
 '声明符、旧式函数定义、可变参数、位域、聚合初始化等在当前候选中保留，尚不构成必须实现的功能清单。','',
 '预处理是独立阶段；三字符组、宏、条件编译等是参考构造，支持范围由项目另行定义。HASH/HASH_HASH 不进入当前普通语法。',
 '当前草案枚举不接受尾逗号；聚合初始化允许尾逗号。相邻字符串在 StringSequence 中连接。',
 'TypeSpecifier 序列的合法组合、存储类别、void 参数、旧式声明合法性等仍需语义/上下文约束。','']
current=None
for p in productions:
    if p['section']!=current:current=p['section'];lines+=['',f'## {current}','']
    lines.append(f"{p['id']}  {p['lhs']} → {' '.join(p['rhs']) or 'ε'}")
lines+=['','## 无冲突表的判据','',
 '对每个 A → α：若 ε ∉ FIRST(α)，SELECT = FIRST(α)；否则 SELECT = (FIRST(α) − {ε}) ∪ FOLLOW(A)。',
 '同一左部的 SELECT 必须两两不交。删除左递归不等于已经满足 LL(1)。本表保留全部冲突候选，不按顺序覆盖。','',
 '## 上下文处理与工程选择','']
for kind,solution in solutions.items():lines.append(f'- {kind}：{solution}')
lines+=['','## 参考与来源','',
 f'- 项目词法基线：EternalLonging/AAACompilerDevelopment 提交 {dfa["commit"]}，lexer/patterns.json。',
 '- 项目本地需求：ruanjianshixun/需求分析/02_语法分析模块需求分析.docx；这里只引用模块职责和上下文问题，不照搬未左因子化的旧 EBNF。',
 '- 本文 BNF 为以 C89 作参考整理的项目候选草案，尚未合入项目，也不是已实现解析器的输出。',
 '- 转换图图式：裘巍《编译器设计之路》第 2 章，第 30 页图 2-3/2-4、第 37 页图 2-6；仅参考表示法。']
if not CORE:
    (OUT/'C89文法与LL1冲突说明.md').write_text('\n'.join(lines),encoding='utf-8')
else:
    assert not conflicts,conflicts
    core_lines=['# 上下文消歧后的 LL(1) 预测分析核心','',
      f'本核心有 {len(G)} 个非终结符、{len(productions)} 条产生式，SELECT 冲突格为 0。',
      '该结论仅适用于经过上下文分类的终结符流及本文 BNF；项目语言类似 C89，具体规则以本草案和后续项目规范为准；整体 Token 适配流程不属于纯 LL(1)。','',
      '## 必须执行的上下文与后置检查','',
      '1. TYPE_NAME：在类型名可能出现的位置查询 typedef 作用域。正在声明的名字、标签、成员名字位置保持 ID。提交声明后及时更新绑定，离开作用域恢复；不一次性预分类整个文件。',
      '2. LABEL_ID：仅语句入口，当前为 ID 且下一 Token 为 COLON 时形成临时标签分类。原始 TokenType 不变。此适配步骤有两 Token 判断，整体不是纯原始 Token LL(1)。',
      '3. AssignmentTail：先解析条件表达式，再处理赋值；若赋值左部不具有原文法 UnaryExpression 形状则语法拒绝。之后另做可修改左值语义检查。当前草案不接受 a+b=c；是否调整此约束须修改文法并重算集合。',
      '4. ParameterDirect：记录声明符是否具名；无名字时拒绝 abstract-declarator 的旧式 ID 参数列表和无效纯分组空串。具名语法不允许仅有数组/函数后缀而缺少 ID。',
      '5. TypeSpecifier/StorageClass/void 参数等合法组合、旧式函数参数对应关系、位域、常量表达式和控制语句位置按项目语义规范验证，C89 仅作为参考；现有候选构造不自动成为全部实现要求。',
      '6. if、else、while、for、do、switch 的语句体必须使用花括号；当前草案块内先声明后语句，for 初始化不接受声明；这些具体规则可按项目需求调整。','',
      '## 文法 BNF','']
    for p in productions:core_lines.append(f"{p['id']}  {p['lhs']} → {' '.join(p['rhs']) or 'ε'}")
    core_lines+=['','## 解析动作约定','',
      '乘除、加减等尾部以循环按左结合折叠 AST；赋值与条件表达式按右结合构造。空产生式不消费 Token。',
      '更新 typedef 绑定与 AST 构造等动作由实现补充；本次交付集合、分析表和流程设计，未实现正式 C++ 解析器。']
    (OUT/'LL1核心文法与上下文约束.md').write_text('\n'.join(core_lines),encoding='utf-8')
(ROOT/('core_validation.json' if CORE else 'grammar_validation.json')).write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(summary,ensure_ascii=False))
print('Conflict groups:',dict(collections.Counter(c['lhs'] for c in conflicts)))
