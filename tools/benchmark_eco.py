"""Sequential, alternating Eco/reference benchmarks with matched audio durations."""
import argparse,json,re,subprocess,statistics,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('reference');p.add_argument('candidate');p.add_argument('output');a=p.parse_args();root=Path(a.output);root.mkdir(parents=True,exist_ok=True)
result={'reference_commit':'a80455549b9b92466f73d9017b8127fad779f013','reference_sha256':hashlib.sha256(Path(a.reference).read_bytes()).hexdigest(),'candidate_sha256':hashlib.sha256(Path(a.candidate).read_bytes()).hexdigest(),'compiler_flags':'-O3 -DNDEBUG -std=gnu++20 -mmacosx-version-min=11.0','warmup_seconds':.5,'measured_seconds':1.,'runs':[],'cases':[]}
failures=[]
for rate in (48000,96000):
 for block,amp,psu,automate,bursts,attack,release in [(32,.126,0,0,0,.5,.5),(64,.126,0,0,0,.5,.5),(256,.126,0,0,0,.5,.5),(32,1.,1,2,0,1.,1.),(64,0.,0,0,0,.5,.5),(256,1.,1,0,1,1.,1.)]:
  warmup=round(.5*rate/block);total=warmup+round(rate/block);case=f'{rate}-{block}-{amp}-{psu}-{automate}-{bursts}';cost={'reference':[],'eco':[]};tails={'reference':[],'eco':[]}
  for i,kind in enumerate(('reference','eco','eco','reference','reference','eco')):
   exe=a.reference if kind=='reference' else a.candidate;quality=0 if kind=='reference' else 3
   args=[exe,'Netlists',2,1,psu,total,amp,block,quality,automate,attack,release,bursts,-1,0,rate,1,warmup]
   run=subprocess.run(list(map(str,args)),capture_output=True,text=True);(root/f'{case}-{i}-{kind}.txt').write_text(run.stdout+run.stderr)
   if run.returncode:raise RuntimeError(f'{case} {kind} failed: {run.stderr}')
   cpu=float(re.search(r'process CPU: avg ([0-9.]+)',run.stdout)[1]);late=int(re.search(r'deadline .*: (\d+)/',run.stdout)[1]);cost[kind].append(cpu);tails[kind].append(late)
   result['runs'].append({'case':case,'kind':kind,'arguments':list(map(str,args)),'process_cpu_ms':cpu,'missed_deadlines':late})
  ref=statistics.mean(cost['reference']);eco=statistics.mean(cost['eco']);speed=ref/eco
  result['cases'].append({'case':case,'rate_hz':rate,'buffer':block,'amplitude':amp,'psu':bool(psu),'eq_automation':bool(automate),'bursts':bool(bursts),'reference_ms':ref,'eco_ms':eco,'speedup':speed,'minimum_10x_pass':speed>=10,'stretch_20x_pass':speed>=20,'reference_missed_deadlines':sum(tails['reference']),'eco_missed_deadlines':sum(tails['eco'])})
  if speed<10:failures.append(case)
  (root/'summary.json').write_text(json.dumps(result,indent=2)+'\n');print(case,f'{ref:.4f} vs {eco:.4f} ms = {speed:.2f}x',flush=True)
print('minimum failures:',failures,flush=True)
raise SystemExit(1 if failures else 0)
