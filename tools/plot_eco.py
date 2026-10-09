from pathlib import Path
import csv,math,json
from reportlab.graphics.shapes import Drawing,String
from reportlab.graphics.charts.barcharts import VerticalBarChart
from reportlab.graphics.charts.legends import Legend
from reportlab.lib import colors
from reportlab.graphics import renderSVG
root=Path('docs/measurements/eco-1.2.0');out=root/'plots';out.mkdir(exist_ok=True)
rows=list(csv.DictReader((root/'holdout.csv').open()))
def chart(name,title,labels,data,series,low,high,step,subtitle):
 d=Drawing(720,355);d.add(String(40,328,title,fontName='Helvetica-Bold',fontSize=15));d.add(String(40,307,subtitle,fontSize=9))
 b=VerticalBarChart();b.x=60;b.y=66;b.width=610;b.height=210;b.data=data;b.categoryAxis.categoryNames=labels
 b.valueAxis.valueMin=low;b.valueAxis.valueMax=high;b.valueAxis.valueStep=step;b.barSpacing=2;b.groupSpacing=12
 palette=[colors.HexColor('#355C8C'),colors.HexColor('#C86F28')]
 for i,col in enumerate(palette):b.bars[i].fillColor=col;b.bars[i].strokeColor=None
 b.valueAxis.labels.fontSize=9;b.categoryAxis.labels.fontSize=9;d.add(b)
 l=Legend();l.x=70;l.y=33;l.fontSize=10;l.columnMaximum=1;l.colorNamePairs=[(palette[i],n) for i,n in enumerate(series)];d.add(l)
 renderSVG.drawToFile(d,str(out/name))
def row(rate,fixture,mode):return next(r for r in rows if int(r['rate'])==rate and int(r['fixture'])==fixture and int(r['mode'])==mode)
def db(value,reference):return max(-120,20*math.log10(max(float(value),1e-15)/max(float(reference),1e-15)))
for rate in [48000,96000]:
 data=[]
 for mode in [0,3]:
  r=row(rate,16,mode);data.append([db(r['h'+str(h)],r['fundamental']) for h in range(2,6)])
 chart(f'harmonics-{rate}.svg',f'Heavy-drive harmonics at {rate//1000} kHz', ['H2','H3','H4','H5'],data,['Circuit reference','Eco 1x'],-100,0,20,'997 Hz, fixture 16. dBc relative to each output fundamental; levels are not loudness normalized.')
 data=[]
 for mode in [0,3]:
  r=row(rate,17,mode);data.append([db(r['imd_lower'],r['fundamental']),db(r['imd_upper'],r['fundamental'])])
 chart(f'imd-{rate}.svg',f'Two-tone IMD at {rate//1000} kHz',['Lower probe','Upper probe'],data,['Circuit reference','Eco 1x'],-100,0,20,'Fixture 17. dBc relative to each output fundamental; raw amplitudes retained in holdout.csv.')
cases=json.loads((root/'benchmark/summary.json').read_text())['cases']
chart('speedup.svg','Measured Eco speedup against v1.1.4 Default',['S32','S64','S256','EQ','Silence','Bursts'],[[c['speedup'] for c in cases[:6]],[c['speedup'] for c in cases[6:]]],['48 kHz','96 kHz'],0,40,10,'Stereo, Eco 1x; reference CPU / Eco CPU. Minimum 10x; stretch 20x. Three runs per engine/workload.')
