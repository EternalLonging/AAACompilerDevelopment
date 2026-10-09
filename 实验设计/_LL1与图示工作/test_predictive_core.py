"""Exercise the generated LL(1) core; not a replacement C89 compiler."""
import json
from pathlib import Path
import importlib.util

root=Path(__file__).resolve().parent
core=json.loads((root/'core_grammar_data.json').read_text(encoding='utf-8'))
module_path=root.parent/'_状态转换表工作/项目快照/tools/check_lexer_patterns.py'
spec=importlib.util.spec_from_file_location('scanner',module_path)
scanner=importlib.util.module_from_spec(spec);spec.loader.exec_module(scanner)
grammar=core['grammar']
productions={p['id']:p for p in core['productions']}
table={(r['lhs'],r['lookahead']):r['productions'][0] for r in core['table']}

def unary_shaped(node):
    # Preserve the C89 grammar's UnaryExpression assignment-left constraint.
    name=node['symbol'];children=node.get('children',[])
    if name=='ConditionalExpression':
        return not children[1]['children'] and unary_shaped(children[0])
    if name.endswith('Expression') and len(children)==2 and children[1]['symbol'].endswith('Tail'):
        return not children[1]['children'] and unary_shaped(children[0])
    if name=='CastExpression':
        if children[0]['symbol']=='NonParenUnary':return True
        tail=children[1]
        return tail['children'][0]['symbol']=='Expression'
    return False

def parse(source,type_indices=()):
    tokens,errors,_=scanner.scan(source)
    if errors:return False,'词法错误'
    # TYPE_NAME positions are explicit fixtures, not a claimed typedef resolver.
    for i in type_indices:
        assert tokens[i][0]=='ID'
        tokens[i]=('TYPE_NAME',tokens[i][1])
    tree={'symbol':'TranslationUnit'}
    stack=[{'symbol':'END_OF_FILE'},tree]
    pos=0;selected=[];pending_label=False
    for steps in range(50000):
        if not stack:break
        node=stack.pop();symbol=node['symbol']
        token=tokens[pos][0]
        if symbol in ['Statement','LabeledStatement'] and token=='ID' and pos+1<len(tokens) and tokens[pos+1][0]=='COLON':
            pending_label=True
        if pending_label:token='LABEL_ID'
        if symbol in grammar:
            pid=table.get((symbol,token))
            if pid is None:return False,f'无预测表项：{symbol}, {token}'
            selected.append(pid)
            children=[{'symbol':s} for s in productions[pid]['rhs']]
            node['children']=children
            stack.extend(reversed(children))
        else:
            if symbol!=token:return False,f'终结符不匹配：{symbol}, {token}'
            node['lexeme']=tokens[pos][1]
            pos+=1;pending_label=False
    else:raise AssertionError('no progress')
    if pos!=len(tokens):return False,'未消费全部输入'
    todo=[tree]
    while todo:
        node=todo.pop();children=node.get('children',[]);todo.extend(children)
        if node['symbol']=='AssignmentExpression' and children[1]['children']:
            if not unary_shaped(children[0]):return False,'赋值左部不符合 C89 UnaryExpression 形状'
    return True,'通过（不含完整语义检查）'

positive=[
 'int main(void){float r;float c;r=1;c=2*3.14159*r;printf("%f",c);return 0;}',
 'int (*f)(int);',
 'int apply(int (*)(int));',
 'int sum(a,b) int a; int b; {return a+b;}',
 'struct S {int a; unsigned b:3;}; struct S s={1,2,};',
 'enum E {A=1,B=2};',
 'int main(void){int x;x=(int)1.2;x=sizeof(int*);return x;}',
 'int main(void){int x;goto done;done: return 0;}',
 'int main(void){int x;while(x){x--;}for(x=0;x<10;x++){if(x){x+=1;}else{x=0;}}return x;}',
 'int main(void){int x;switch(x){case 1: x=2;break;default: x=0;}do{x--;}while(x);return x;}',
 'int f(const char *s,...);',
 'int main(void){int a;int b;int c;a=b=c=1;a=(b?c:a);return a;}',
 'int main(void){char *s;s="a" "b";return 0;}',
 'int main(void){int a[2];int *p;p=&a[0];*p=1;return a[0];}'
]
negative=[
 'int main(void){if(1) return 0;}',
 'int main(void){for(int i=0;i<3;i++){}}',
 'int main(void){int x;x=1;int y;}',
 'enum E{A,B,};',
 'int main(void){int a;int b;a+b=1;}',
 'int main(void){int a;a=;}',
 'int main(void){return 0}',
 'int main(void){if(1){return 0;}else return 1;}'
]
results=[]
for sample,expected in [(s,True) for s in positive]+[(s,False) for s in negative]:
    accepted,reason=parse(sample)
    assert accepted==expected,(sample,accepted,reason)
    results.append({'source':sample,'expected':expected,'accepted':accepted,'reason':reason})
typedef_sample='typedef int T; T x;'
accepted,reason=parse(typedef_sample,type_indices=[4])
assert accepted
results.append({'source':typedef_sample,'expected':True,'accepted':True,'reason':'TYPE_NAME 位置由测试明确给定；不宣称实现完整作用域适配器'})
(root/'core_parse_validation.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
print(f'{len(results)} predictive-core fixtures passed; full C89 semantic validation is not implemented.')
