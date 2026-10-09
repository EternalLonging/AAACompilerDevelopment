import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Workbook, SpreadsheetFile } from '@oai/artifact-tool';

const workDir = path.dirname(fileURLToPath(import.meta.url));
const data = JSON.parse(await fs.readFile(path.join(workDir, 'dfa_data.json'), 'utf8'));
const wb = Workbook.create();
const main = wb.worksheets.add('普通词法状态转换表');
const classes = wb.worksheets.add('输入字符分类');
const rules = wb.worksheets.add('规则与关键字');
const context = wb.worksheets.add('头文件上下文');
const navy = '#203864', gray = '#EDF1F7', ink = '#203047';
function col(n) { let s = ''; for (n++; n > 0; n = Math.floor((n-1)/26)) s = String.fromCharCode(65+(n-1)%26)+s; return s; }
function range(sh, r, c, nr, nc) { return sh.getRangeByIndexes(r-1, c-1, nr, nc); }
function base(sh, rows, cols) {
  sh.showGridLines = false;
  const all = range(sh, 1, 1, rows, cols);
  all.format.font = { name:'Arial', size:11, color:ink };
  all.format.rowHeight = 25;
  all.format.verticalAlignment = 'center';
  all.format.columnWidth = 12;
  sh.tabColor = navy;
}
function title(sh, text) {
  sh.getRange('A2').values = [[text]];
  sh.getRange('A2').format.font = { name:'Arial', size:16, bold:true, color:navy };
  sh.getRange('A2').format.rowHeight = 31;
}
function header(sh, row, labels, name, lastRow) {
  const rg = range(sh, row, 1, 1, labels.length);
  rg.values = [labels];
  rg.format.fill = navy;
  rg.format.font = { name:'Arial', size:11, bold:true, color:'#FFFFFF' };
  rg.format.horizontalAlignment = 'center';
  rg.format.wrapText = true;
  rg.format.rowHeight = 38;
  rg.format.borders = { insideVertical:{style:'thin', color:'#FFFFFF'} };
  const table = sh.tables.add(`A${row}:${col(labels.length-1)}${lastRow}`, true, name);
  table.showFilterButton = true;
  for (let r = row+1; r <= lastRow; r++) {
    if ((r-row)%2 === 0) range(sh,r,1,1,labels.length).format.fill = '#F4F6FA';
  }
}
function setWidths(sh, widths, rows) {
  widths.forEach((w,i) => range(sh,1,i+1,rows,1).format.columnWidth = w);
}
function shown(s) { return s; }
function writeTextMatrix(sh,r,c,matrix) {
  const rg=range(sh,r,c,matrix.length,matrix[0].length);
  rg.setNumberFormat('@');
  rg.values=matrix;
  matrix.forEach((row,i)=>row.forEach((v,j)=>{
    if (typeof v==='string' && (v.startsWith('=') || /^[+-]?(?:\d|\.\d)/.test(v))) {
      range(sh,r+i,c+j,1,1).formulas=[['="'+v.replaceAll('"','""')+'"']];
    }
  }));
}
function action(rule) { return ({token:'输出',skip:'跳过',error:'诊断'})[rule?.action] || '非接受'; }
function output(rule) { return rule?.token || rule?.code || (rule?.action === 'skip' ? '更新位置' : rule?.action === 'header_name' ? 'header_name' : '—'); }
function stateRows(dfa, ruleList) {
  return dfa.matrix.map((row,s) => {
    const idx = dfa.accept[s];
    const rule = idx >= 0 ? ruleList[idx] : null;
    return [s,rule?.name || '—', rule?.action === 'header_name' ? '头文件名' : action(rule),
            output(rule),shown(dfa.witnesses[s] || '空前缀'),...row];
  });
}
const mainRows = stateRows(data.ordinary,data.rules);
const mainCols = 5 + data.ordinary.classes.length;
base(main, mainRows.length+8, mainCols);
title(main,'普通词法 DFA 状态转换表');
main.getRange('A3').values = [[`C89 与项目 // 注释扩展。${data.ordinary.state_count} 个状态，${data.ordinary.classes.length} 类输入，64 条普通规则。初态为 0。`]];
main.getRange('A4').values = [['转移列 C00–C47 对应“输入字符分类”。单元格为下一状态编号，-1 表示无转移，不消费当前字节。']];
main.getRange('A5').values = [['接受态仍继续扫描。无法转移或遇 EOF 时，回到最后接受位置并执行该动作；同长按规则序号优先。']];
main.getRange('A6').values = [['ID 接受后查 32 关键字；跳过和诊断也更新位置。扫描结束单独追加 END_OF_FILE，EOF 不属于 256 字节输入。']];
main.getRange('A7').values = [['依据仓库正则推导并最小化的设计表；状态编号为本次生成编号，仓库尚未实现正式 C++ DFA 扫描器。']];
main.getRange('A7').format.font.color = '#8A4B10';
header(main,8,['状态','接受规则','动作','Token / 诊断代码','最短到达前缀（转义显示）',...data.ordinary.classes.map((_,i)=>`C${String(i).padStart(2,'0')}`)],'OrdinaryDfa',mainRows.length+8);
writeTextMatrix(main,9,1,mainRows);
setWidths(main,[9,31,11,35,26,...data.ordinary.classes.map(()=>8.5)],mainRows.length+8);
range(main,9,6,mainRows.length,data.ordinary.classes.length).setNumberFormat('0');
range(main,9,6,mainRows.length,data.ordinary.classes.length).format.horizontalAlignment = 'right';
range(main,9,1,mainRows.length,1).setNumberFormat('0');
range(main,9,6,mainRows.length,data.ordinary.classes.length).conditionalFormats.add('cellIs',{operator:'equal',formula:-1,format:{font:{color:'#A6ACB6'}}});
range(main,9,3,mainRows.length,1).conditionalFormats.add('containsText',{text:'诊断',format:{fill:'#FCE8E6',font:{color:'#9C2B28'}}});
range(main,9,3,mainRows.length,1).conditionalFormats.add('containsText',{text:'跳过',format:{fill:'#E8F0ED',font:{color:'#35624F'}}});
main.freezePanes.freezeRows(8);
main.freezePanes.freezeColumns(5);

