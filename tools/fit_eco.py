"""Fit offline circuit captures into fixed-size runtime profiles; report errors honestly."""
import argparse,csv,json
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('capture');p.add_argument('header');p.add_argument('report');a=p.parse_args()
root=Path(a.capture);records=list(csv.DictReader((root/'fixtures.csv').open()));profiles=[];curves=[];report=[];impulses={}; spectra={}
rate=96000.
def groups(roots):
 todo=sorted(roots,key=lambda r:abs(1-r));out=[]
 while todo:
  r=todo.pop(0)
  if abs(r.imag)>1e-6:
   j=min(range(len(todo)),key=lambda j:abs(todo[j]-r.conjugate()));out.append([r,todo.pop(j)])
  else:
   real=[i for i,v in enumerate(todo) if abs(v.imag)<=1e-6]
   out.append([r,todo.pop(real[0])] if real else [r])
 return sorted(out,key=lambda rs:abs(1-sum(rs)/len(rs)))
for row in records:
 id=int(row['id']);stage=int(row['stage']);amp=float(row['amplitude']);
 if amp:continue
 data=np.fromfile(root/f'{id}-impulse.bin',dtype=np.float64).reshape(-1,2);h0=data[:,1]/.001;h0-=np.median(h0[-256:]);h=np.fft.rfft(h0)
 freq=np.fft.rfftfreq(len(h0),1/rate);fitbins=np.unique(np.searchsorted(freq,np.geomspace(20,30000,500)));checks=(freq>=20)&(freq<=20000);best=None
 for order in (2,4,6,8):
  z=np.exp(-2j*np.pi*freq[fitbins]/rate);e=np.column_stack([z**i for i in range(order+1)])
  # Relative weighting keeps high- and low-frequency fits balanced.
  weight=1/np.maximum(abs(h[fitbins]),.01*np.max(abs(h[fitbins])))
  mat=np.column_stack([-h[fitbins,None]*e[:,1:],e])*weight[:,None];target=h[fitbins]*weight
  sol=np.linalg.lstsq(np.vstack([mat.real,mat.imag]),np.r_[target.real,target.imag],rcond=1e-12)[0]
  den=np.r_[1.,sol[:order]];num=sol[order:];poles=np.roots(den)
  if np.any(abs(poles)>=1):continue
  zz=np.exp(-2j*np.pi*freq[checks]/rate);pred=sum(num[i]*zz**i for i in range(order+1))/sum(den[i]*zz**i for i in range(order+1));rel=pred/h[checks]
  mag=float(np.max(abs(20*np.log10(np.maximum(abs(rel),1e-20)))));phase=float(np.max(abs(np.angle(rel)*180/np.pi)))
  score=mag+.02*phase
  if best is None or score<best[0]:best=(score,num,den,poles,mag,phase)
  if mag<.08 and phase<1:break
 if best is None:raise RuntimeError(f'no stable fit {id} stage {stage}')
 _,num,den,poles,mag,phase=best;pg=groups(poles);zg=groups(np.roots(num));sections=[]
 while len(pg)<len(zg):pg.append([])
 while len(zg)<len(pg):zg.append([])
 for pp,zp in zip(pg,zg):
  aa=np.pad(np.poly(pp).real,(0,3-len(pp)-1));bb=np.pad(np.poly(zp).real,(0,3-len(zp)-1));sections.append([*bb,aa[1],aa[2]])
 profiles.append((row,num[0],sections));spectra[id]=h;impulses[(stage,int(row['plate']),int(row['boost']))]=h0
 report.append(dict(row,magnitude_error_db=mag,phase_error_deg=phase,sections=len(sections)))
 print(id,stage,round(mag,3),round(phase,3),flush=True)
