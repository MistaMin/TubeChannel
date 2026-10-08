"""Render circuit/prototype compression timing from measured CSV, without alignment or normalization."""
import argparse
import json
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.patches import Patch

parser = argparse.ArgumentParser()
parser.add_argument('measurements', type=Path)
args = parser.parse_args()
root = args.measurements
out = root / 'plots'
out.mkdir(exist_ok=True)
normal = np.genfromtxt(root / 'timing-curves.csv', delimiter=',', names=True)
stress = np.genfromtxt(root / 'round-8-holdout6.csv', delimiter=',', names=True)
BLUE, ORANGE, RED = '#1565b0', '#db6a17', '#a82747'
plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 11, 'axes.titlesize': 13,
                     'axes.labelsize': 11, 'axes.spines.top': False, 'axes.spines.right': False,
                     'figure.facecolor': 'white', 'savefig.facecolor': 'white', 'svg.fonttype': 'none'})
legend = [Line2D([], [], color=BLUE, lw=2, label='Original circuit'),
          Line2D([], [], color=ORANGE, lw=2, ls='--', label='Reduced detector, round 8'),
          Patch(facecolor='#e3e8ef', label='Input tone on')]
metrics = {}

def series(data, case):
    a = data[data['case'] == case]
    assert len(a) > 100 and np.all(np.diff(a['time_seconds']) > 0)
    assert np.all(np.isfinite(a['reference_gr_db'])) and np.all(np.isfinite(a['prototype_gr_db']))
    return a['time_seconds'], -a['reference_gr_db'], -a['prototype_gr_db']

def panel(fig, slot, data, case, title, span, yrange=None, erange=None):
    grid = slot.subgridspec(2, 1, height_ratios=[3, 1], hspace=0.10)
    ax = fig.add_subplot(grid[0]); err = fig.add_subplot(grid[1], sharex=ax)
    t, ref, proto = series(data, case)
    for a in [ax, err]:
        for start, stop in [(0.1, 0.5), (0.9, 1.1)]:
            a.axvspan(start, stop, color='#e3e8ef', zorder=0)
        a.grid(axis='y', color='#cbd3de', alpha=.55, linewidth=.7)
        a.set_xlim(*span)
    ax.plot(t, ref, color=BLUE, lw=1.8)
    ax.plot(t, proto, color=ORANGE, ls='--', lw=1.8)
    ax.set_title(title, loc='left', pad=9)
    ax.set_ylabel('Gain change (dB)')
    if yrange: ax.set_ylim(*yrange)
    else: ax.set_ylim(min(ref.min(), proto.min()) - 0.6, 0.4)
    ax.tick_params(labelbottom=False)
    delta = proto - ref
    err.plot(t, delta, color=RED, lw=1.3)
    err.axhline(0, color='#586575', lw=.9, ls=':')
    err.set_ylabel('Difference\n(dB)', fontsize=10)
    if erange: err.set_ylim(*erange)
    err.set_xlabel('Time (seconds)')
    peak = np.argmax(np.abs(delta))
    metrics[f'{data is stress}:{case}'] = {'case': int(case), 'csv_sample_rms_error_db': float(np.sqrt(np.mean(delta**2))),
        'csv_sample_max_error_db': float(abs(delta[peak])), 'peak_time_seconds': float(t[peak])}
    return ax, err

def finish(fig, name, title, subtitle):
    fig.suptitle(title, x=.055, y=.975, ha='left', fontsize=20, fontweight='bold', color='#152b42')
    fig.text(.055, .927, subtitle, ha='left', va='top', fontsize=11, color='#46576a')
    fig.legend(handles=legend, loc='upper left', bbox_to_anchor=(.05, .906), ncol=3, frameon=False, fontsize=11)
    fig.text(.055, .027, 'Lower curve = more compression. Difference = prototype gain minus circuit gain; positive = less compression.', fontsize=10, color='#46576a')
    fig.savefig(out / (name + '.png'), dpi=160)
    fig.savefig(out / (name + '.svg'))
    plt.close(fig)

cases = [0, 1, 2, 3]
titles = ['Manual • shortest settings (knobs 0 / 0)',
          'Manual • default settings (knobs 0.5 / 0.5)',
          'Manual • longest settings (knobs 1 / 1)',
          'Auto • automatic timing']
