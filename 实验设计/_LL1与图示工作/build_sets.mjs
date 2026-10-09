import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {Workbook,SpreadsheetFile,FileBlob} from '@oai/artifact-tool';
const root=path.dirname(fileURLToPath(import.meta.url));
const core=JSON.parse(await fs.readFile(path.join(root,'core_grammar_data.json'),'utf8'));
const raw=JSON.parse(await fs.readFile(path.join(root,'grammar_data.json'),'utf8'));
const fixtures=JSON.parse(await fs.readFile(path.join(root,'core_parse_validation.json'),'utf8'));
const dfa=JSON.parse(await fs.readFile(path.join(root,'../_状态转换表工作/dfa_data.json'),'utf8'));
const scopeEdits=[
 ['使用说明','A2','类 C89 项目语言：文法、集合与 LL(1) 设计'],
 ['使用说明','B8','类似 C89 的自定义 C 类语言，控制体采用花括号'],
 ['使用说明','C8','C89 仅作参考；具体功能、文法与语义由项目定义'],
 ['使用说明','A11','参考文法候选'],
 ['使用说明','C11','候选中的旧式函数、复杂声明符等不自动成为必须实现的功能'],
 ['使用说明','C14','当前草案要求 UnaryExpression 左部形状及可修改左值；可按项目规范调整'],
 ['使用说明','C15','记录是否具名；抽象声明符不得误用旧式 ID 参数列表'],
 ['使用说明','A16','当前草案限制'],
 ['使用说明','A17','控制体'],
 ['使用说明','C23','功能范围按项目需求确认；此文件描述现有候选草案'],
 ['C89原始集合','A2','以 C89 为参考的项目候选 FIRST 与 FOLLOW'],
 ['C89原始集合','A3','当前候选保留参考赋值左部结构；存在 SELECT 冲突，不能直接作为无冲突 LL(1) 表。'],
 ['C89原始SELECT','A2','以 C89 为参考的项目候选 BNF 与 SELECT'],
 ['终结符映射','A3','HASH / HASH_HASH 不进入当前普通 Token 文法。32 个关键字在候选中使用，范围以项目规范为准。'],
 ...['FIRST集','FOLLOW集','SELECT集','核心文法'].map(n=>[n,'A3','项目当前草案的上下文 LL(1) 核心；TYPE_NAME / LABEL_ID 与守卫见“使用说明”。'])
];
function applyScope(w){for(const [s,a,v] of scopeEdits)w.worksheets.getItem(s).getRange(a).values=[[v]];}
if(process.argv.includes('--scope-edit')){
 const target=path.join(root,'../LL1与图示/C89_LL1集合与预测分析表.xlsx');
 const edit=await SpreadsheetFile.importXlsx(await FileBlob.load(target));
 applyScope(edit);edit.recalculate();
 console.log((await edit.inspect({kind:'match',searchTerm:'#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A|#NUM!|#NULL!',options:{useRegex:true,maxResults:10},maxChars:800})).ndjson);
 for(const [sheetName,range,file] of [['使用说明','A1:C23','scope_usage'],['C89原始集合','A1:D8','scope_raw'],['C89原始SELECT','A1:F8','scope_raw_select'],['终结符映射','A1:D10','scope_tokens'],...['FIRST集','FOLLOW集','SELECT集','核心文法'].map((n,i)=>[n,'A1:'+(['D','C','F','D'][i])+'8','scope_core'+i])]){
  const b=await edit.render({sheetName,range,scale:1.2,format:'png'});await fs.writeFile(path.join(root,file+'.png'),new Uint8Array(await b.arrayBuffer()));
 }
 await(await SpreadsheetFile.exportXlsx(edit)).save(target);
 await fs.rename(target+'.inspect.ndjson',path.join(root,'scope_workbook.inspect.ndjson')).catch(e=>{if(e.code!=='ENOENT')throw e;});
 await fs.writeFile(path.join(root,'scope_edits.json'),JSON.stringify(scopeEdits,null,2));
 process.exit(0);
}
const wb=Workbook.create();
const navy='#203864',ink='#203047';
const set=s=>'{ '+s.join(', ')+' }';
const rhs=p=>p.rhs.join(' ')||'ε';
function col(n){let s='';for(n++;n>0;n=Math.floor((n-1)/26))s=String.fromCharCode(65+(n-1)%26)+s;return s;}
function sheet(name,title,headers,rows,widths,notes=[],options={}){
  const sh=wb.worksheets.add(name),start=notes.length+5,last=start+rows.length;
  sh.showGridLines=false;sh.tabColor=navy;
  const used=sh.getRange(`A1:${col(headers.length-1)}${last}`);
  used.format.font={name:'Arial',size:10,color:ink};used.format.verticalAlignment='center';used.format.rowHeight=24;
  widths.forEach((w,i)=>sh.getRange(`${col(i)}1:${col(i)}${last}`).format.columnWidth=w);
  sh.getRange('A2').values=[[title]];sh.getRange('A2').format.font={name:'Arial',size:15,bold:true,color:navy};
  sh.getRange('A2').format.rowHeight=32;
  notes.forEach((n,i)=>{sh.getRange(`A${3+i}`).values=[[n]];});
  sh.getRange(`A${start}:${col(headers.length-1)}${start}`).values=[headers];
  const h=sh.getRange(`A${start}:${col(headers.length-1)}${start}`);
  h.format={fill:navy,font:{name:'Arial',size:10,color:'#FFFFFF',bold:true},horizontalAlignment:'center',wrapText:true,rowHeight:34};
  sh.getRange(`A${start+1}:${col(headers.length-1)}${last}`).values=rows;
  if(!options.matrix)sh.getRange(`A${start+1}:${col(headers.length-1)}${last}`).format.wrapText=true;
  for(let i=0;i<rows.length;i++){
    let lines=1;
    if(!options.matrix)rows[i].forEach((v,j)=>{
      const text=String(v??'');const chinese=(text.match(/[\u3400-\u9FFF]/g)||[]).length;
      lines=Math.max(lines,Math.ceil((text.length+chinese)/(widths[j]*0.9)));
    });
    const rg=sh.getRange(`A${start+i+1}:${col(headers.length-1)}${start+i+1}`);
    rg.format.rowHeight=Math.max(25,lines*15+12);
    if(i%2)rg.format.fill='#F3F6FA';
  }
  if(options.table!==false)sh.tables.add(`A${start}:${col(headers.length-1)}${last}`,true,options.name||'Table'+wb.worksheets.items.length);
  sh.freezePanes.freezeRows(start);sh.freezePanes.freezeColumns(options.freezeCols||1);
  return {sh,start,last};
}
const summaries=[
 ['语法目标','C89 构造范围，控制体采用花括号','不是不加限制的全部 C89 源程序集合'],
 ['词法依据','普通词法状态表：92 状态、48 输入类','普通语法使用 Token，FIRST/FOLLOW 不使用 C00 等字符类别'],
 ['LL(1) 核心',`${core.summary.nonterminals} 非终结符 / ${core.summary.productions} 产生式 / 0 冲突格`,'仅对 TYPE_NAME / LABEL_ID 上下文分类后的 Token 流成立'],
 ['C89 原始候选',`${raw.summary.nonterminals} 非终结符 / ${raw.summary.productions} 产生式 / ${raw.conflicts.length} 冲突格`,'原始候选 SELECT 冲突完整保留在后部表中'],
 ['类型名分类','TYPE_NAME：查询 typedef 作用域','原词法 TokenType 为 ID；声明名、成员名、标签名位置保持 ID'],
 ['标签分类','LABEL_ID：语句入口 ID 后紧跟 COLON','适配器需要两个 Token 判断；整体不是原始 Token 流纯 LL(1)'],
 ['赋值约束','AssignmentTail 按右结合展开','赋值左部必须符合原 C89 UnaryExpression 形状，并另做可修改左值检查'],
 ['声明符约束','参数采用具名/抽象统一前缀结构','记录是否具名；抽象声明符不得误用旧式 ID 参数列表'],
 ['C89 与 C99','块内先声明后语句，for 初始化只用表达式','无 VLA、复合字面量、指定初始化器、C99 新关键字；枚举无尾逗号'],
 ['控制体','if / else / while / for / do / switch 统一用 CompoundStatement','已由用户确认；消除悬空 else 冲突'],
 ['起始与终止','起始符 TranslationUnit；FOLLOW 起始包含 END_OF_FILE','EOF 不重复写入起始产生式右侧；严格语法不接受空翻译单元'],
 ['SELECT 算法','FIRST(右部) 去掉 ε；可空时再并入 FOLLOW(左部)','同一左部 SELECT 两两不交才是 LL(1)'],
 ['预测表符号','Pxxx 为该版本产生式号；— 表示语法错误格','核心和原始候选的 Pxxx 编号属于各自版本，不能交叉引用'],
 ['恢复策略','诊断后使用 FOLLOW 与上下文同步点；每步必须消费 Token 或弹栈','发生语法错误时按项目合同不交付正常可执行 AST'],
 ['本次验证',`${fixtures.length} 个预测核心用例；可达、可生成、无左递归检查`,'typedef 用例给定类型名位置；不宣称实现完整作用域适配或语义分析'],
 ['项目实现状态','本次为集合、文法、预测表与图示设计','正式 C++ 语法分析器尚未实现，本文件不是完成编译器的运行输出']
];
const usage=sheet('使用说明','C89 文法、集合与 LL(1) 预测分析设计',['项目','结果或规则','范围与约束'],summaries,[22,72,96],[
 '2026-10-09。先看约束，再使用 FIRST / FOLLOW / SELECT 和预测分析表。',
 `来源：项目提交 ${core.commit} 的 lexer/patterns.json；本次新增 BNF 见配套文法说明。`
],{table:false});
usage.sh.getRange(`A${usage.start+1}:A${usage.last}`).format.font.bold=true;
const common=['以下为上下文消歧后的 LL(1) 核心；TYPE_NAME / LABEL_ID 与后置检查见“使用说明”。'];
const nts=Object.keys(core.grammar);
const first=sheet('FIRST集','LL(1) 核心 FIRST 集',['非终结符','FIRST','可推导 ε','模块'],nts.map(n=>[n,set(core.first[n]),core.nullable.includes(n)?'是':'否',core.sections[n]]),[34,112,14,50],common,{name:'CoreFirst'});
const follow=sheet('FOLLOW集','LL(1) 核心 FOLLOW 集',['非终结符','FOLLOW','模块'],nts.map(n=>[n,set(core.follow[n]),core.sections[n]]),[34,140,50],common,{name:'CoreFollow'});
const select=sheet('SELECT集','LL(1) 核心逐产生式 SELECT 集',['产生式号','左部','右部','FIRST(右部)','SELECT','取集依据'],core.productions.map(p=>[p.id,p.lhs,rhs(p),set(p.first_rhs),set(p.select),p.nullable?'FIRST(右部) − {ε} ∪ FOLLOW(左部)':'FIRST(右部)']),[12,34,96,112,112,37],common,{name:'CoreSelect',freezeCols:2});
const grammar=sheet('核心文法','LL(1) 核心 BNF',['产生式号','左部','右部','模块'],core.productions.map(p=>[p.id,p.lhs,rhs(p),p.section]),[12,34,112,55],common,{name:'CoreGrammar',freezeCols:2});
const terms=core.terminals;
const tm=new Map(core.table.map(r=>[r.lhs+'|'+r.lookahead,r.productions]));
const matrix=sheet('预测分析表','LL(1) 核心预测分析表 M[A,a]',['非终结符',...terms],nts.map(n=>[n,...terms.map(t=>tm.get(n+'|'+t)?.join(' / ')||'—')]),[34,...terms.map(t=>Math.max(14,t.length*1.05+3))],['每个非空格仅一个产生式号；— 为语法错误格。EPSILON 不作为输入列。'],{name:'CorePrediction',matrix:true});
const rawsets=sheet('C89原始集合','C89 原始候选 FIRST 与 FOLLOW',['非终结符','FIRST','FOLLOW','可推导 ε'],Object.keys(raw.grammar).map(n=>[n,set(raw.first[n]),set(raw.follow[n]),raw.nullable.includes(n)?'是':'否']),[35,113,140,13],['保留标准赋值左部等候选结构。存在 SELECT 冲突，不能按无冲突 LL(1) 表直接实现。'],{name:'RawSets'});
const rawselect=sheet('C89原始SELECT','C89 原始候选 BNF 与 SELECT',['产生式号','左部','右部','FIRST(右部)','SELECT','涉及冲突'],raw.productions.map(p=>[p.id,p.lhs,rhs(p),set(p.first_rhs),set(p.select),raw.conflicts.some(c=>c.productions.includes(p.id))?'有':'无']),[12,35,98,110,112,15],['同一左部所有 SELECT 均如实保留，没有按规则优先级覆盖。'],{name:'RawSelect',freezeCols:2});
rawselect.sh.getRange(`F${rawselect.start+1}:F${rawselect.last}`).conditionalFormats.add('containsText',{text:'有',format:{fill:'#FCE8E6',font:{color:'#A52828'}}});
const conflicts=sheet('冲突分析','原始候选预测表冲突与处理要求',['左部','向前看 Token','冲突产生式号','分类','处理要求'],raw.conflicts.map(c=>[c.lhs,c.lookahead,c.productions.join(' / '),c.category,c.solution]),[35,27,24,45,130],['原始候选 20 个冲突格；不能用“优先选择第一条”当作已经消除了冲突。'],{name:'RawConflicts'});
const literalMap=new Map(dfa.rules.filter(r=>r.token).map(r=>[r.token,r.literal||r.name]));
Object.entries(dfa.keywords).forEach(([word,token])=>literalMap.set(token,word));
const mapping=sheet('终结符映射','语法终结符与词法接口',['语法终结符','源码或词法规则','来源','说明'],terms.map(t=>[
  t,t==='TYPE_NAME'?'typedef 类型名':t==='LABEL_ID'?'ID 后跟 COLON':t==='END_OF_FILE'?'输入结束':literalMap.get(t)||t,
  ['TYPE_NAME','LABEL_ID'].includes(t)?'语法层临时分类':t==='END_OF_FILE'?'词法扫描结束追加':'项目 TokenType',
  t==='TYPE_NAME'?'按作用域和名字角色查询，原词法枚举仍为 ID':t==='LABEL_ID'?'仅语句入口适配器两 Token 判断，原词法枚举仍为 ID':t==='END_OF_FILE'?'FOLLOW 起始标记与预测分析终止条件':'与现有词法状态表输出名称一致'
]),[32,40,27,93],['HASH / HASH_HASH 由预处理层处理，不进入普通 C89 Token 语法。全部 32 个关键字在文法中使用。'],{name:'TerminalMap'});
// Preserve punctuation as literal output in the mapping table.
for(let i=0;i<terms.length;i++){
  const value=literalMap.get(terms[i]);
  if(value?.startsWith('='))mapping.sh.getRange(`B${mapping.start+1+i}`).formulas=[['="'+value+'"']];
}
applyScope(wb);wb.recalculate();
console.log((await wb.inspect({kind:'match',searchTerm:'#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A|#NUM!|#NULL!|#SPILL!|#CALC!',options:{useRegex:true,maxResults:20},maxChars:1500})).ndjson);
const previews=[['使用说明','A1:C12','sets_usage'],['FIRST集',`A1:D${first.start+7}`,'sets_first'],['FOLLOW集',`A1:C${follow.start+6}`,'sets_follow'],['SELECT集',`A1:F${select.start+4}`,'sets_select'],['核心文法',`A1:D${grammar.start+6}`,'sets_grammar'],['预测分析表',`A1:I${matrix.start+10}`,'sets_matrix'],['C89原始集合',`A1:D${rawsets.start+5}`,'sets_raw'],['C89原始SELECT',`A1:F${rawselect.start+4}`,'sets_raw_select'],['冲突分析',`A1:E${conflicts.start+5}`,'sets_conflicts'],['终结符映射',`A1:D${mapping.start+7}`,'sets_tokens']];
for(const [sheetName,range,file] of previews){if(process.argv.includes('--matrix-only')&&sheetName!=='预测分析表')continue;const b=await wb.render({sheetName,range,scale:1.2,format:'png'});await fs.writeFile(path.join(root,file+'.png'),new Uint8Array(await b.arrayBuffer()));}
const target=path.join(root,'../LL1与图示/C89_LL1集合与预测分析表.xlsx');
await(await SpreadsheetFile.exportXlsx(wb)).save(target);
await fs.rename(target+'.inspect.ndjson',path.join(root,'sets_workbook.inspect.ndjson')).catch(e=>{if(e.code!=='ENOENT')throw e;});
const locations={};
for(const s of [first,follow,select,grammar,matrix,rawsets,rawselect,conflicts,mapping])locations[s.sh.name]={header:s.start,last:s.last};
await fs.writeFile(path.join(root,'sheet_locations.json'),JSON.stringify(locations,null,2));
console.log(JSON.stringify({output:target,sheets:10,core:core.summary,raw:raw.summary}));
