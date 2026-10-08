#!/usr/bin/env python3
"""Offline diagnostics: capture both solvers, report errors and paired timings.

No error threshold here certifies sound equivalence. All measurements are scalar
open-loop detector measurements, not complete plugin playback validation.
"""
import argparse
import csv
import math
import pathlib
import re
import statistics
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('executable', type=pathlib.Path)
parser.add_argument('netlists', type=pathlib.Path)
parser.add_argument('--rates', nargs='+', type=int, default=[44100, 48000, 96000])
parser.add_argument('--timing-runs', type=int, default=3)
args = parser.parse_args()
if args.timing_runs < 1:
    parser.error('--timing-runs must be positive')
columns = ['led_amps', 'timing_volts', 'positive_load_amps', 'negative_load_amps', 'opamp_load_amps']
status = ['iterations', 'factors', 'retries', 'rescues', 'converged']

def run(rate, reduced, path='-'):
    result = subprocess.run([str(args.executable.resolve()), str(args.netlists.resolve()),
                             str(path), str(rate), str(int(reduced))],
                            check=True, capture_output=True, text=True)
    times = [float(x) for x in re.findall(r'samples, ([0-9.]+) us/sample', result.stdout)]
    if len(times) != 7:
        raise RuntimeError('unexpected capture output: ' + result.stdout)
    return times, result.stdout

with tempfile.TemporaryDirectory(prefix='tubechannel-detector-comparison-') as directory:
    for rate in args.rates:
        original = pathlib.Path(directory) / 'original.csv'
        reduced = pathlib.Path(directory) / 'reduced.csv'
        run(rate, False, original)
        _, description = run(rate, True, reduced)
        print(next(line for line in description.splitlines() if line.startswith('reduction:')), flush=True)
        stats = {k: [0., 0., 0.] for k in columns}
        count = changed = 0
        with original.open() as a, reduced.open() as b:
            for x, y in zip(csv.DictReader(a), csv.DictReader(b), strict=True):
                if (x['case'], x['sample'], x['time_seconds'], x['input_volts']) != (
                        y['case'], y['sample'], y['time_seconds'], y['input_volts']):
                    raise RuntimeError('capture stimuli are not aligned')
                count += 1
                changed += any(x[k] != y[k] for k in status)
                for k in columns:
                    ref, candidate = float(x[k]), float(y[k])
                    if not math.isfinite(ref) or not math.isfinite(candidate):
                        raise RuntimeError('nonfinite capture')
                    delta = candidate - ref
                    s = stats[k]
                    s[0] = max(s[0], abs(delta)); s[1] += delta * delta; s[2] += ref * ref
        print(f'{rate} Hz: {count} samples; {changed} rows with different solver-status fields', flush=True)
        for k, (peak, error, signal) in stats.items():
            db = 10 * math.log10(error / signal) if error and signal else (-math.inf if not error else math.inf)
            print(f'  {k}: max absolute error {peak:.9g}; relative RMS error {db:.2f} dB', flush=True)
        timings = [[], []]
        for repetition in range(args.timing_runs):
            # Alternate order to reduce consistent first-run bias; never run concurrently.
            for mode in ([False, True] if repetition % 2 == 0 else [True, False]):
                times, _ = run(rate, mode)
                timings[int(mode)].append(times)
        for scenario in range(7):
            before = statistics.median(t[scenario] for t in timings[0])
            after = statistics.median(t[scenario] for t in timings[1])
            print(f'  case {scenario}: median {before:.3f} -> {after:.3f} us/sample; '
                  f'change {(after / before - 1) * 100:+.1f}%', flush=True)