const escapeByte = b => ({0:'NUL',9:'\\t',10:'\\n',11:'\\v',12:'\\f',13:'\\r',32:'空格',34:'双引号',39:'单引号',92:'反斜杠'})[b] || (b>=33 && b<=126 ? String.fromCharCode(b) : `0x${b.toString(16).toUpperCase().padStart(2,'0')}`);
function compress(bytes,hex=false) {
  let parts=[];
  for (let i=0;i<bytes.length;i++) {
    const a=bytes[i]; let b=a;
    while (i+1<bytes.length && bytes[i+1]===b+1) b=bytes[++i];
    const show = n => hex ? `0x${n.toString(16).toUpperCase().padStart(2,'0')}` : `${n}`;
    parts.push(a===b?show(a):`${show(a)}–${show(b)}`);
  }
  return parts.join(', ');
}
const classRows = data.ordinary.classes.map((bytes,i)=>[
  `C${String(i).padStart(2,'0')}`, shown(compress(bytes,true)),shown(compress(bytes)),bytes.length,
  bytes.length<=18 ? shown(bytes.map(escapeByte).join('  ')) : '控制字节及其他字节，具体范围见十六进制集合',
  bytes.some(b=>b>=128)?'含 UTF-8 非 ASCII 字节；它们仅可作为字面量或注释内容等被识别':'ASCII 字节'
]);
base(classes,classRows.length+7,6);
title(classes,'输入字符分类');
classes.getRange('A3').values = [['按所有 DFA 状态的转移向量合并字节。同一类别内，每个字节在任何状态上都转移到相同目标。']];
classes.getRange('A4').values = [['48 类互斥且合计覆盖 0–255；这里的分类不是泛化的“字母/数字”，例如 L、e/E、p/P 会因规则不同分开。']];
classes.getRange('A5').values = [['UTF-8 按字节扫描；非 ASCII 标识符不属于当前 C89 规则。状态表中的最短前缀用反斜杠转义显示控制字节。']];
header(classes,7,['输入类别','十六进制字节集合','十进制字节集合','字节数','字符显示','含义'], 'ByteClasses',classRows.length+7);
writeTextMatrix(classes,8,1,classRows);
setWidths(classes,[12,63,51,10,66,74],classRows.length+7);
classes.freezePanes.freezeRows(7);
classes.getRange('D8:D55').setNumberFormat('0');