# Use the actual passive feedback topology to keep poles identifiable at flat
# settings. Measured compensation accounts for finite op-amp bandwidth/loading.
from numpy.polynomial import Polynomial as P
def ideal_eq(row):
 stage=int(row['stage']);l=float(row['level']);f=float(row['freq']);rg=int(row['range']);narrow=int(row['narrow'])
 q=P([0.,1.]);one=P([1.])
 if stage==6:
  cl=.1e-6+(.22e-6 if rg>>1 else 0);ch=220e-12+(470e-12 if rg&1 else 0)
  A=P([1/(20000*(1-l)+.001)+(0 if rg>>1 else 1e-6),cl]);B=P([1/(20000*l+.001)+(0 if rg>>1 else 1e-6),cl]);cin=22e-6
  an=A*q*cin;ad=A*(one+q*3000*cin)+q*cin;bn=B;bd=one+B*3000
  pa=an*bd;pb=bn*ad;dd=ad*bd;total=pa+pb+dd/47000
  ah=1/(3000+20000*(1-f)+.001);bh=1/(3000+20000*f+.001)
  cy=q*ch if rg&1 else q*(220e-12+470e-12)+q*q*(220e-12*470e-12*1e6)
  cden=one if rg&1 else one+q*(470e-12*1e6)
  high=(ah+bh)*cden+cy;K=-cy*47000
  num=-pa*high+total*K*ah
  den=pb*high-total*K*bh+10*(an*bn*(high-(ah+bh)*K)-pa*bh*K/47000+pa*bh*ah*cden-total*K*bh*ah-pb*ah*ah*cden+pb*high*ah)
 else:
  a=1/(100+20000*l+.001);b=1/(100+20000*(1-l)+.001);total=a+b+1e-6
  g=1.221 if narrow else 1.
  frac=(108200*(8200/108200)**f-8200)/100000;R=8200+100000*frac
  cs=(1e-9 if stage==7 else 10e-9)*(11 if rg==0 else 1)
  cw=(.68e-9 if stage==7 else 6.8e-9)*(11 if rg==0 else 1)
  tau=.1 if stage==7 else 1.
  common=P([1,R*(2*cs+cw),R*R*cs*cw])*(one+q*tau)+q*R*(.1e-6 if stage==7 else 1e-6)+q*q*R*R*cs*(.1e-6 if stage==7 else 1e-6)
  nn=common-q*q*(2*g*a/total*tau*R*cs);dd=common-q*q*(2*g*b/total*tau*R*cs)
  u=1/9980+a*(b+1e-6)/total;v=1/9980+a*b/total
  num=-q*22e-6*nn;den=q*22e-6*dd+(one+q*22e-6*10)*(u*dd+v*nn)
 nr=np.roots(num.coef[::-1]);dr=np.roots(den.coef[::-1])
 # The two coupling poles occupy one section, and the two resonator/shelf
 # poles the other. Retain cancelled poles at flat rather than changing order.
 nr=sorted(nr,key=abs);dr=sorted(dr,key=abs)
 if any(v.real>1e-5 for v in dr):raise RuntimeError('unstable passive EQ topology')
 ss=[];theta=.55;warp=np.pi*14000/rate;kappa=warp/np.tan(warp);dt=1/(rate*kappa)
 for j in range(0,len(dr),2):
  bb=np.poly(nr[j:j+2]).real;aa=np.poly(dr[j:j+2]).real
  if len(aa)==2:
   norm=aa[0]+aa[1]*dt*theta
   ss.append([(bb[0]+bb[1]*dt*theta)/norm,(-bb[0]+bb[1]*dt*(1-theta))/norm,0,(-aa[0]+aa[1]*dt*(1-theta))/norm,0]);continue
  def digital(c):
   return np.array([c[0]+c[1]*dt*theta+c[2]*dt*dt*theta*theta,
    -2*c[0]+c[1]*dt*(1-2*theta)+2*c[2]*dt*dt*theta*(1-theta),
    c[0]-c[1]*dt*(1-theta)+c[2]*dt*dt*(1-theta)**2])
  bd=digital(bb);ad=digital(aa);ss.append([*(bd/ad[0]),ad[1]/ad[0],ad[2]/ad[0]])
 return num.coef[-1]/den.coef[-1],ss
