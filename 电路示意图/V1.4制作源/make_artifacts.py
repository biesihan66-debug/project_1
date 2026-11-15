from pathlib import Path
from collections import defaultdict
import json, html, sys
import geometry as layout
sys.stdout.reconfigure(encoding="utf-8")

ROOT=Path(__file__).parent
OUT=ROOT.parent
OUT.mkdir(exist_ok=True)
D=json.loads((ROOT/'layout_V1.4.json').read_text(encoding='utf-8'))
def H(p): return layout.hole(p)
def P(h): return layout.pt(h)
def points(route): return layout.expand([tuple(p) for p in route['points']])
routes=D['routes']
for side,prefix in [('bottom','B'),('top','T')]:
    ordered=sorted([r for r in routes if r['side']==side],key=lambda r:(r['stage'],routes.index(r)))
    for i,r in enumerate(ordered,1):r['id']=f'{prefix}{i:02}'

# Validate actual copper connectivity, never treating a resistor or capacitor as a wire.
owners={}
terminals=defaultdict(list)
for obj in D['parts']+D['chips']:
    for i,(p,n) in enumerate(zip(obj['pads'],obj['nets']),1):
        p=tuple(p)
        assert owners.get(p,n)==n,(obj['ref'],H(p))
        owners[p]=n;terminals[n].append((p,obj['stage'],obj['ref']))
for obj in D['ports']:
    p=tuple(obj['pad']);n=obj['net']
    assert owners.get(p,n)==n
    owners[p]=n;terminals[n].append((p,obj['stage'],obj['ref']))
bottom={};top={};cross=[]
top_forbidden=set(layout.blocked)
for ch in D['chips']:
    c=ch['pads'][0][0]
    top_forbidden.update((x,y) for x in range(c,c+4) for y in (7,8))
for obj in D['parts']:
    if obj['side']=='top':top_forbidden.update(layout.expand([tuple(p) for p in obj['pads']])[1:-1])
for obj in D['ports']:
    assert tuple(obj['pad']) not in top_forbidden,('port under component',obj['ref'],H(obj['pad']))
for r in routes:
    cells=points(r)
    if r['side']=='bottom':
        for p in cells:
            assert bottom.get(p,r['net'])==r['net'],('bottom short',r['id'],H(p))
            assert owners.get(p,r['net'])==r['net'],('pin short',r['id'],H(p))
            assert p not in layout.blocked,('bypass space',H(p))
            bottom[p]=r['net']
    else:
        for p in cells:
            assert top.get(p,r['net'])==r['net'],('top cross',r['id'],H(p))
            assert p not in top_forbidden,('component collision',r['id'],H(p))
            assert owners.get(p,r['net'])==r['net'],('top passes terminal',r['id'],H(p))
            top[p]=r['net']
        for p in (cells[0],cells[-1]):
            assert owners.get(p,r['net'])==r['net']
            assert bottom.get(p,r['net'])==r['net']
            owners[p]=r['net']
for p,n in top.items():
    if p in bottom and bottom[p]!=n:cross.append(H(p))
def check_connectivity(stage):
    parent={}
    def find(p):
        parent.setdefault(p,p)
        if parent[p]!=p:parent[p]=find(parent[p])
        return parent[p]
    def union(a,b):parent[find(a)]=find(b)
    for r in routes:
        if r['stage']>stage:continue
        ps=points(r) if r['side']=='bottom' else [tuple(r['points'][0]),tuple(r['points'][-1])]
        for p in ps:union(ps[0],p)
    for n,terms in terminals.items():
        pts=[p for p,s,ref in terms if s<=stage]
        if not pts:continue
        assert len({find(p) for p in pts})==1,('open',stage,n,[(H(p),ref) for p,s,ref in terms if s<=stage])
    groups=defaultdict(set)
    for n,terms in terminals.items():
        for p,s,ref in terms:
            if s<=stage:groups[find(p)].add(n)
    assert all(len(v)==1 for v in groups.values()),('short',groups)
check_connectivity(2);check_connectivity(4)
assert len([r for r in routes if r['side']=='top'])==7
assert len([r for r in routes if r['side']=='top' and r['stage']==2])==2
assert set(D['chips'][0]['nets'])=={'3V3','GND','TP1','TP2','TIA_N','DRV_P','DRV_N','VREF'}
assert D['chips'][2]['nets'][0:3]==['VDIFF','DIFF_N','DIFF_P']
report={
 '版本':'V1.4','底面异网相交':0,'正面绝缘线相互交叉':0,
 '绝缘跳线总数':7,'两线阶段绝缘跳线':2,'四线补装绝缘跳线':5,
 '正反面投影相交孔位_保持绝缘':sorted(set(cross)),
 '两线网络连通':'通过','四线网络连通':'通过','不同网络意外短接':'未发现',
 '元件脚号':'MCP6022 PDIP-8，缺口朝左；8/7/6/5 在 F 行，1/2/3/4 在 I 行',
 '焊接面':'仅镜像横坐标，A 行保持在上，列号 32 到 1',
 '实物状态':'未焊接验收；未上电、未验证精度；几何与网表核对不代替实测',
}
if '--check-only' in sys.argv:
    print(json.dumps(report,ensure_ascii=False))
    raise SystemExit(0)
