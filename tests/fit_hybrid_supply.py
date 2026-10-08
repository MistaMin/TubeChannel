"""Fit load/recovery and phase-locked ripple; reserve the later steps for validation."""
import argparse
import json
import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument('capture')
parser.add_argument('output')
args = parser.parse_args()
x = np.genfromtxt(args.capture, delimiter=',', names=True)
# 2 kHz fitting captures eight mains-frequency harmonics without aliasing them.
x = x[::4]
t, phase = x['time_seconds'], x['phase']
train = t < 4.5
rails = ['p', 'n', 'raw', 'aux', 'multiplier', 'high', 'plate']
load_steps = [0.04, 0.04, 0.04, 0.004, 0.006, 0.006, 0.006]
basis = np.column_stack([func(h * phase) for h in range(1, 9) for func in [np.sin, np.cos]])

def state(tau):
    # Input steps occur at 1, 3, 5, 6.5 seconds; causal exact one-pole response.
    result = np.zeros_like(t)
    for edge, jump in [(1., 1.), (3., -1.), (5., 1.), (6.5, -1.)]:
        mask = t >= edge
        result[mask] += jump * (1 - np.exp(-(t[mask] - edge + 1 / 8000) / tau))
    return result

result = {'loadOhms': [], 'recoverySeconds': [], 'rippleSin': [], 'rippleCos': [], 'rails': {}}
for name, step in zip(rails, load_steps):
    best = None
    for tau in np.geomspace(0.001, 3.0, 180):
        design = np.column_stack([np.ones_like(t), state(tau), basis])
        coefficients = np.linalg.lstsq(design[train], x[name][train], rcond=None)[0]
        sign = 1 if name == 'n' else -1
        if coefficients[1] * sign < -1e-8:
            continue
        prediction = design @ coefficients
        loss = np.mean((prediction[train] - x[name][train]) ** 2)
        if best is None or loss < best[0]:
            best = (loss, tau, coefficients, prediction)
    if best is None:
        raise RuntimeError(f'no physically signed fit for {name}')
    loss, tau, c, predicted = best
    result['loadOhms'].append(abs(float(c[1])) / step)
    result['recoverySeconds'].append(float(tau))
    result['rippleSin'].append(c[2::2].tolist())
    result['rippleCos'].append(c[3::2].tolist())
    result['rails'][name] = {'fitted_mean_volts': float(c[0]), 'training_rms_volts': float(np.sqrt(loss)),
                           'later_steps_rms_volts': float(np.sqrt(np.mean((predicted[~train] - x[name][~train]) ** 2))),
                           'later_steps_max_error_volts': float(np.max(abs(predicted[~train] - x[name][~train])))}
with open(args.output, 'w') as f:
    json.dump(result, f, indent=2)
for name, record in result['rails'].items():
    print(name, record)
