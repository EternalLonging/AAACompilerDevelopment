"""单张词法图的状态位置、共用主干和避障折线。"""
from collections import Counter, defaultdict
import heapq
import xml.etree.ElementTree as ET

POSITIONS = {
    0:(130,235),1:(500,320),2:(500,500),4:(500,680),3:(1050,680),27:(500,850),26:(1600,800),
    6:(500,1050),36:(1000,950),37:(1100,1200),72:(1600,1100),71:(2100,1250),86:(2600,1100),87:(2600,1500),
    9:(500,1900),41:(1050,1850),73:(1600,1750),43:(1100,2200),75:(1600,2200),74:(2100,2100),42:(2600,1900),88:(2600,2350),
    17:(500,2800),54:(1050,2650),53:(1050,2850),52:(1100,3150),78:(1750,3150),89:(2400,3150),
    16:(500,4500),50:(1100,4400),76:(1700,4400),18:(500,5000),19:(500,5650),55:(1100,5100),56:(1650,5200),
    62:(1100,4700),82:(1750,4700),83:(2250,4700),59:(2900,4500),61:(3450,4500),81:(4000,4500),
    51:(2300,5350),58:(3000,5400),79:(3550,5200),80:(4100,5500),77:(4650,5800),57:(2750,6100),60:(4100,6250),
}
for state,y,children in [
    (5,340,[(35,340)]),(7,550,[(38,550)]),(8,820,[(39,730),(40,910)]),(12,1080,[(44,1080)]),
    (13,1340,[(45,1240),(46,1440)]),(15,1680,[(47,1580),(48,1780),(49,1930)]),
    (22,2180,[(63,2080),(64,2280)]),(23,2480,[(65,2480)]),(24,2750,[(67,2650),(66,2850)]),
    (30,3080,[(68,3080)]),(32,3370,[(69,3270),(70,3470)]),
]:
    POSITIONS[state]=(3300,y)
    POSITIONS.update({child:(3950,cy) for child,cy in children})