(ROOT/'verification_V1.4.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
(OUT/'洞洞板连接核对_V1.4.txt').write_text('\n'.join(f'{k}：{v}' for k,v in report.items())+'\n',encoding='utf-8-sig')

COLORS={'3V3':'#b52b4c','GND':'#365f86','VREF':'#9b6b00','TP1':'#b94b16','TP2':'#7146b4',
 'TIA_N':'#b94b16','DRV_P':'#bc6234','DRV_N':'#70675c','AC1':'#7c6d50','DDS':'#b94b16',
 'BUF_P':'#087b73','BUF_N':'#087b73','S+':'#087b73','S-':'#087b73','DIFF_P':'#087b73',
 'DIFF_N':'#087b73','VDIFF':'#7146b4','VBUF':'#9b6b00','ADC1':'#7146b4'}
INK='#1f3040';MUTED='#546b7d';GRID='#d6c8af';BG='#f3f5f4'
class SVG:
    def __init__(self,w=1760,h=1510):
        self.w=w;self.h=h;self.s=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" font-family="Microsoft YaHei, sans-serif">']
        self.rect(0,0,w,h,BG)
    def rect(self,x,y,w,h,fill='none',stroke='none',rx=0,sw=1,extra=''):
        self.s.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}" {extra}/>')
    def text(self,x,y,t,size=22,fill=INK,weight=400,anchor='start',extra=''):
        self.s.append(f'<text x="{x}" y="{y}" font-size="{size}" fill="{fill}" font-weight="{weight}" text-anchor="{anchor}" {extra}>{html.escape(str(t))}</text>')
    def line(self,ps,color=INK,width=3,dash='',extra=''):
        self.s.append(f'<polyline points="{" ".join(f"{x},{y}" for x,y in ps)}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linecap="round" stroke-linejoin="round" stroke-dasharray="{dash}" {extra}/>')
    def circle(self,x,y,r,fill='white',stroke=INK,sw=1):
        self.s.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')
    def save(self,name):
        (OUT/(name+'.svg')).write_text('\n'.join(self.s+['</svg>']),encoding='utf-8')
    def header(self,num,title,sub):
        self.text(54,64,'RLC / 洞洞板 V1.4',34,INK,700)
        self.text(1705,62,num,28,MUTED,700,'end')
        self.text(54,117,title,32,INK,700)
        self.text(54,155,sub,21,MUTED)
    def note(self,y,title,lines):
        self.rect(42,y,1676,70+len(lines)*34,'white','#d9e1e2',12)
        self.text(66,y+37,title,25,INK,700)
        for i,line in enumerate(lines):self.text(66,y+76+34*i,line,21,MUTED)

G=47;X=130;Y=260
def xy(p,back=False):
    c,r=p
    return (X+((32-c) if back else (c-1))*G,Y+(r-1)*G)
def board(s,back=False,zones=False):
    s.rect(X-36,Y-38,31*G+72,17*G+76,'#fcf8ee','#c8b898',10,1.6)
    if zones:
        for a,b,color in [(1,15,'#fff0dd'),(16,23,'#e4f4ec'),(24,32,'#e6edf8')]:
            s.rect(X+(a-1)*G-20,Y-20,(b-a+1)*G,17*G+40,color,'none',4,extra='opacity="0.45"')
    for c in range(1,33):
        px,_=xy((c,1),back)
        s.text(px,Y-19,c,16,MUTED,600,'middle')
        s.text(px,Y+17*G+31,c,16,MUTED,600,'middle')
    for r in range(1,19):
        _,py=xy((1,r),back)
        s.text(X-23,py+5,chr(64+r),17,MUTED,600,'middle')
        s.text(X+31*G+23,py+5,chr(64+r),17,MUTED,600,'middle')
        for c in range(1,33):
            px,py=xy((c,r),back);s.circle(px,py,4.6,'#fffefb',GRID,1.2)
    px,py=xy((1,1),back);s.circle(px,py,9,'none','#273d4c',2)
    s.text(100,1130,('焊接面：A 行仍在上，32 列在左。按孔号找点，不再二次镜像。' if back else '元件面：A 行在上，1 列在左。孔距横竖均为 2.54 mm；图不按打印比例使用。'),21,MUTED)

