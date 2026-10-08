"""Fit stable small-signal sections; reject profiles that miss measured response."""
import argparse,json
import numpy as np
p=argparse.ArgumentParser();p.add_argument('capture');p.add_argument('output');a=p.parse_args()
x=np.genfromtxt(a.capture,delimiter=',',names=True)
rate=96000.; records=[]
for k in range(5):
 rows=x[x['stage']==k]
 impulse=rows['output_volts']/0.001
 # DC numerical offsets are measured from the end of the impulse tail.
 impulse-=np.median(impulse[-256:])
 h=np.fft.rfft(impulse)
 if k==0:
  dry=rows['dry_volts']/0.001;dry-=np.median(dry[-256:]); hd=np.fft.rfft(dry)
  records.append(('coupling',hd));records.append(('band0',h/np.where(abs(hd)>1e-12,hd,1)))
 else:records.append((f'band{k}' if k<4 else 'output',h))
freq=np.fft.rfftfreq(len(impulse),1/rate)
# Independent uniform bins for evaluation, log-spaced bins for fitting.
fitbins=np.unique(np.searchsorted(freq,np.geomspace(15,35000,600)))
checkbins=np.where((freq>=20)&(freq<=20000))[0]
result={'rate_hz':rate,'state':'default controls, tolerances off; stage profiles only','profiles':{}}
for name,h in records:
 best=None
 for order in [1,2,3,4,6,8]:
  z=np.exp(-2j*np.pi*freq[fitbins]/rate)
  e=np.column_stack([z**i for i in range(order+1)])
  matrix=np.column_stack([-h[fitbins,None]*e[:,1:],e]); target=h[fitbins]
  sol=np.linalg.lstsq(np.vstack([matrix.real,matrix.imag]),np.r_[target.real,target.imag],rcond=1e-12)[0]
  den=np.r_[1.,sol[:order]];num=sol[order:]
  poles=np.roots(den);zeros=np.roots(num)
  if np.any(abs(poles)>=1):continue
  zz=np.exp(-2j*np.pi*freq[checkbins]/rate)
  pred=sum(num[i]*zz**i for i in range(order+1))/sum(den[i]*zz**i for i in range(order+1))
  relative=pred/h[checkbins]
  mag=float(np.max(abs(20*np.log10(abs(relative)))))
  phase=float(np.max(abs(np.angle(relative)*180/np.pi)))
  if best is None or mag+phase*.02<best[0]:best=(mag+phase*.02,order,num,den,poles,zeros,mag,phase)
  if mag<0.03 and phase<0.3:break
 if best is None:raise RuntimeError(f'no stable fit for {name}')
 _,order,num,den,poles,zeros,mag,phase=best
 def groups(roots):
  todo=list(roots);out=[]
  while todo:
   r=todo.pop(0)
   if abs(r.imag)>1e-7:
    j=min(range(len(todo)),key=lambda j:abs(todo[j]-r.conjugate()));s=todo.pop(j);out.append([r,s])
   else:
    real=[i for i,v in enumerate(todo) if abs(v.imag)<=1e-7]
    out.append([r,todo.pop(real[0])] if real else [r])
  return out
 pg,zg=groups(poles),groups(zeros)
 while len(zg)<len(pg):zg.append([])
 while len(pg)<len(zg):pg.append([])
 sections=[]
 for pgroup,zgroup in zip(pg,zg):
  aa=np.pad(np.real_if_close(np.poly(pgroup)).real,(0,3-len(pgroup)-1))
  bb=np.pad(np.real_if_close(np.poly(zgroup)).real,(0,3-len(zgroup)-1))
  sections.append([float(v) for v in [*bb,aa[1],aa[2]]])
 result['profiles'][name]={'sections':sections,'gain':float(num[0]),'order':order,'max_magnitude_error_db_20hz_20khz':mag,'max_phase_error_degrees_20hz_20khz':phase,'accepted_small_signal':mag<0.03 and phase<0.3}
 print(name,'order',order,'magnitude error',mag,'phase error',phase,'accepted',mag<.03 and phase<.3)
with open(a.output,'w') as f:json.dump(result,f,indent=2)