def vector_fit(h,poles,freq):
 bins=np.unique(np.searchsorted(freq,np.geomspace(20,30000,700)));z=np.exp(2j*np.pi*freq[bins]/rate);weight=1/np.maximum(abs(h[bins]),.01)
 def basis(p):
  cols=[];transform=[];used=set()
  for i,v in enumerate(p):
   if i in used:continue
   t=np.zeros(len(p),complex)
   if abs(v.imag)<1e-8:
    t[i]=1;cols.append(1/(z-v.real));transform.append(t);used.add(i)
   else:
    j=min((j for j in range(len(p)) if j!=i and j not in used),key=lambda j:abs(p[j]-v.conjugate()))
    t[i]=t[j]=1;cols.append(1/(z-v)+1/(z-v.conjugate()));transform.append(t)
    t=np.zeros(len(p),complex);t[i]=1j;t[j]=-1j;cols.append(1j/(z-v)-1j/(z-v.conjugate()));transform.append(t);used.update((i,j))
  return np.column_stack(cols),np.column_stack(transform)
 p=np.array(poles,dtype=complex)
 for iteration in range(8):
  B,T=basis(p);mat=np.column_stack([B,np.ones(len(z)),-h[bins,None]*B])*weight[:,None];target=h[bins]*weight
  real=np.vstack([mat.real,mat.imag]);rhs=np.r_[target.real,target.imag]
  # Anchor unobservable cancelled poles rather than replacing flat-band
  # resonators with arbitrary poles. All iteration is offline.
  reg=np.zeros((len(p),2*len(p)+1));reg[:,len(p)+1:]=np.diag(np.linalg.norm(real[:,len(p)+1:],axis=0)*.001)
  sol=np.linalg.lstsq(np.vstack([real,reg]),np.r_[rhs,np.zeros(len(p))],rcond=1e-12)[0]
  c=T@sol[len(p)+1:];q=np.linalg.eigvals(np.diag(p)-np.outer(np.ones(len(p)),c))
  q=np.array([v if abs(v)<1 else 1/v.conjugate() for v in q]);q=np.array([v if abs(v)<.999999 else v*.999999/abs(v) for v in q])
  # Preserve conjugate pairs to roundoff.
  p=q
 B,T=basis(p);mat=np.column_stack([B,np.ones(len(z))])*weight[:,None];target=h[bins]*weight
 sol=np.linalg.lstsq(np.vstack([mat.real,mat.imag]),np.r_[target.real,target.imag],rcond=1e-12)[0]
 r=T@sol[:-1];gain=sol[-1];zeros=np.linalg.eigvals(np.diag(p)-np.outer(np.ones(len(p)),r/gain))
 return p,zeros,gain
new=[]
for row,g,ss in profiles:
 stage=int(row['stage'])
 if stage not in (6,7,8):new.append((row,g,ss));continue
 _,ideal=ideal_eq(row);poles=[]
 for *_,a1,a2 in ideal:poles.extend(np.roots([1,a1] if abs(a2)<1e-15 else [1,a1,a2]))
 # Finite op-amp dynamics start from stable high-frequency poles. The
 # measured complete stage, not a division by a potentially nonminimum-
 # phase ideal transfer, supplies the fitting target.
 poles+= [-.8,-.2,-.65]
 h=spectra[int(row['id'])];freq=np.fft.rfftfreq((len(h)-1)*2,1/rate)
 pp,zp,gain=vector_fit(h,poles,freq);pg=groups(pp);zg=groups(zp);sections=[]
 for pgroup,zgroup in zip(pg,zg):
  aa=np.pad(np.poly(pgroup).real,(0,3-len(pgroup)-1));bb=np.pad(np.poly(zgroup).real,(0,3-len(zgroup)-1));sections.append([*bb,aa[1],aa[2]])
 if len(sections)>4:raise RuntimeError('Eco filter capacity exceeded')
 zz=np.exp(-2j*np.pi*freq[checks]/rate);pred=np.full(len(zz),gain,dtype=complex)
 for b0,b1,b2,a1,a2 in sections:pred*=(b0+b1*zz+b2*zz**2)/(1+a1*zz+a2*zz**2)
 rel=pred/h[checks];mag=float(np.max(abs(20*np.log10(np.maximum(abs(rel),1e-20)))));phase=float(np.max(abs(np.angle(rel)*180/np.pi)))
 new.append((row,gain,sections));report.append(dict(row,correction_magnitude_error_db=mag,correction_phase_error_deg=phase))