def chip(s,ch,back=False,ghost=False):
    pp=[xy(p,back) for p in ch['pads']];xs=[p[0] for p in pp];ys=[p[1] for p in pp]
    fill='#ded9ce' if ghost else ('#314756' if ch['stage']==2 else '#256359')
    s.rect(min(xs)-13,min(ys)+11,max(xs)-min(xs)+26,max(ys)-min(ys)-22,fill,'none',9)
    notchx=max(xs)+13 if back else min(xs)-13
    s.circle(notchx,(min(ys)+max(ys))/2,10,'#fcf8ee','none')
    s.text((min(xs)+max(xs))/2,(min(ys)+max(ys))/2-5,ch['ref'],25,MUTED if ghost else 'white',700,'middle')
    if not ghost:s.text((min(xs)+max(xs))/2,(min(ys)+max(ys))/2+24,'MCP6022',15,'white',400,'middle')
    for i,(p,n) in enumerate(zip(ch['pads'],ch['nets']),1):
        x,y=xy(p,back);s.circle(x,y,6,'white',COLORS[n],2)
        s.text(x,y+22 if p[1]==9 else y-11,i,16,INK,700,'middle')
    if not ghost:
        desc={'U1':'激励 / 跨阻','U2':'S+ / S− 缓冲','U3':'差分 / 偏置缓冲'}[ch['ref']]
        s.text((min(xs)+max(xs))/2,(min(ys)+max(ys))/2+47,desc,14,'white',400,'middle')

def component(s,p,back=False,ghost=False):
    if p['side']=='back_cap':return
    a,b=[xy(x,back) for x in p['pads']];mx=(a[0]+b[0])/2;my=(a[1]+b[1])/2
    color='#c6b79a' if ghost else ('#c28a35' if p['ref'].startswith('R') else '#2f8c8a')
    s.line([a,b],color,2 if ghost else 3)
    vert=a[0]==b[0]
    w,h=(19,60) if vert else (60,19)
    if p['ref'].startswith('C'):w,h=(17,30) if vert else (31,17)
    s.rect(mx-w/2,my-h/2,w,h,'#eee7d7' if ghost else ('#ffe0a7' if p['ref'].startswith('R') else '#bfe2de'),color,4,1.5)
    if not ghost:
        for pad,n in zip(p['pads'],p['nets']):
            x,y=xy(pad,back);s.circle(x,y,5.5,COLORS[n],'white',1)
        if vert:
            s.text(mx+16,my-5,p['ref'],18,INK,700)
            s.text(mx+16,my+18,p['value'],14,MUTED)
        else:
            # Cf is close to the TIA pin row; offset its label to the right.
            if p['ref']=='Cf':s.text(mx+34,my-10,'Cf 22p',15,INK,700)
            elif p['ref']=='C4':s.text(mx+69,my+5,'C4 4.7nF',16,INK,700)
            else:
                s.text(mx,my-17,p['ref']+'  '+p['value'],17,INK,700,'middle')
        if p['ref']=='C1':
            x,y=a;s.text(x+13,y+21,'+',20,'#b52b4c',700)

def draw_ports(s,back=False,active=4,labels=True):
    offsets={'3V3':(12,22,'start'),'GND':(10,-12,'start'),'F+':(-12,-14,'end'),
             'F-':(14,-14,'start'),'PC4/TP2':(-13,-14,'end'),'DDS':(0,26,'middle'),
             'S+':(-13,7,'end'),'S-':(14,7,'start')}
    for p in D['ports']:
        x,y=xy(p['pad'],back);color=COLORS[p['net']] if p['stage']<=active else '#bfc4c6'
        s.circle(x,y,8,color,'white',2)
        if labels and p['ref'] in offsets:
            dx,dy,anch=offsets[p['ref']]
            if back:dx=-dx;anch={'start':'end','end':'start','middle':'middle'}[anch]
            s.text(x+dx,y+dy,p['ref'].replace('F-','F−').replace('S-','S−'),18,color,700,anch,
                   'paint-order="stroke" stroke="#fcf8ee" stroke-width="5" stroke-linejoin="round"')
    x1,y1=xy(P('Q8'),back);x3,y3=xy(P('Q10'),back)
    s.rect(min(x1,x3)-13,y1-13,abs(x3-x1)+26,26,'none','#7146b4',4,2)
    for i,h in enumerate(['Q8','Q9','Q10'],1):
        x,y=xy(P(h),back);s.text(x,y-20,i,16,'#7146b4',700,'middle')
    s.text((x1+x3)/2,y1+37,'JP1',18,'#7146b4',700,'middle')