base(rules,113,12);
title(rules,'词法规则、关键字与验证示例');
rules.getRange('A3').values = [[`来源：https://github.com/EternalLonging/AAACompilerDevelopment/blob/${data.commit}/lexer/patterns.json`]];
rules.getRange('A4').values = [[`规范说明：https://github.com/EternalLonging/AAACompilerDevelopment/blob/${data.commit}/docs/lexer-regex.md`]];
rules.getRange('A5').values = [[`生成依据：main 提交 ${data.commit}；核对日期：2026-10-08。`]];
rules.getRange('A6').values = [['规则编号从 1 开始，对应 JSON 顺序。先最长匹配，再比较优先级；32 个关键字通过 ID 查表分类，不另建正则。']];
const ruleRows = data.rules.map((r,i)=>[i+1,r.name,action(r),output(r),shown(r.regex),shown(r.literal || '—'),r.description]);
header(rules,8,['序号','规则名','动作','Token / 诊断代码','正则（JSON 解码后）','固定拼写','说明'],'LexicalRules',72);
writeTextMatrix(rules,9,1,ruleRows);
rules.getRange('A9:A72').setNumberFormat('0');
setWidths(rules,[8,31,11,36,92,38,86,4,18,25,26,57],113);
range(rules,9,5,64,1).format.wrapText = true;
range(rules,9,7,64,1).format.wrapText = true;
for (let i=0;i<ruleRows.length;i++) {
  range(rules,9+i,1,1,7).format.rowHeight = Math.max(30, Math.ceil(ruleRows[i][4].length/85)*19+8,Math.ceil(ruleRows[i][6].length/47)*19+8);
}
const keyRows = Object.entries(data.keywords).map(([word,token])=>[word,token]);
rules.getRange('I8:J8').values = [['关键字原文','Token']];
rules.getRange('I8:J8').format = {fill:navy,font:{name:'Arial',size:11,bold:true,color:'#FFFFFF'},horizontalAlignment:'center'};
rules.getRange('I9:J40').values = keyRows;
rules.tables.add('I8:J40',true,'KeywordLookup');
rules.getRange('I44:J44').values = [['正则宏','定义']];
rules.getRange('I44:J44').format = {fill:navy,font:{name:'Arial',size:11,bold:true,color:'#FFFFFF'}};
const macros = Object.entries(data.definitions);
rules.getRangeByIndexes(44,8,macros.length,2).values = macros;
rules.getRangeByIndexes(44,9,macros.length,1).format.wrapText = true;
rules.getRange('J44:J63').format.columnWidth = 51;
rules.getRange('I45:J61').format.rowHeight = 43;
// Example rows use the existing wide source columns to preserve each spelling.
rules.getRange('E76').values = [['代表性识别结果（不含自动追加的 EOF）']];
rules.getRange('E76').format.font.bold = true;
rules.getRange('E77').values = [['DFA 通过项目 17 组检查；3000 组随机输入与项目字节正则扫描器一致。']];
rules.getRange('E79:G79').values = [['输入源码（换行显示为 \\n）','Token 序列','诊断代码']];
rules.getRange('E79:G79').format = {fill:navy,font:{name:'Arial',size:11,bold:true,color:'#FFFFFF'},wrapText:true,rowHeight:42};
writeTextMatrix(rules,80,5,data.examples.map(e=>[shown(e.source),e.tokens,e.errors]));
range(rules,80,6,data.examples.length,1).format.wrapText = true;
range(rules,80,5,data.examples.length,3).format.rowHeight = 60;
rules.freezePanes.freezeRows(8);