fig = plt.figure(figsize=(13.6, 9.6))
grid = fig.add_gridspec(2, 2, left=.07, right=.97, bottom=.10, top=.83, hspace=.42, wspace=.23)
min_gain = min(min(series(normal, c)[1].min(), series(normal, c)[2].min()) for c in cases)
for i, c in enumerate(cases):
    panel(fig, grid[i//2, i%2], normal, c, titles[i], (0, 6), (min_gain - .6, .4), (-3.5, 3.5))
finish(fig, 'timing-overview', 'Compression timing: original circuit vs prototype',
       'Identical 1 kHz tone, 1.8 V peak into the detector; 48 kHz, fixed nominal rails, tolerances off. Two bursts, then long recovery.')

fig = plt.figure(figsize=(13.6, 9.6))
grid = fig.add_gridspec(2, 2, left=.07, right=.97, bottom=.10, top=.83, hspace=.43, wspace=.23)
for row, c in enumerate([1, 2]):
    panel(fig, grid[row, 0], normal, c, ('Default' if c == 1 else 'Longest') + ' manual • first attack', (.085, .30), erange=(-3.5, 3.5))
    panel(fig, grid[row, 1], normal, c, ('Default' if c == 1 else 'Longest') + ' manual • final release', (1.075, 3.5), erange=(-3.5, 3.5))
finish(fig, 'timing-zoom', 'Where the timing responses separate',
       'Same controlled tone as the overview. Left: first attack. Right: recovery after the second burst stops at 1.1 seconds.')

fig = plt.figure(figsize=(13.6, 6.0))
grid = fig.add_gridspec(1, 2, left=.07, right=.97, bottom=.15, top=.74, wspace=.23)
panel(fig, grid[0, 0], stress, 6, 'Slow manual stress case • attack', (.085, .30), (-30.4, .4), (-8.2, 1.5))
recovery_ax, _ = panel(fig, grid[0, 1], stress, 6, 'Same stress case • complete recovery', (0, 6), (-30.4, .4), (-8.2, 1.5))
t, ref, proto = series(stress, 6)
peak = np.argmax(np.abs(proto - ref))
recovery_ax.annotate('', xy=(t[peak], ref[peak]), xytext=(t[peak], proto[peak]),
                     arrowprops={'arrowstyle': '<->', 'color': RED, 'lw': 1.5})
recovery_ax.annotate('7.5 dB extra compression', xy=(t[peak], proto[peak]), xytext=(3.5, -13),
                     color=RED, fontsize=10, arrowprops={'arrowstyle': '-', 'color': RED})
finish(fig, 'timing-stress-case', 'Largest timing mismatch in the independent checks',
       'Case 6: 44.1 kHz; attack / release knobs 0.9 / 0.9. 10.48 / 15.804 kHz input components, 2.191 V scale, 7 Hz modulation.')
(out / 'plotted-metrics.json').write_text(json.dumps(metrics, indent=2) + '\n')
(out / 'README.md').write_text('''# Compression timing plots

Blue: original circuit detector. Orange dashed: current round-8 reduced detector.
Gain change is negative attenuation; lower means more compression. The lower trace
is prototype gain minus circuit gain, so positive means less prototype compression.
No level normalization, time shift, smoothing or fitted curve is applied.
Gray intervals mark input on: 0.1–0.5 seconds and 0.9–1.1 seconds.

`timing-overview` and `timing-zoom` use fresh six-second captures of the identical
1 kHz / 1.8 V peak tone at 48 kHz, fixed nominal rails and no component tolerances.
Threshold and ratio remain at defaults while manual attack/release knobs are
0/0, 0.5/0.5 and 1/1. Auto uses the same input; its manual timing controls are
automatically timed. Prototype Auto curves are identical across manual-knob settings; measured reference differences reach 0.108 dB in this capture.

These are detector plus unchanged optical-response measurements, not complete
plugin audio curves. They use the 14.7 kOhm optical-divider GR metric.
Panel time labels are calibration labels, not measured timing for this stimulus:
short: 4.3 ms / 97 ms; default: about 10.2 ms / 185 ms; long: 33.3 ms / 1.107 s.
Actual attack/release depends on signal, optical state and control history.

`timing-stress-case` uses independent holdout6 case 6 (different stimulus), chosen
because it had the largest full-cadence optical GR mismatch: 7.5034 dB. The second generated sinusoid is above Nyquist and appears as a 15.804 kHz component in the 44.1 kHz discrete input; both models receive exactly the same samples.
Do not compare this stimulus directly with the controlled-tone panels.
CSV samples are about 2 ms apart; plotted peaks may be lower than the full-cadence
validation peaks. See `plotted-metrics.json` and the underlying validation logs.
PNG previews and editable SVG exports are supplied for every figure.
''')
# Verify the reduced Auto mode ignores manual knobs; measure finite reference coupling separately.
auto_differences = []
for c in [4, 5]:
    a, b = series(normal, 3), series(normal, c)
    assert np.array_equal(a[0], b[0]) and np.array_equal(a[2], b[2]), 'Prototype Auto controls unexpectedly differ'
    auto_differences.append(float(np.max(np.abs(a[1] - b[1]))))
(out / 'auto-control-differences.json').write_text(json.dumps({'reference_max_difference_db': auto_differences, 'prototype_difference_db': 0.0}, indent=2) + '\n')
print('Rendered all three PNG/SVG figures. Reference Auto-control differences:', auto_differences)