def draw_routes(s,back,stage=None,side='bottom',faded=False,ids=False):
    for r in routes:
        if r['side']!=side or (stage and r['stage']!=stage):continue
        ps=[xy(p,back) for p in r['points']]
        color='#d6d1c4' if faded else COLORS[r['net']]
        s.line(ps,'#fcf8ee',9)
        s.line(ps,color,3 if faded else (5 if side=='bottom' else 6),'' if side=='bottom' else '11 6')
        for p in [r['points'][0],r['points'][-1]]:
            x,y=xy(p,back);s.circle(x,y,5 if faded else 6,color,'#fcf8ee',1.2)
        if ids and not faded:
            # A label near the middle of the longest segment, on its free side.
            a,b=max(zip(ps,ps[1:]),key=lambda t:abs(t[0][0]-t[1][0])+abs(t[0][1]-t[1][1]))
            mx,my=(a[0]+b[0])/2,(a[1]+b[1])/2
            dx,dy=(14,0) if a[0]==b[0] else (0,-13)
            s.text(mx+dx,my+dy,r['id'],17,color,700,'middle' if not dx else 'start',
                   'paint-order="stroke" stroke="#fcf8ee" stroke-width="6" stroke-linejoin="round"')

def cap(s,ch,back=True):
    a=xy(ch['pads'][7],back);b=xy(ch['pads'][3],back)
    ref={'U1':'C2','U2':'C5','U3':'C6'}[ch['ref']]
    s.line([a,b],'#ffffff',10)
    s.line([a,b],'#437270',4,'5 4')
    cx=a[0]+(b[0]-a[0])*0.22;cy=a[1]+(b[1]-a[1])*0.22
    s.rect(cx-14,cy-9,28,18,'#bfe2de','#437270',4,2)
    s.text(cx+26,cy+5,ref,16,'#326864',700,extra='paint-order="stroke" stroke="#fcf8ee" stroke-width="4"')

names=[]
s=SVG(h=1510);s.header('01 / 安装','先摆元件和接口，再按第 02～04 张焊接','32×18 个独立焊盘 · 三颗 MCP6022 缺口均朝左 · 配套 H743 固件 V1.4 / Fixed1k')
board(s,zones=True)
for ch in D['chips']:chip(s,ch)
for p in D['parts']:component(s,p)
draw_ports(s)
s.note(1160,'区域与接口',[
 '左区先装 U1、R1～R6、Rf、C1～C4、Cf；中右区 U2/U3、R7～R10、C5/C6 留待四线补装。',
 '3.3 V=A2，GND=R2，DDS VOUT=N15；F+=I4，F−=C9；S+=L19，S−=E20。',
 'PA6 → JP1 中脚 Q9；PC4 → C8（TP2）。JP1：1=Q8/TP1，2=Q9/PA6，3=Q10/VDIFF。',
 'C2/C5/C6 为背面 100 nF 旁路电容，见焊接图；C1 立装于 P 行，已与 R2 留开间隔；小电容先试摆。',
 '孔网中心跨度 78.74×43.18 mm，外侧还需留边。数字模块和 H743 独立固定，不让 SPI/MCLK 横穿模拟板。'])
name='01_洞洞板元件安装_V1.4';s.save(name);names.append(name)

s=SVG(h=1580);s.header('02 / 两线底面','两线先焊：仅看彩色线和 C2','左右翻板后的实际焊接视角 · 每条实线均为贴板铜线 · 同一底面的不同网络不交叉')
board(s,True)
for ch in D['chips']:chip(s,ch,True,True)
draw_routes(s,True,2,'bottom')
cap(s,D['chips'][0])
draw_ports(s,True,2)
s.note(1160,'焊接顺序与读图',[
 '先用记号笔标 A1 和 A 行；焊好芯片座，再焊 A 行 3.3 V 母线、R 行 GND 母线及局部短线。',
 '实线经过的孔均属同一网络，可用铜线贴板走并在端点、分支点和必要固定点焊牢；不用长段锡堆代替导线。',
 '跨阻输入：C9 → D9 → E9 → F9，仅 3 个孔距。Rf 在 D8–D11，Cf 在 E8–E9，紧靠 U1 的 6/7 脚。',
 'C2 背焊于 U1 的 8/4 脚焊盘 F7/I10；电容体靠 8 脚，去 4 脚的引线套绝缘管，悬跨且不碰中间焊盘。',
 '还需安装第 04 张中的 T01、T02。JP1 跳帽短接 Q8–Q9（1–2），两线才完整。',
 '焊完先查网络通断，再插芯片；外部仅一路 5 V 给 H743，模拟板从 J2-3 取 3.3 V。'])
name='02_洞洞板两线底面焊接_V1.4';s.save(name);names.append(name)