POSITIONS.update({84:(4650,2080),85:(4650,2650)})
for i,state in enumerate([10,11,14,20,21,25,28,29,31,33,34]):
    POSITIONS[state]=(3300+i%4*500,3750+i//4*180)


def clear_segment(a,b):
    """返回线段是否避开所有状态的圆圈。"""
    for x,y in POSITIONS.values():
        if a[0]==b[0]:
            if x-49<a[0]<x+49 and max(min(a[1],b[1]),y-49)<min(max(a[1],b[1]),y+49):return False
        elif y-49<a[1]<y+49 and max(min(a[0],b[0]),x-49)<min(max(a[0],b[0]),x+49):return False
    return True


class Router:
    """保存避障网格及已有线段，优先复用同一状态的主干。"""
    def __init__(self):
        xs=sorted({230,2900,3000,5050}|{x+d for x,_ in POSITIONS.values() for d in (-110,-70,0,70,100)})
        ys=sorted({180,235,6500}|{y+d for _,y in POSITIONS.values() for d in (-110,-70,0,70,110)})
        self.neighbors=defaultdict(list)  # 存每个网格点可通行的相邻点。
        self.owners=defaultdict(set)  # 存每条线段所属的起始状态。
        self.point_owners=defaultdict(set)  # 存经过每个点的起始状态。
        for i,x in enumerate(xs):
            for j,y in enumerate(ys):
                a=(x,y)
                for b in ((xs[i+1],y) if i+1<len(xs) else None,(x,ys[j+1]) if j+1<len(ys) else None):
                    if b and clear_segment(a,b):
                        direction=int(a[1]!=b[1]);distance=abs(a[0]-b[0])+abs(a[1]-b[1])
                        self.neighbors[a].append((b,direction,distance));self.neighbors[b].append((a,direction,distance))

    def path(self,source,target):
        """返回固定共用出口之后的避障路径。"""
        sx,sy=POSITIONS[source];tx,ty=POSITIONS[target]
        start=(sx+100,sy);end=(tx,ty-70) if source==target else (tx-70,ty)
        prefix=[]
        if source==0:
            prefix=[start,(3000,sy),(3000,ty)] if tx>=3000 else [start,(start[0],ty)]
            start=prefix[-1]
        queue=[(0,0,start,0)];best={(start,0):0};parents={}
        while queue:
            _,cost,point,old_direction=heapq.heappop(queue);key=(point,old_direction)
            if cost!=best.get(key):continue
            if point==end:
                path=[point]
                while key in parents:key=parents[key];path.append(key[0])
                path.reverse()
                for a,b in zip(path,path[1:]):
                    self.owners[tuple(sorted((a,b)))].add(source);self.point_owners[a].add(source);self.point_owners[b].add(source)
                return prefix[:-1]+path if prefix else path
            for nxt,direction,distance in self.neighbors[point]:
                owners=self.owners[tuple(sorted((point,nxt)))]
                factor=.25 if source in owners else 1.35 if owners else 1
                new_cost=cost+distance*factor+(30 if direction!=old_direction else 0)+(22 if self.point_owners[nxt]-{source} else 0)
                new_key=(nxt,direction)
                if new_cost<best.get(new_key,float('inf')):
                    best[new_key]=new_cost;parents[new_key]=(point,old_direction)
                    heuristic=(abs(nxt[0]-end[0])+abs(nxt[1]-end[1]))*.25
                    heapq.heappush(queue,(new_cost+heuristic,new_cost,nxt,direction))
        raise ValueError(f"找不到避障路径: S{source} → S{target}")


def simplify(points):
    """合并同一直线上的路径点，返回折线拐点。"""
    result=[]
    for point in points:
        if len(result)>=2 and (result[-2][0]==result[-1][0]==point[0] or result[-2][1]==result[-1][1]==point[1]):result[-1]=point
        else:result.append(point)
    return result


def generate_single(data,spec,short,text,class_text,category_label):
    """返回全部唯一状态位于同一画布、出边共用主干的 drawio XML。"""
    assert set(POSITIONS)==set(range(data['state_count']))
    file=ET.Element('mxfile',host='app.diagrams.net',type='device')
    diagram=ET.SubElement(file,'diagram',id='single',name='词法状态转换总图')
    model=ET.SubElement(diagram,'mxGraphModel',page='1',pageScale='1',grid='0',pageWidth='5250',pageHeight='8650',background='#ffffff')
    root=ET.SubElement(model,'root');ET.SubElement(root,'mxCell',id='0');ET.SubElement(root,'mxCell',id='1',parent='0')
    text(root,'title','MiniC 词法状态转换总图',430,25,4400,55,36,True,'#172B4D')
    text(root,'summary','90 个状态 · 47 类字节 · 所有有效转移 · 同一状态出边共用主干，分叉后标注输入',430,90,4500,38,23)
    text(root,'legend','双圈：接受状态    绿色：Token    青色：跳过    红色：诊断候选    每条箭头读取一个字节',430,140,4500,38,20)
    for ident,title,x,y in [('a','空白、换行与标识符',700,190),('b','字符串与转义',700,910),('c','字符常量与转义',700,1650),('d','斜杠与注释',700,2500),('e','运算符',3100,190),('f','分隔符',3100,3570),('g','整数、浮点、进制、后缀与数字错误',700,4240)]:
        text(root,'section-'+ident,title,x,y,1900,36,25,True)
    for state,(x,y) in POSITIONS.items():
        index=data['accept'][state];rule=spec['rules'][index] if index>=0 else None
        label=f'S{state}'+('\n'+short.get(rule['name'],rule.get('literal',rule['name'])) if rule else '')
        fill,stroke={'token':('#ECF7EF','#237A44'),'skip':('#E8F6F7','#257E87'),'error':('#FFF0EF','#B74B42')}.get(rule['action'] if rule else '',('#EDF2FC','#41689B'))
        obj=ET.SubElement(root,'object',id=f's{state}',label=label,dfaState=str(state),acceptingRule=rule['name'] if rule else '',action=rule['action'] if rule else '')
        cell=ET.SubElement(obj,'mxCell',vertex='1',parent='1',style=f"shape={'doubleEllipse' if rule else 'ellipse'};html=0;whiteSpace=wrap;aspect=fixed;fontFamily=Microsoft YaHei;fontSize=12;strokeWidth=1.6;fillColor={fill};strokeColor={stroke};")
        ET.SubElement(cell,'mxGeometry',x=str(x-35),y=str(y-35),width='70',height='70',**{'as':'geometry'})
    groups=defaultdict(list)
    for source,row in enumerate(data['matrix']):
        for category,target in enumerate(row):
            if target>=0:groups[source,target].append(category)
    router=Router();routes={};symbols={}
    for source,target in sorted(groups,key=lambda p:(p[0],abs(POSITIONS[p[1]][1]-POSITIONS[p[0]][1]),p[1])):routes[source,target]=router.path(source,target)
    counts=Counter((s,tuple(sorted((a,b)))) for (s,_),p in routes.items() for a,b in zip(p,p[1:]));boxes=[]
    for (source,target),categories in groups.items():
        label=category_label(categories)
        members=sorted({b for c in categories for b in data['classes'][c]})
        if len(members)<=5:label=' '.join({9:r'\t',10:r'\n',13:r'\r',32:'SP'}.get(b,chr(b) if 33<=b<=126 else f'0x{b:02X}') for b in members)
        if len(label)>24:
            key=tuple(categories)
            if key not in symbols:symbols[key]=f'B{len(symbols)+1:02d}'
            label=symbols[key]
        obj=ET.SubElement(root,'object',id=f'e{source}-{target}',label=label,dfaSource=str(source),dfaTarget=str(target),dfaClasses=','.join(map(str,categories)))
        cell=ET.SubElement(obj,'mxCell',edge='1',parent='1',source=f's{source}',target=f's{target}',style='edgeStyle=none;rounded=0;html=0;noLabel=1;endArrow=classic;endFill=1;exitX=1;exitY=0.5;exitPerimeter=0;'+('entryX=0.5;entryY=0;' if source==target else 'entryX=0;entryY=0.5;')+'entryPerimeter=0;strokeColor=#62778B;strokeWidth=1.15;')
        geometry=ET.SubElement(cell,'mxGeometry',relative='1',**{'as':'geometry'});array=ET.SubElement(geometry,'Array',**{'as':'points'});route=routes[source,target]
        for x,y in [route[0]]+simplify(route)[1:]:ET.SubElement(array,'mxPoint',x=str(x),y=str(y))
        width=max(48,min(185,len(label)*9+15));candidates=[]
        label_route=route[(2 if POSITIONS[target][0]>=3000 else 1):] if source==0 else route
        chunks=[]
        for a,b in zip(label_route,label_route[1:]):
            unique=counts[source,tuple(sorted((a,b)))]==1
            if chunks and chunks[-1][2]==unique and (chunks[-1][0][0]==a[0]==b[0] or chunks[-1][0][1]==a[1]==b[1]):
                chunks[-1]=(chunks[-1][0],b,unique)
            else:chunks.append((a,b,unique))
        for a,b,unique in chunks:
            if a[1]==b[1]:candidates.append((unique,abs(a[0]-b[0])>=width,abs(a[0]-b[0]),(a[0]+b[0])/2-width/2,a[1]-26))
            else:
                candidates.append((unique,abs(a[1]-b[1])>=35,abs(a[1]-b[1])/2,a[0]+7,(a[1]+b[1])/2-14))
                candidates.append((unique,abs(a[1]-b[1])>=35,abs(a[1]-b[1])/2,a[0]-width-7,(a[1]+b[1])/2-14))
        candidates.sort(reverse=True);chosen=candidates[0]
        for candidate in candidates:
            x,y=candidate[3:]
            clear_states=all(x+width<=cx-43 or x>=cx+43 or y+25<=cy-43 or y>=cy+43 for cx,cy in POSITIONS.values())
            if clear_states and all(x+width<=bx or bx+bw<=x or y+25<=by or by+bh<=y for bx,by,bw,bh in boxes):chosen=candidate;break
        x,y=chosen[3:];boxes.append((x,y,width,25))
        label_cell=text(root,f'label-{source}-{target}',label,x,y,width,25,14,color='#233C56');label_cell.set('labelFor',f'e{source}-{target}')
        label_cell.set('style',label_cell.get('style').replace('fillColor=none;','fillColor=#ffffff;'))
    initial=ET.SubElement(root,'mxCell',id='initial',value='开始',edge='1',parent='1',target='s0',style='edgeStyle=none;html=0;endArrow=classic;fontSize=16;entryX=0;entryY=0.5;entryPerimeter=0;')
    geo=ET.SubElement(initial,'mxGeometry',relative='1',**{'as':'geometry'});ET.SubElement(geo,'mxPoint',x='25',y='235',**{'as':'sourcePoint'})
    text(root,'notes','最长匹配结束后执行最后接受动作；同长按规则顺序。完整 ID 再查关键字。\n红色状态可继续转移；未画出的转移为 -1。EOF 由扫描驱动生成。交叉处不表示新的状态。',400,6600,4550,90,21)
    text(root,'classes-title','输入字节类别（SP 为空格，0xHH 为十六进制字节）',400,6730,4500,40,25,True)
    for index in range(len(data['classes'])):
        col,row=divmod(index,12);text(root,f'class-{index}',f'C{index:02d} = {class_text(index)}',400+col*1190,6800+row*40,1130,38,17)
    text(root,'symbols-title','较长输入集合的缩写（每条边仍只消耗一个字节）',400,7330,4500,40,25,True)
    for index,(categories,symbol) in enumerate(symbols.items()):
        col,row=divmod(index,16);text(root,f'symbol-{index}',f'{symbol} = {category_label(list(categories))}',400+col*1600,7400+row*60,1550,55,17)
    ET.indent(file,space='  ')
    return ET.tostring(file,encoding='utf-8',xml_declaration=True)
