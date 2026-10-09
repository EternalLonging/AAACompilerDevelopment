import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {instance} from '@viz-js/viz';
import sharp from 'sharp';
const root=path.dirname(fileURLToPath(import.meta.url));
const out=path.join(root,'../LL1与图示/矢量图');
await fs.mkdir(out,{recursive:true});
const dfa=JSON.parse(await fs.readFile(path.join(root,'../_状态转换表工作/dfa_data.json'),'utf8'));
const viz=await instance();
const q=JSON.stringify;
const manifest=[];
const aliases=new Map();
function compact(a){let parts=[];for(let i=0;i<a.length;i++){let j=i;while(j+1<a.length&&a[j+1]===a[j]+1)j++;parts.push('C'+String(a[i]).padStart(2,'0')+(j>i?'-C'+String(a[j]).padStart(2,'0'):''));i=j;}return parts.join(', ');}
function grouped(d,sources,filter=()=>true){const edges=[];for(const s of sources){const dest=new Map();d.matrix[s].forEach((t,c)=>{if(t>=0&&filter(c)){if(!dest.has(t))dest.set(t,[]);dest.get(t).push(c);}});for(const [t,classes] of dest)edges.push({s,t,classes});}return edges;}
async function save(name,title,dot,extra={}){
 if(extra.type==='flow')dot=dot.replace(/(\w+) \[label="((?:[^"\\]|\\.)*)"([^\]]*)\]/g,(all,id,label,attrs)=>{
 const max=Math.max(...label.split('\\n').map(line=>[...line].reduce((w,ch)=>w+(/[\u3400-\u9fff]/.test(ch)?12:7),0)));
 const factor=attrs.includes('diamond')?1.85:attrs.includes('oval')?1.4:1;
 return `${id} [label="${label}"${attrs},width=${((max+30)*factor/72).toFixed(2)}]`;
 });
 await fs.writeFile(path.join(out,name+'.dot'),dot);
 const svg=viz.renderString(dot,{format:'svg',engine:'dot'});
 await fs.writeFile(path.join(out,name+'.svg'),svg);
 const match=svg.match(/viewBox="0.00 0.00 ([\d.]+) ([\d.]+)"/);
 const width=Number(match[1]),height=Number(match[2]);
 await sharp(Buffer.from(svg),{density:130}).resize({width:Math.min(2200,Math.round(width*1.8)),withoutEnlargement:true}).png().toFile(path.join(root,name+'.png'));
 manifest.push({name,title,width,height,...extra});
}
const prefix='digraph G { graph [rankdir=LR, bgcolor="white", nodesep=0.3, ranksep=0.65, pad=0.25]; node [fontname="Arial", fontsize=12, shape=circle, width=0.55]; edge [fontname="Arial", fontsize=11, arrowsize=0.65];';
async function automaton(name,title,d,rules,sources,filter=()=>true,full=false){
 const edges=grouped(d,sources,filter),own=new Set(sources),nodes=new Set(sources);
 for(const e of edges)nodes.add(e.t);
 let dot=prefix;
 for(const n of [...nodes].sort((a,b)=>a-b))dot+=`n${n} [label="${name.startsWith('H')?'H':'S'}${n}",shape=${d.accept[n]>=0?'doublecircle':'circle'},style=${own.has(n)?'solid':'dashed'}];\n`;
 if(own.has(0))dot+='start [shape=point,width=0.04]; start -> n0;\n';
 for(const e of edges){let label=compact(e.classes);if(label.length>22){if(!aliases.has(label))aliases.set(label,'K'+String(aliases.size+1).padStart(2,'0'));label=aliases.get(label);}dot+=`n${e.s} -> n${e.t} [label=${q(label)}];\n`;}
 dot+='}';
 await save(name,title,dot,{type:full?'full':'dfa',automaton:name.startsWith('H')?'header':'ordinary',sources,edges,nodes:[...nodes]});
}
await automaton('D00_完整普通DFA','普通词法完整状态图',dfa.ordinary,dfa.rules,dfa.ordinary.matrix.map((_,i)=>i),()=>true,true);
for(let part=0;part<4;part++)await automaton(`D0${part+1}a_起始分派${part+1}`,`起始分派 C${String(part*12).padStart(2,'0')}-C${String(part*12+11).padStart(2,'0')}`,dfa.ordinary,dfa.rules,[0],c=>c>=part*12&&c<part*12+12);
const groups=[
 ['D03_空白与无效字节','空白、换行与无效字节',[1,2,3,4]],
 ['D04_标识符与宽字符前缀','标识符与宽字符前缀',[27,28]],
 ['D05_字符串','字符串与转义',[6,37,38,73,74,88,89]],
 ['D06_字符字面量','字符字面量与转义',[10,43,44,45,75,76,77,90]],
 ['D07a_整数与数值错误','整数与数值错误',[19,20,57,58,59]],
 ['D07b_整数后缀与十六进制','整数后缀与十六进制',[61,62,63,64,83,84,85]],
 ['D08_浮点数','浮点数与指数',[53,60,79,81,82]],
 ['D09_注释与斜杠','注释与斜杠',[18,54,55,56,80,91]],
 ['D10_运算符一','运算符与预处理符号（一）',[5,7,8,9,13,14,16,36,39,40,41,42,46,47,48,49,50,51]],
 ['D11_运算符二','关系、移位与位运算符',[23,24,25,31,33,35,65,66,67,68,69,70,71,72,86,87]],
 ['D12_标点与省略号','标点与省略号',[11,12,15,17,21,22,26,29,30,32,34,52,78]]
];
for(const [name,title,sources] of groups)await automaton(name,title,dfa.ordinary,dfa.rules,sources);
await automaton('H01_头文件上下文','专用头文件上下文 DFA',dfa.header,dfa.header_rules,dfa.header.matrix.map((_,i)=>i));
const fp='digraph G { graph [rankdir=TB, bgcolor="white", nodesep=0.55, ranksep=0.5, pad=0.25]; node [fontname="Microsoft YaHei",fontsize=12,shape=box,margin="0.15,0.10"]; edge [fontname="Microsoft YaHei",fontsize=11,arrowsize=0.7];';
await save('F01_语法分析总流程','类 C89 项目语言语法分析总流程',fp+`
 start [label="开始",shape=oval]; input [label="接收预处理后的 Token 流\\n保留源码位置，初始化诊断和作用域"];
 valid [label="有词法错误或\\n缺少结束标记？",shape=diamond]; reject [label="返回失败 ParseResult\\n包含诊断，不提供正常可执行 AST",shape=oval];
 parse [label="调用 parseTranslationUnit()\\n按各非终结符的 SELECT 选择分支"];
 context [label="按当前作用域与名字角色分类 TYPE_NAME\\n仅语句入口以 ID + COLON 分类 LABEL_ID"];
 step [label="匹配终结符 / 调用子分析函数\\n维护声明绑定与块作用域\\n检查赋值左部 UnaryExpression 形状\\n检查具名 / 抽象声明符约束"];
 good [label="分支、匹配及\\n必要语法守卫成功？",shape=diamond]; recovery [label="记录位置和预期 Token\\n按 FOLLOW 与上下文同步点恢复\\n保证前进：消费 Token 或退出当前分析层"];
 build [label="构造 AST\\n保持运算优先级与结合性"];
 more [label="翻译单元\\n还有待分析内容？",shape=diamond]; endcheck [label="已到 EOF 且\\n没有语法错误？",shape=diamond]; success [label="返回成功 ParseResult(AST)\\n交给项目语义检查",shape=oval];
 start -> input -> valid; valid -> reject [label="是"]; valid -> parse [label="否"];
 parse -> context -> step -> good;
 good -> build [label="是"]; good -> recovery [label="否"]; recovery -> more; build -> more;
 more -> context [label="是"]; more -> endcheck [label="否"]; endcheck -> success [label="是"]; endcheck -> reject [label="否"];
}`,{type:'flow',notes:['项目语言类似 C89，具体规则由项目定义；规划为 C++17 手写递归下降，此图为设计流程。','上下文分类不可提前一次性完成。完整可修改左值、类型组合、控制位置等由后续语义层检查。','同步点包括 SEMI、RBRACE、END_OF_FILE；错误恢复不得跳过后继续返回成功 AST。']});
await save('F02_预测分析循环','LL(1) 预测表驱动循环（集合验证与教学）',fp+`
 start [label="开始",shape=oval]; init [label="栈底放 EOF，压入 TranslationUnit\\n输入为经过上下文分类的 Token 流"];
 read [label="读取栈顶 X 和当前输入 a"];
 done [label="X = EOF 且 a = EOF？",shape=diamond]; success [label="接受",shape=oval];
 terminal [label="X 是终结符？",shape=diamond]; match [label="X = a？",shape=diamond]; consume [label="弹栈；消费当前 Token"];
 lookup [label="查询 M[X,a]（SELECT 预测表）"]; entry [label="恰有一条产生式？",shape=diamond]; push [label="弹出 X；产生式右部逆序入栈\\nε 不入栈"];
 error [label="报告语法错误或表冲突\\n严格验证模式：拒绝",shape=oval];
 start -> init -> read -> done; done -> success [label="是"]; done -> terminal [label="否"];
 terminal -> match [label="是"]; terminal -> lookup [label="否"];
 match -> consume [label="是"]; match -> error [label="否"]; consume -> read;
 lookup -> entry; entry -> push [label="是"]; entry -> error [label="否"]; push -> read;
}`,{type:'flow',notes:['本图用于解释预测分析表与验证脚本，正式项目采用上一页的递归下降设计。','核心表无多产生式格。TYPE_NAME / LABEL_ID 适配及后置守卫另按配套约束执行。']});
const expected=new Set();dfa.ordinary.matrix.forEach((row,s)=>row.forEach((t,c)=>{if(t>=0)expected.add(`${s}/${c}/${t}`);}));
const actual=new Set();let duplicates=0;
for(const m of manifest.filter(m=>m.type==='dfa'&&m.automaton==='ordinary'))for(const e of m.edges)for(const c of e.classes){const k=`${e.s}/${c}/${e.t}`;if(actual.has(k))duplicates++;actual.add(k);}
if(duplicates||actual.size!==expected.size||[...expected].some(k=>!actual.has(k)))throw new Error('Diagram edge coverage mismatch');
await fs.writeFile(path.join(root,'diagram_manifest.json'),JSON.stringify(manifest,null,2));
await fs.writeFile(path.join(root,'input_set_aliases.json'),JSON.stringify([...aliases].map(([classes,id])=>({id,classes})),null,2));
await fs.writeFile(path.join(root,'diagram_validation.json'),JSON.stringify({ordinary_states:92,ordinary_classes:48,nonfailure_transitions:expected.size,segmented_transition_coverage:actual.size,duplicate_transitions:duplicates,header_states:7,full_svg:true},null,2));
console.log(manifest.map(m=>`${m.name}: ${Math.round(m.width)} x ${Math.round(m.height)} pt`).join('\n'));