s=SVG(h=1540);s.header('03 / 四线补焊','四线补装：灰色保留，只补彩色部分','本图仍是焊接面 · 灰线是两线已焊部分 · 不拆主电路，不切断已焊铜线')
board(s,True)
for ch in D['chips']:chip(s,ch,True,True)
draw_routes(s,True,2,'bottom',True)
draw_routes(s,True,4,'bottom')
cap(s,D['chips'][1]);cap(s,D['chips'][2]);draw_ports(s,True)
s.note(1160,'四线补装顺序',[
 '断电后装 U2/U3、R7～R10 和 C5/C6；四个 10 kΩ 电阻使用 0.1% 配对件。',
 '补焊本图彩线，再按第 04 张补 T03～T07。C5 背焊 F17/I20；C6 背焊 F26/I29，接法与 C2 一样。',
 '保留 Force 两根线：F+=I4，F−=C9；新增独立 Sense：S+=L19，S−=E20。',
 '仅将 JP1 跳帽从 Q8–Q9（1–2）移到 Q9–Q10（2–3）；PA6 的线始终在 Q9，PC4 始终在 C8。',
 '本图已按全部八脚功能核对连通性。实际焊点、差分增益/相位、开短路校准与测量精度仍需实物验证。'])
name='03_洞洞板四线补焊_V1.4';s.save(name);names.append(name)