profiles=new
for row in records:
 amp=float(row['amplitude']);stage=int(row['stage']);id=int(row['id'])
 if not amp:continue
 h0=impulses[(stage,int(row['plate']),int(row['boost']))]
 H=np.sum(h0*np.exp(-2j*np.pi*1000*np.arange(len(h0))/rate))
 xy=np.fromfile(root/f'{id}-curve.bin',dtype=np.float64).reshape(-1,2)
 t=np.arange(len(xy))/rate;x=(amp*H*np.exp(2j*np.pi*1000*t)).imag
 start=16384
 valid=np.fromfile(root/f'{id}-valid.bin',dtype=np.uint8).astype(bool)
 # Quarantine two milliseconds after an unconverged reference step.
 bad=np.convolve((~valid).astype(int),np.ones(193,dtype=int),mode='full')[:len(valid)]>0
 mask=(np.arange(len(x))>=start)&~bad
 x=x[mask];y=xy[mask,1];order=np.argsort(x);x=x[order];y=y[order]
 # Average nearby points from both directions of the drive cycle; residual
 # hysteresis is measured separately, not hidden in a sound-match claim.
 bins=np.linspace(-amp*abs(H),amp*abs(H),97);ys=[];xs=[]
 for lo,hi in zip(bins[:-1],bins[1:]):
  m=(x>=lo)&(x<=hi)
  if np.any(m):xs.append(float(np.mean(x[m])));ys.append(float(np.mean(y[m])))
 xs=np.array(xs);ys=np.array(ys);domain=amp*abs(H)
 grid=np.linspace(-domain,domain,1025);out=np.interp(grid,xs,ys);out-=np.interp(0,grid,out)
 # Exact measured small-signal slope at the center prevents level offsets
 # from the high-drive fit contaminating the linear operating region.
 blend=np.clip(abs(grid)/max(domain*.025,.01),0,1);blend=blend*blend*(3-2*blend)
 out=grid+(out-grid)*blend
 if not np.all(np.isfinite(out)):raise RuntimeError('nonfinite curve')
 curves.append((row,domain,out))
 report.append(dict(row,curve_domain=domain,curve_points=len(out),excluded_reference_samples=int(np.sum(~mask[start:])),hysteresis_rms=float(np.sqrt(np.mean((y-np.interp(x,grid,out))**2)))))
combined=[]
for key in sorted({(int(r['stage']),int(r['plate']),int(r['boost'])) for r,_,_ in curves}):
 group=sorted([(r,d,y) for r,d,y in curves if (int(r['stage']),int(r['plate']),int(r['boost']))==key],key=lambda t:t[1])
 domain=group[-1][1];grid=np.linspace(-domain,domain,1025);out=[]
 for x in grid:
  r,d,y=next((item for item in group if abs(x)<=item[1]*.8),group[-1])
  out.append(np.interp(x,np.linspace(-d,d,1025),y))
 combined.append((group[0][0],domain,np.array(out)))
curves=combined
if any(x.get('correction_magnitude_error_db',0)>.1 or x.get('correction_phase_error_deg',0)>1 for x in report):
 raise RuntimeError('generated EQ profiles failed the measured frequency-response gate')
def fmt(x):return f'{float(x):.17g}'
s=['#pragma once','#include "FilterBank.h"','#include <array>','namespace tubechannel::hybrid::ecoData {',
 '// Generated by tools/fit_eco.py from nominal 96 kHz circuit captures.',
 '// Domain and fitting errors are recorded in docs/measurements/eco-1.2.0/fit.json.',
 'struct Linear { int stage; double level, frequency; int range, narrow, plate, boost; FilterBank<4>::Profile filter; };',f'inline const std::array<Linear,{len(profiles)}> linear {{{{']
for row,g,ss in profiles:
 sections=['{'+','.join(map(fmt,v))+'}' for v in ss];sections+=['{}']*(4-len(sections))
 s.append('{'+f'{row["stage"]},{row["level"]},{row["freq"]},{row["range"]},{row["narrow"]},{row["plate"]},{row["boost"]},'+ '{{{'+','.join(sections)+'}},'+str(len(ss))+','+fmt(g)+'}},')
s+=['}};','struct Curve { int stage, plate, boost; double limit; std::array<double,1025> output; };',f'inline const std::array<Curve,{len(curves)}> curves {{{{']
for row,dom,y in curves:s+=['{'+f'{row["stage"]},{row["plate"]},{row["boost"]},{fmt(dom)},'+'{{'+','.join(map(fmt,y))+'}}},']
s+=['}};', 'inline constexpr std::array<double,8> rails {{'+','.join((root/'rails.txt').read_text().split())+'}};', '} // namespace tubechannel::hybrid::ecoData'];Path(a.header).write_text('\n'.join(s)+'\n');Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
