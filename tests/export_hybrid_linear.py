"""Export accepted measured profiles to fixed-size native filter data."""
from pathlib import Path
import argparse,json
p=argparse.ArgumentParser();p.add_argument('fit');p.add_argument('header');a=p.parse_args()
r=json.loads(Path(a.fit).read_text())
if not all(p['accepted_small_signal'] for p in r['profiles'].values()):raise SystemExit('profiles failed frequency-response gates')
s=['#pragma once','#include "Equalizer.h"','#include "Output.h"','', 'namespace tubechannel::hybrid', '{',
   '// Measured at 96 kHz, default EQ/master, tolerances off. Other states/rates require new profiles.',
   'struct DefaultLinearCalibration','{','    static constexpr double sampleRate = 96000.0;',
   '    static Equalizer::Profile equalizer()','    {','        Equalizer::Profile p;']
for name,p in r['profiles'].items():
 if name=='output':continue
 target='p.coupling' if name=='coupling' else f'p.bands[{int(name[-1])}]'
 s += [f'        {target}.count = {len(p["sections"])};', f'        {target}.gain = {p["gain"]:.17g};']
 for i,sec in enumerate(p['sections']):s.append(f'        {target}.sections[{i}] = {{ '+', '.join(f'{x:.17g}' for x in sec)+' };')
s += ['        return p;','    }','    static Output::Profile output()','    {','        Output::Profile p;']
p=r['profiles']['output'];s += [f'        p.before.count = {len(p["sections"])};',f'        p.before.gain = {p["gain"]:.17g};']
for i,sec in enumerate(p['sections']):s.append(f'        p.before.sections[{i}] = {{ '+', '.join(f'{x:.17g}' for x in sec)+' };')
s += ['        return p;','    }','};','} // namespace tubechannel::hybrid','']
Path(a.header).write_text('\n'.join(s))