const headRows = stateRows(data.header,data.header_rules);
base(context,43,10);
title(context,'#include 头文件上下文状态转换表');
context.getRange('A3').values = [['仅在 #include 头文件上下文启用，初态为 0。两条规则不加入普通词法 DFA，避免把 a<b>c 当成头文件。']];
context.getRange('A4').values = [['头文件名必须闭合且内容非空；反斜杠是原文字节，不按 C 字符串转义处理。-1 表示无转移。']];
context.getRange('A5').values = [['头文件识别需完整消费参数并达到接受态；宏展开、文件搜索及失败诊断由预处理上下文处理。']];
header(context,7,['状态','接受规则','动作','输出','最短到达前缀',...data.header.classes.map((_,i)=>`H${String(i).padStart(2,'0')}`)],'HeaderDfa',14);
writeTextMatrix(context,8,1,headRows);
context.getRange('A8:A14').setNumberFormat('0');
setWidths(context,[10,30,13,25,52,12,12,12,12,12],43);
context.getRange('F8:J14').setNumberFormat('0');
context.getRange('A18').values = [['头文件输入字符分类（独立于普通词法 C00–C47）']];
context.getRange('A18').format.font.bold = true;
context.getRange('A20:E20').values = [['类别','十六进制字节集合','字节数','字符显示','说明']];
context.getRange('A20:E20').format = {fill:navy,font:{name:'Arial',size:11,bold:true,color:'#FFFFFF'},wrapText:true,rowHeight:36};
const hclass = data.header.classes.map((bytes,i)=>[
  `H${String(i).padStart(2,'0')}`,shown(compress(bytes,true)),bytes.length,
  bytes.length<8?bytes.map(escapeByte).join('  '):'其余字节',
  bytes.includes(10)?'CR / LF 禁止出现在头文件名内部':bytes.includes(34)?'双引号开始/闭合':bytes.includes(60)?'尖括号开始':bytes.includes(62)?'尖括号闭合':'普通头文件内容，包括反斜杠'
]);
writeTextMatrix(context,21,1,hclass);
context.getRange('C21:C25').setNumberFormat('0');
context.getRange('B21:B25').format.wrapText = true;
context.getRange('A21:E25').format.rowHeight = 54;
context.getRange('A29').values = [['上下文规则']];
context.getRange('A29').format.font.bold = true;
context.getRange('A31:E31').values = [['序号','规则名','动作','正则','说明']];
context.getRange('A31:E31').format = {fill:navy,font:{name:'Arial',size:11,bold:true,color:'#FFFFFF'},wrapText:true,rowHeight:36};
context.getRange('A32:E33').values = data.header_rules.map((r,i)=>[i+1,r.name,r.action,r.regex,r.description]);
context.getRange('D32:E33').format.wrapText = true;
context.getRange('A32:E33').format.rowHeight = 62;
context.getRange('A36').values = [['预处理前置顺序']];
context.getRange('A36').format.font.bold = true;
context.getRange('A37').values = [['1. 规范源字符及 C89 三字符组，保留原文位置映射。']];
context.getRange('A38').values = [['2. 移除反斜杠紧接换行的续行，保留映射。']];
context.getRange('A39').values = [['3. 按上下文处理指令、头文件名、宏和条件编译，再分类普通 C 单词。']];
context.getRange('A41').values = [['EOF 不作为表中一列；未闭合头文件不产生合法 header_name。预处理实现仍待项目开发。']];
context.freezePanes.freezeRows(7);

wb.recalculate();
console.log((await wb.inspect({kind:'table',range:'普通词法状态转换表!A8:H13',include:'values,formulas',tableMaxRows:6,tableMaxCols:8,maxChars:3500})).ndjson);
console.log((await wb.inspect({kind:'match',searchTerm:'#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A|#NUM!|#NULL!|#SPILL!|#CALC!',options:{useRegex:true,maxResults:20},maxChars:1200})).ndjson);
for (const [sheetName,rg,file] of [
  ['普通词法状态转换表','A1:K17','preview_dfa.png'],
  ['输入字符分类','A1:F14','preview_classes.png'],
  ['规则与关键字','A1:G16','preview_rules.png'],
  ['规则与关键字','I8:J22','preview_keywords.png'],
  ['规则与关键字','E76:G85','preview_examples.png'],
  ['头文件上下文','A1:J14','preview_header.png'],
  ['头文件上下文','A18:E33','preview_header_classes.png']
]) {
  const blob = await wb.render({sheetName,range:rg,scale:1.5,format:'png'});
  await fs.writeFile(path.join(workDir,file),new Uint8Array(await blob.arrayBuffer()));
}
const xlsxFile = await SpreadsheetFile.exportXlsx(wb);
const target = path.join(workDir,'..','AAACompilerDevelopment_词法状态转换表.xlsx');
await xlsxFile.save(target);
await fs.rename(target+'.inspect.ndjson',path.join(workDir,'workbook.inspect.ndjson')).catch(e=>{if(e.code!=='ENOENT')throw e;});
console.log(JSON.stringify({output:target,sheets:4,states:data.ordinary.state_count,classes:data.ordinary.classes.length}));
