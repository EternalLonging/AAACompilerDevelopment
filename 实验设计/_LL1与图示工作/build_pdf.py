import json,re,sys,xml.etree.ElementTree as ET
from pathlib import Path
from reportlab.pdfgen.canvas import Canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from pypdf import PdfReader

root=Path(__file__).parent
out=root.parent/'LL1与图示'
manifest=json.loads((root/'diagram_manifest.json').read_text('utf-8'))
dfa=json.loads((root.parent/'_状态转换表工作/dfa_data.json').read_text('utf-8'))
aliases=json.loads((root/'input_set_aliases.json').read_text('utf-8'))
pdfmetrics.registerFont(TTFont('Chinese',r'C:\Windows\Fonts\simhei.ttf'))
font='Chinese'
def txt(c,x,y,text,size=11):
    c.setFont(font,size);c.drawString(x,y,text)
def wrap(text,width,size=11):
    lines=[];line=''
    for ch in text:
        if pdfmetrics.stringWidth(line+ch,font,size)>width:
            lines.append(line);line=ch
        else:line+=ch
    return lines+[line]
def para(c,x,y,text,width,size=11):
    for line in wrap(text,width,size):txt(c,x,y,line,size);y-=size*1.55
    return y-6
def footer(c,w,page):txt(c,36,22,f'AAACompilerDevelopment  |  2026-10-09  |  {page}',9)
def heading(c,w,h,title,page):
    c.setPageSize((w,h));txt(c,36,h-38,title,17);footer(c,w,page)
def draw_svg(c,file,xbase,ybase,width,height):
    svg=ET.parse(file).getroot();group=list(svg)[0]
    tr=group.attrib['transform'];tx,ty=map(float,re.search(r'translate\(([-\d.]+) ([-\d.]+)\)',tr).groups())
    c.saveState();c.translate(xbase+tx,ybase+height-ty)
    for element in group.iter():
        tag=element.tag.split('}')[-1];a=element.attrib
        if tag not in ('ellipse','polygon','path','text'):continue
        if tag=='text':
            text=''.join(element.itertext());size=float(a.get('font-size',12));f=font if any(ord(ch)>127 for ch in text) else 'Helvetica'
            c.setFont(f,size);x=float(a['x']);y=-float(a['y']);anchor=a.get('text-anchor')
            if anchor=='middle':c.drawCentredString(x,y,text)
            elif anchor=='end':c.drawRightString(x,y,text)
            else:c.drawString(x,y,text)
            continue
        fill=a.get('fill','none');stroke=a.get('stroke','none')
        c.setFillColorRGB(1,1,1) if fill=='white' else c.setFillColorRGB(0,0,0)
        c.setStrokeColorRGB(0,0,0);c.setLineWidth(float(a.get('stroke-width',1)))
        c.setDash([float(v) for v in a.get('stroke-dasharray','').split(',') if v] or [])
        if tag=='ellipse':
            cx,cy,rx,ry=(float(a[k]) for k in ('cx','cy','rx','ry'))
            c.ellipse(cx-rx,-cy-ry,cx+rx,-cy+ry,stroke=int(stroke!='none'),fill=int(fill!='none'))
        else:
            p=c.beginPath()
            if tag=='polygon':
                pts=[tuple(map(float,item.split(','))) for item in a['points'].split()]
                p.moveTo(pts[0][0],-pts[0][1])
                for x,y in pts[1:]:p.lineTo(x,-y)
                p.close()
            else:
                parts=re.findall(r'[A-Za-z]|[-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?',a['d']);i=0;cmd=None
                while i<len(parts):
                    if parts[i].isalpha():cmd=parts[i];i+=1
                    if cmd=='M':x,y=map(float,parts[i:i+2]);i+=2;p.moveTo(x,-y);cmd='L'
                    elif cmd=='L':x,y=map(float,parts[i:i+2]);i+=2;p.lineTo(x,-y)
                    elif cmd=='C':v=list(map(float,parts[i:i+6]));i+=6;p.curveTo(v[0],-v[1],v[2],-v[3],v[4],-v[5])
                    elif cmd in ('Z','z'):p.close();cmd=None
                    else:raise ValueError((cmd,parts))
            c.drawPath(p,stroke=int(stroke!='none'),fill=int(fill!='none'))
    c.restoreState()

def hex_ranges(values):
    parts=[];i=0
    while i<len(values):
        j=i
        while j+1<len(values) and values[j+1]==values[j]+1:j+=1
        parts.append(f'{values[i]:02X}'+(f'-{values[j]:02X}' if j>i else ''));i=j+1
    return ','.join(parts)

