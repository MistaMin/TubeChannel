"""Native harmonic and separated-order ESS comparisons; never claims a fitted Hammerstein model."""
import argparse
import gzip
import json
from pathlib import Path

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

parser = argparse.ArgumentParser()
parser.add_argument("captures", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
fs = 48000
names = ["Mic, normal gain / high plate", "Mic, +20 dB / low plate", "Instrument, normal gain / high plate"]
frequencies = [100, 1000, 3000, 6000, 10000, 18000]
records = {"sample_rate": fs, "harmonics": [], "separated_orders": [], "limitations": [
    "Native diagnostics, not Plugin Doctor exports or fitted Hammerstein kernels.",
    "ESS uses a common inverse and fixed order windows; stateful/clipped responses need caution.",
    "No independent time alignment or gain normalization between variants.",
]}

def load(kind, fixture, variant):
    path = args.captures / f"{kind}-{fixture}-{variant}.f32"
    raw = path.read_bytes()
    data = np.frombuffer(raw, dtype="<f4").astype(np.float64)
    if len(data) != fs * 3 or not np.isfinite(data).all():
        raise ValueError("invalid native capture")
    (args.output / (path.name + ".gz")).write_bytes(gzip.compress(raw, mtime=0))
    return data

fig, axes = plt.subplots(1, 3, figsize=(15, 4.5), layout="constrained")
for fixture in range(3):
    signals = [load("harmonic", fixture, variant) for variant in ["original", "lookup"]]
    amplitudes = [{}, {}]
    for segment, frequency in enumerate(frequencies):
        start, end = int((segment * .5 + .25) * fs), int((segment + 1) * .5 * fs)
        time = np.arange(start, end) / fs
        for order in range(1, 7):
            if order * frequency >= fs / 2:
                continue
            basis = np.exp(-2j * np.pi * frequency * order * time)
            measured = [2 * abs(np.dot(signal[start:end], basis)) / (end - start) for signal in signals]
            levels = [20 * np.log10(max(amp, 1e-30)) for amp in measured]
            records["harmonics"].append(dict(fixture=fixture, frequency=frequency, order=order,
                original_dbfs=levels[0], lookup_dbfs=levels[1], difference_db=levels[1] - levels[0]))
            for variant in range(2):
                amplitudes[variant][frequency, order] = levels[variant]
    for order, color in [(1, "#2455aa"), (2, "#cc7722"), (3, "#228866")]:
        freq = [f for f in frequencies if (f, order) in amplitudes[0]]
        for variant, style in [(0, "-"), (1, "--")]:
            axes[fixture].semilogx(freq, [amplitudes[variant][f, order] for f in freq], style,
                color=color, marker="o" if variant == 0 else "x", label=f"H{order} {'original' if variant == 0 else 'lookup'}")
    axes[fixture].set(title=names[fixture], xlabel="Input frequency (Hz)", ylabel="Output harmonic (dBFS)", ylim=(-140, 15))
    axes[fixture].grid(alpha=.2)
axes[0].legend(fontsize=8)
fig.suptitle("1.1.4 tube-law trial: native harmonic sweep (same gain, no alignment)")
fig.savefig(args.output / "harmonic-comparison.png", dpi=170)
fig.savefig(args.output / "harmonic-comparison.svg")
plt.close(fig)

# Exponential swept-sine inverse, following Farina's separated-harmonic method.
# Finite windows below are comparison diagnostics, not a complete stateful identification.
count = 2 * fs
time = np.arange(count) / fs
scale = 2 / np.log(1000)
fade = np.clip(np.minimum(time / .01, (2 - time) / .01), 0, 1)
sweep = fade * np.sin(2 * np.pi * 20 * scale * np.expm1(time / scale))
inverse = sweep[::-1] * np.exp(-time / scale)
fft_size = 1 << (5 * fs - 1).bit_length()
inverse_fft = np.fft.rfft(inverse, fft_size)
input_response = np.fft.irfft(np.fft.rfft(sweep, fft_size) * inverse_fft, fft_size)
normalization = np.max(np.abs(input_response))
fundamental_index = count - 1 + int(.1 * fs)
fig, axes = plt.subplots(2, 3, figsize=(14, 7), layout="constrained")
for fixture in range(3):
    signals = [load("sweep", fixture, variant) for variant in ["original", "lookup"]]
    responses = [np.fft.irfft(np.fft.rfft(signal, fft_size) * inverse_fft, fft_size) / normalization for signal in signals]
    for order in range(1, 7):
        center = fundamental_index - int(round(fs * scale * np.log(order)))
        before = int((.04 if order == 1 else .02) * fs)
        after = int((.25 if order == 1 else .02) * fs)
        original, lookup = [response[center-before:center+after] for response in responses]
        energy = np.dot(original, original)
        error = lookup - original
        residual = 10 * np.log10(np.dot(error, error) / max(energy, 1e-30) + 1e-30)
        records["separated_orders"].append(dict(fixture=fixture, order=order, residual_db=float(residual),
            original_window_energy=float(energy), max_window_error=float(np.max(abs(error)))))
        if order in [1, 2]:
            axis = axes[order - 1, fixture]
            milliseconds = np.arange(-before, after) / fs * 1000
            axis.plot(milliseconds, original, color="#2455aa", label="Original")
            axis.plot(milliseconds, lookup, color="#cc7722", linestyle="--", label="Lookup")
            axis.set(title=f"{names[fixture]} — order {order}", xlabel="Time from predicted order arrival (ms)", ylabel="Common-normalized response")
            axis.grid(alpha=.2)
axes[0, 0].legend()
fig.suptitle("Separated-order ESS diagnostic: a shared inverse, no independent gain or time fitting")
fig.savefig(args.output / "separated-order-comparison.png", dpi=170)
fig.savefig(args.output / "separated-order-comparison.svg")
plt.close(fig)
(args.output / "sweep-analysis.json").write_text(json.dumps(records, indent=2) + "\n")
visible = [r for r in records["harmonics"] if r["original_dbfs"] > -100]
print("max harmonic-level difference above -100 dBFS:", max(abs(r["difference_db"]) for r in visible))
print("minimum separated-order window residual:", max(r["residual_db"] for r in records["separated_orders"]))