s=SVG(h=1880);s.header('04 / 正面跳线','7 根绝缘线：两线装 T01～T02，四线再加 T03～T07','返回元件面：列号从 1 到 32 · 虚线是有实体长度的带皮导线 · 只剥两端，不在经过孔上焊接')
board(s)
for ch in D['chips']:chip(s,ch,False,True)
for p in D['parts']:component(s,p,False,True)
draw_routes(s,False,None,'top',ids=True);draw_ports(s)
tr=sorted([r for r in routes if r['side']=='top'],key=lambda r:r['id'])
s.rect(42,1150,1676,386,'white','#d9e1e2',12)
s.text(66,1191,'按线号裁线、弯折；路径中的转弯孔只作定位，端点才焊接',24,INK,700)
for i,r in enumerate(tr):
    x=68+(i//4)*830;y=1236+(i%4)*66
    hs=[H(p) for p in r['points']];mm=sum(abs(a[0]-b[0])+abs(a[1]-b[1]) for a,b in zip(r['points'],r['points'][1:]))*2.54
    s.text(x,y,r['id']+'  '+('两线' if r['stage']==2 else '四线补装')+' / '+r['net'],20,COLORS[r['net']],700)
    s.text(x,y+26,' → '.join(hs)+f'   路径约 {mm:.0f} mm，另留端头',18,MUTED)
s.note(1558,'从两线切到四线，只改变这些',[
 '① 补装绿色区域元件及 T03～T07。② F+/F− 保留，S+/S− 加到同一被测件两端的独立触点。',
 '③ JP1：Q8—Q9 → Q9—Q10。PA6 由 TP1 改采 VDIFF，PC4 仍采 TP2。',
 'Sense 触点靠元件本体，Force 触点靠外；板端不把 F 与 S 短接。两只双触点 Kelvin 夹合计四根线。',
 '先用 1 kHz、1 kΩ 反馈和 1 kΩ 标准电阻验收。V1.4 是布线改版，实测精度未验收。'])
name='04_洞洞板正面跳线与2改4_V1.4';s.save(name);names.append(name)

# A compact index with the exact stage handover, kept separate from large hole maps.
s=SVG(1760,1040);s.header('总览','洞洞板重新排布 / 以实际焊接顺序为主','仅洞洞板图纸更新到 V1.4；面包板沿用 V1.3，固件沿用 H743 V1.4。旧版文件保留。')
cards=[(54,'01','摆元件','缺口全朝左，按孔号预留四线区。'),(480,'02','焊两线','先底面短线，再装 T01、T02。'),(906,'03','补四线','加 U2/U3 与取样网络，不拆主线。'),(1332,'04','移跳帽','1–2 改 2–3；PA6/PC4 引线不动。')]
for x,num,t,desc in cards:
    s.rect(x,202,374,176,'white','#d9e1e2',12)
    s.text(x+22,241,num,23,'#087b73',700);s.text(x+22,283,t,28,INK,700)
    # Keep each line comfortably inside its card.
    cut={'01':['缺口全朝左，','按孔号预留四线区。'],'02':['先底面短线，','再装 T01、T02。'],'03':['加 U2/U3 与取样网络，','不拆主线。'],'04':['1–2 改 2–3；','PA6/PC4 引线不动。']}[num]
    for i,line in enumerate(cut):s.text(x+22,321+28*i,line,20,MUTED)
s.rect(54,414,795,354,'white','#d9e1e2',12);s.rect(885,414,821,354,'white','#d9e1e2',12)
for x,title,four in [(82,'两线阶段',False),(913,'四线阶段',True)]:
    s.text(x,458,title,28,INK,700)
    xx=x+40;yy=548
    s.line([(xx,yy),(xx+220,yy)],INK,4);s.rect(xx+70,yy-22,80,44,'#ffe0a7','#c28a35',5)
    s.text(xx+110,yy+8,'DUT',23,INK,700,'middle')
    s.line([(xx,yy),(xx,yy+60)],'#b94b16',5);s.line([(xx+220,yy),(xx+220,yy+60)],'#b94b16',5)
    s.text(xx,yy+87,'F+ / I4',19,'#b94b16',600,'middle');s.text(xx+220,yy+87,'F− / C9',19,'#b94b16',600,'middle')
    if four:
        for dx,t in [(52,'S+ / L19'),(168,'S− / E20')]:
            s.line([(xx+dx,yy),(xx+dx,yy-48)],'#087b73',4);s.text(xx+dx,yy-60,t,16,'#087b73',600,'middle')
    jx=x+360;jy=548
    for i,lab in enumerate(['1 / Q8','2 / Q9','3 / Q10']):
        s.circle(jx+i*116,jy,7,'#7146b4','none');s.text(jx+i*116,jy-20,lab,18,'#7146b4',600,'middle')
    start=1 if four else 0;s.line([(jx+start*116,jy),(jx+start*116,jy+24),(jx+(start+1)*116,jy+24),(jx+(start+1)*116,jy)],'#7146b4',7)
    s.text(jx+116,jy+59,'跳帽 2–3' if four else '跳帽 1–2',21,'#7146b4',700,'middle')
    s.text(x,666,'PA6 / J3-28 → Q9，中脚始终不换线',22,INK,600)
    s.text(x,706,'PC4 / J3-26 → C8，始终采 TP2',22,INK,600)
s.note(800,'这版解决的是焊接落地问题',[
 '底面不同网络无交叉；正面 7 根绝缘跳线分阶段编号；背面已镜像，提供逐孔连接表。',
 '电路参数保持 1 kΩ ∥ 22 pF 固定反馈。图纸连通性已核对，焊接、电源、波形和精度待实物验收。'])
s.save('00_两线改四线总览_V1.4')

# Keep the familiar main filename as a real assembly/wiring sheet.
full=SVG(h=1490)
full.header('完整成板参考','完整底面：电源、反馈和采样线各走各的通道','焊接面已左右镜像 · 图中合并两线与四线的底面铜线 · 正面绝缘线单独看第 04 张')
board(full,True)
for ch in D['chips']:chip(full,ch,True,True)
draw_routes(full,True,2,'bottom');draw_routes(full,True,4,'bottom')
for ch in D['chips']:cap(full,ch)
draw_ports(full,True)
full.note(1160,'分阶段施工，逐根核对',[
 '首次两线只做第 02 张和 T01/T02；四线再补第 03 张和 T03～T07。底面不同网络没有交叉。',
 '升级时保留 Force，新增 S+=L19 / S−=E20；JP1 从 Q8–Q9 移到 Q9–Q10。PA6 留 Q9，PC4 留 C8。',
 '3.3 V、GND、VREF 分别为红、蓝、金；橙为激励相关、紫为 ADC、绿为取样相关。连接以孔号为准。',
 '全部图纸与逐孔 TXT 已核对，仍需焊后通断、上电和精度验收。元件面和焊接面始终使用同一组孔号。'])
front=(OUT/'01_洞洞板元件安装_V1.4.svg').read_text(encoding='utf-8').split('>',1)[1].rsplit('</svg>',1)[0]
combined='<svg xmlns="http://www.w3.org/2000/svg" width="1760" height="3000" viewBox="0 0 1760 3000" font-family="Microsoft YaHei, sans-serif">'
combined+='<svg width="1760" height="1510" viewBox="0 0 1760 1510">'+front+'</svg>'
combined+='<svg y="1510" width="1760" height="1490" viewBox="0 0 1760 1490">'+'\n'.join(full.s[1:])+'</svg></svg>'
(OUT/'洞洞板焊接示意图_V1.4.svg').write_text(combined,encoding='utf-8')

old=(ROOT.parent/'RLC电桥思路及接线说明_V1.3.txt').read_text(encoding='utf-8-sig')
intro=old.split('三、洞洞板')[0].replace('RLC 电桥 V1.3','RLC 电桥 V1.4',1)
intro=intro.replace('洞洞板 O29/P29/Q29 的选接排针另称 ADC 跳帽。','洞洞板 Q8/Q9/Q10 的选接排针称 JP1；不是大越板 J1。')
intro=intro.replace('主板按 V1.3 图上半部接好。','面包板仍按 V1.3 图上半部接好（本次未改）。')
middle='''三、洞洞板 V1.4：照着四张分图做
本次只重排洞洞板，电气拓扑、阻容标称值、H743 接口及固件 V1.4 不变。
面包板图继续使用原 V1.3；旧 V1.3 洞洞板坐标已全部作废于本新版，不能混用。
V1.4 是新板布局。若旧板已经焊好，不能只凭移动几个线头宣称套用新版孔号。

文件阅读顺序：01 元件安装 → 02 两线底面 → 04 装 T01/T02 → 两线验收 → 03 四线补焊 → 04 补 T03～T07。
板材：2.54 mm 间距的独立焊盘板，有效孔网至少 32 列 × 18 行，另留板边及夹具固定空间。
有效孔中心跨度为 78.74 × 43.18 mm，不能仅凭商品写“80×50 mm”断定装得下。
元件面 1 列在左、A 行在上。左右翻到焊接面后，32 列在左、A 行仍在上。
先标记 A1；两面坐标指同一个孔。图已经镜像，不要再自行镜像一次。

布线分工：
  Bxx：焊接面铜线，按所列折线贴板敷设。经过的每个孔归同一网络。
       用细镀锡实心铜线连接，端点、分支点及必要固定点焊牢；不靠长距离锡堆搭桥。
  Txx：元件面带皮线，仅两端焊接。所列转弯孔仅用来定位，不剥皮、不插孔、不焊中间点。
       约 0.3～0.5 mm 芯径的绝缘单芯线易整形；按路径长度另留端头，剪去多余尾线。
  旁路：C2/C5/C6 从背面焊在各 IC 座的 8/4 脚焊盘；电容体贴近 8 脚，
       通向 4 脚的较长引脚套绝缘管，不接中间孔。三颗电容下方的区域已避让裸铜线。
       先焊底面线路、旁路和芯片座，再插 IC；板底留足绝缘间隙。
以上不需要在任意交叉点临时想办法。顶面绝缘线可以投影跨过底面铜线，两者保持绝缘。

模块外接：
  大越 J2-3 3.3 V → A2；J2-5 GND → R2；AD9833 VOUT → N15。
  AD9833 GND 与本板和 H743 共地；DDS、OLED 和 H743 单独固定。
  5 V 仅给 H743 J2-1，不进本板 3.3 V 网络；数字时钟与 SPI 沿模拟板外绕行。
  PA6 / 大越 J3-28 → Q9（JP1 中脚）；PC4 / 大越 J3-26 → C8（TP2）。
  F+ → I4；F− → C9；S+ → L19；S− → E20。夹线做机械固定，不能拉扯焊点。
  TP1 可测 I4，TP2 可测 C8，VREF 可测 O4；四线 VDIFF 可测 Q10。
  JP1 三脚按 Q8 / Q9 / Q10 横向排列：1=TP1，2=PA6，3=VDIFF。

两线首次：
  装 U1、R1～R6、Rf、C1～C4、Cf，装 JP1 和对应外接线。
  焊完全部“两线先焊 Bxx”，再装 T01/T02。U2/U3 区域先留空。
  跳帽接 Q8–Q9，即 1–2；只使用 F+/F−。Fixed1k、1 kHz、1 kΩ 反馈先测 1 kΩ 标准电阻。
四线升级：
  断电，补 U2/U3、R7～R10（10 kΩ、0.1% 配对）、C5/C6 和“四线补焊 Bxx”。
  补齐 T03～T07。F+/F− 保留，在 DUT 两端各加一个独立 Sense 触点。
  跳帽移到 Q9–Q10，即 2–3。PA6 的线始终留在 Q9，PC4 始终留在 C8。
  两只双触点 Kelvin 夹一边一只，Sense 触点靠 DUT 本体；F 与 S 只在 DUT 两端相会。
  四线装齐后回两线对照：跳帽回 1–2；Sense 可继续接在 DUT 上，避免已上电缓冲输入悬空。

IC 座脚号（MCP6022 PDIP-8，全部缺口朝左）：
  U1 上排 F7/F8/F9/F10 = 8/7/6/5；下排 I7/I8/I9/I10 = 1/2/3/4。
  U2 上排 F17/F18/F19/F20 = 8/7/6/5；下排 I17/I18/I19/I20 = 1/2/3/4。
  U3 上排 F26/F27/F28/F29 = 8/7/6/5；下排 I26/I27/I28/I29 = 1/2/3/4。
  U1A 激励、U1B 跨阻；U2A/U2B 为 S+/S− 跟随器；U3A 差分、U3B 为 VREF 跟随器。

元件端点（同一焊盘多处接线可在背面焊到同一引脚，不要硬塞多根导线进孔）：
'''
for p in D['parts']:
    middle+=f"  {p['ref']:<4} {p['value']:<12} {H(p['pads'][0])}–{H(p['pads'][1])}   {p['nets'][0]} / {p['nets'][1]}"+('  [背面旁路]' if p['side']=='back_cap' else '')+'\n'
middle+='  C1 的 P4 是正端，P2 是负端；C3 取无极性电容。\n\n'
for stage in (2,4):
    middle+=('两线先焊：底面 Bxx\n' if stage==2 else '四线补焊：底面 Bxx\n')
    for r in sorted(routes,key=lambda r:r['id']):
        if r['side']=='bottom' and r['stage']==stage:
            middle+=f"  {r['id']} {r['net']:<7} "+' → '.join(H(p) for p in r['points'])+'\n'
    middle+='\n'
middle+='正面绝缘跳线：按线号操作，端点才焊接\n'
for r in tr:
    mm=sum(abs(a[0]-b[0])+abs(a[1]-b[1]) for a,b in zip(r['points'],r['points'][1:]))*2.54
    middle+=f"  {r['id']} {'两线先装' if r['stage']==2 else '四线补装'} {r['net']:<7} "+' → '.join(H(p) for p in r['points'])+f'   路径约 {mm:.0f} mm，另留端头\n'
middle+='''
四、核对与验收
图纸已核对两线/四线网络连通、不同网络无意外短接、底面无异网交叉、正面线不穿过元件体。
这是图纸核对，不能替代焊接后的通断和上电测试。
1. 不插 IC，按 B/T 端点逐一测通断，查焊锡碰邻孔。电阻连接的网络不一定绝缘，重点查意外零欧短接。
2. 夹具从板上拔下、夹绝缘片，确认四根引线独立；再接回板。板上 F− 为虚地，不能当 GND 用。
3. 上电前核对供电和 IC 方向；单路 5 V 给 H743，模拟板为 3.3 V，VREF 约 1.65 V。
4. Fixed1k + 1 kΩ 标准电阻，先两线：检查 TP1/TP2 无削顶，显示阻值合理，再验证 VDIFF。
5. 四线 VDIFF ≈ VREF + (S+ − S−)。验证极性、增益、相位后，按实际接法重新做开短路记录。
6. 100 kHz、10 pF 和低阻量程精度待实测；1 kΩ∥22 pF 固定反馈不等于已具备自动量程硬件。

本次范围：仅洞洞板电路图和与之配套的逐孔接线说明升为 V1.4；面包板 V1.3、固件 V1.4、思路及物料表不改。
旧 V1.3 的电气连接用于比对；V1.4 的布局重新编号，只按本版本坐标施工。

参考：
Microchip MCP6021/1R/2/3/4，DS20001685F：MCP6022 脚号及 §4.7 本地旁路。
https://ww1.microchip.com/downloads/aemDocuments/documents/MSLD/ProductDocuments/DataSheets/MCP6021-Data-Sheet-DS20001685.pdf
本图为洞洞板样机布置：将旁路体贴近电源脚并缩短回路；不能以网表检查声称高频或精度已验收。
H743 板接口以本地代码 V1.4 的 System/BSP_PinMap.h 与项目进度记录为准。
'''
(OUT/'RLC电桥思路及接线说明_V1.4.txt').write_text(intro+middle,encoding='utf-8-sig')
(OUT/'先看这里_V1.4.txt').write_text('''洞洞板 V1.4：本次仅优化实际焊接布局。

洞洞板焊接示意图_V1.4：元件面与完整底面汇总。
00：两线改四线的接口与跳帽总览。
01：元件面，先摆元件与接口。
02：焊接面，两线先焊的底面铜线。
03：焊接面，四线补焊的底面铜线。
04：元件面，7 根绝缘跳线，含两线改四线步骤。
RLC电桥思路及接线说明_V1.4.txt：逐孔连接和焊接顺序。
洞洞板连接核对_V1.4.txt：图纸核对结果与实测状态。

PNG 可直接看；SVG 可无限放大。四张分图的孔号完全一致；焊接面已经左右镜像。
面包板继续使用相邻“电路图V1.3（新开发板）”文件夹中的 V1.3 图，未重复生成。
代码继续用 H743 V1.4 / Fixed1k。旧文件不覆盖。
图纸核对已完成，尚未焊接和上电验收。
''',encoding='utf-8-sig')
print(json.dumps(report,ensure_ascii=False))
print('SVG:',len(names)+2)