if '--flow-only' not in sys.argv:
    statepath=out/'词法状态转换图.pdf'
    c=Canvas(str(statepath));page=1
    c.setTitle('AAACompilerDevelopment 词法状态转换图')
    heading(c,595,842,'词法状态转换图：读图说明',page)
    y=770
    for t in [
     '依据现有词法状态表生成：普通 DFA 92 状态、48 输入字节类，另有专用头文件 DFA 7 状态、5 输入类。状态编号与现有 Excel 完全一致，S 为普通状态，H 为头文件状态。',
     '图形约定参考裘巍《编译器设计之路》第二章：印刷页 30 图 2-3 / 图 2-4，印刷页 37 图 2-6，以及页 32、38 的转换表和扫描说明。参考其画法与流程，语言规则依据本项目。',
     '圆圈表示状态，双圈表示存在接受规则；小点引出箭头指向起始状态。分图中的虚线圆圈是其他分图的边界目标，其后续转移请按同一状态编号查相应分图。双圈与虚线可同时出现。',
     'C00-C47 对应现有 Excel 的输入字符分类。Kxx 是较长输入类集合的简称，完整展开见本图册的集合表。头文件图中的 C00-C04 使用独立的头文件类别，不能套用普通字符表。',
     '每条边消费一个输入字节。合并标签表示多个互斥类别到达相同目标，范围端点均包含。未画出的类别等价于转换表的 -1；EOF 是扫描终止条件，不是额外字节边。',
     '扫描器保留最后一次接受位置，继续尝试更长匹配，无法转移时回到最后接受位置并执行该规则。接受规则可能输出 Token、跳过空白/注释，或报告错误。ID 接受后再查询 32 个关键字。',
     '本项目采用接受状态与最长匹配记录；书中部分图使用非匹配字符进入终态并回退。这里不添加虚构的“其他字符”边，确保与 Excel 的每个有效转移相同。',
     '普通图分为起始分派、标识符、数值、字符串、字符、注释、运算符和标点。全部分图的边合并后恰好覆盖普通转换表所有非 -1 单元格；完整总图另以 SVG 提供，便于放大。',
     '词法来源为项目固定提交 589ac50d8602fc926577316e5b6514fba5e242a3 的 lexer/patterns.json。这是已核实快照，不宣称已取得最新仓库提交或实现了正式 C++ 扫描器。'
    ]:y=para(c,36,y,t,523,11)
    c.showPage();page+=1
    for chunk in range(2):
        heading(c,595,842,f'普通 DFA 输入字符分类（{chunk+1}/2）',page);y=770
        for i,values in list(enumerate(dfa['ordinary']['classes']))[chunk*24:(chunk+1)*24]:
            ascii_text=''.join(chr(b) for b in values if 33<=b<=126)
            desc=f'C{i:02d}  字节 HEX: {hex_ranges(values)}'+(f'    可见字符: {ascii_text}' if ascii_text else '')
            y=para(c,36,y,desc,523,10)
        c.showPage();page+=1
    heading(c,595,842,'输入类集合简称与头文件类别',page);y=770
    for item in aliases:y=para(c,36,y,item['id']+' = '+item['classes'],523,10)
    y-=12
    for i,values in enumerate(dfa['header']['classes']):y=para(c,36,y,f'头文件 C{i:02d}: HEX {hex_ranges(values)}',523,10)
    assert y>36,('alias page overflow',y)
    c.showPage();page+=1
    pages=[]
    for m in manifest:
        if m['type']!='dfa':continue
        d=dfa[m['automaton']];rules=dfa['header_rules'] if m['automaton']=='header' else dfa['rules'];prefix='H' if m['automaton']=='header' else 'S'
        accepted=[(s,rules[d['accept'][s]]) for s in sorted(m['sources']) if d['accept'][s]>=0]
        legend_lines=[f'{prefix}{s}: {r["name"]}  [{r.get("action","emit")}]'+(f' -> {r["token"]}' if r.get('token') else '') for s,r in accepted]
        w=max(595,m['width']+72);h=max(842,m['height']+135+len(legend_lines)*15)
        heading(c,w,h,m['title'],page)
        txt(c,36,h-60,'边标签为输入类别；双圈为接受规则；虚线圈为跨图目标。',10)
        ybase=h-80-m['height'];draw_svg(c,out/'矢量图'/f'{m["name"]}.svg',(w-m['width'])/2,ybase,m['width'],m['height'])
        y=ybase-18
        for line in legend_lines:txt(c,36,y,line,10);y-=15
        assert y>=35,(m['name'],y)
        c.showPage();pages.append({'page':page,'name':m['name'],'size':[w,h]});page+=1
    c.save()
flowpath=out/'语法分析流程图.pdf'
c=Canvas(str(flowpath));c.setTitle('类 C89 项目语言的 LL(1) 语法分析流程')
for page,m in enumerate([m for m in manifest if m['type']=='flow'],1):
    w=max(595,m['width']+72);notes=m['notes'];notes_height=sum(len(wrap(t,w-72,11))*17+6 for t in notes)
    h=max(842,m['height']+170+notes_height)
    heading(c,w,h,m['title'],page)
    txt(c,36,h-61,'椭圆：开始 / 结束；矩形：处理；菱形：条件判断；箭头标签：分支。',11)
    ybase=h-80-m['height'];draw_svg(c,out/'矢量图'/f'{m["name"]}.svg',(w-m['width'])/2,ybase,m['width'],m['height'])
    y=ybase-17
    for t in notes:y=para(c,36,y,t,w-72,11)
    assert y>35
    c.showPage()
c.save()
report={'state_pdf_pages':len(PdfReader(out/'词法状态转换图.pdf').pages),'flow_pdf_pages':len(PdfReader(flowpath).pages),'state_diagram_pages':pages if '--flow-only' not in sys.argv else json.loads((root/'pdf_validation.json').read_text('utf-8'))['state_diagram_pages'],'vector_paths':True}
(root/'pdf_validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf-8')
for old in ['D01_起始分派上','D02_起始分派下','D07_整数与数值错误']:
    for ext in ['svg','dot']:(out/'矢量图'/f'{old}.{ext}').unlink(missing_ok=True)
print(json.dumps(report,ensure_ascii=False))
