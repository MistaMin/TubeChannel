# Tube Channel: design

Manufacturer: OpenGrid. Product: Tube Channel. Framework: JUCE 8 (C++20) + GoodLookinUI
(consumed as a submodule, like HybridEQ; nothing plugin-specific goes into the toolkit).


## 1. Product rules (locked)

1. Audio stages retain the analogue circuit model. Default uses the reduced detector; HQ and
   Derp use the original detector circuit. Both use the same optical response and compressor audio stage.
2. The Analogue / Digital switch changes only the UI skin (analogue-like vs flat digital). It is
   stored in plugin state, is not an audio parameter, and never touches the audio thread.
3. Audio circuits run at the selected oversampled rate. Detector updates run near 48 kHz and
   the original supply model runs at its existing 8 kHz cadence.
4. Power-supply sag is simulated from the real supply circuit and shown as an animated Power panel
   driven by the simulated rail values (not a canned loop).
5. Channel configurations: Mono, Stereo (one control set, optional linked detector), Dual-mono
   (two independent control sets). Each channel has its own component-tolerance seed (recallable).
6. Sidechain input: a detector source switch, Internal or External. External feeds the detector
   input stage (A3A) level-matched to the internal path. The insert loops are not exposed.

## 2. Oversampling

| Quality | Rate | Min-phase (default) | Linear-phase (+L) |
|---|---|---|---|
| Default | 2x at 44.1/48 kHz, reduced detector | IIR half-band cascade | FIR half-band cascade |
| HQ | 2x at 44.1/48 kHz, original detector | IIR | FIR |
| `\( ᐖ)/ - Derp mode` | 4x at 44.1/48 kHz, original detector | IIR | FIR |

The factor scales down as the session rate goes up, so the oversampled rate stays put:

| Session rate | Default | HQ | `\( ᐖ)/ - Derp mode` |
|---|---|---|---|
| 44.1 / 48 kHz | 2x | 2x | 4x |
| 88.2 / 96 kHz | 1x | 1x | 2x |
| 176.4 / 192 kHz | 1x | 1x | 1x |

At 1x the engine is an exact passthrough with no latency, +L included. The UI shows the factor
actually running.

- Two controls: Quality {Default, HQ, `\( ᐖ)/ - Derp mode`} and +L {off, on}.
- Source code and documentation use the full name `\( ᐖ)/ - Derp mode`. Host-facing parameter strings use
  plain ASCII ("Derp mode", "Derp mode +L"). The UI draws the emoticon as vector art.
- +L modes report exact latency to the host. Min-phase modes report 0.
- Mode changes re-initialise the filter bank with a short fade and re-report latency.
- Targets (to be measured, not assumed): >= 100 dB alias rejection, passband ripple < 0.01 dB.

## 3. Signal path

Input (XLR / instrument) -> impedance network and phantom -> discrete input stage -> low cut ->
12AX7 differential tube stage -> phase -> preamp output -> opto compressor (+12AT7 make-up) ->
4-band EQ -> master -> balanced / unbalanced output stage. Meters are high-impedance taps.

Modelling approach per block:
- Passive and active linear filters: derived from the netlist, discretised at the oversampled rate.
- Nonlinear stages (input pair and current mirrors, triodes, plate-load mirrors, Darlington
  follower, opto LDR, detector, clamps): nodal / state-space (DK) solvers, fitted to the supplied
  device data. Op-amps: GBW, slew, output swing, rail-dependent clipping.
- Compressor sidechain: reduced detector in Default, original circuit (Netlists/det.net) in HQ/Derp.
  Opto cell: power-law R(I) with the datasheet 3.5 ms rise and ~72 ms fall.
- Tube stage includes the Warm feedback (R7g, C24, R48, C19, R80 into the tail emitter) and the
  Q17 / Q27 peak-driven bias reduction. The supplied netlist leaves both out, so they are built
  and checked against the NORMAL and HIGH plate voltage behaviour.

## 4. Power supply model

- Transformer (centre-tapped), full-wave rectifier, +/-25 V reservoirs, 7815/7915 regulators with
  the 47R heater bypass resistors, +75 V doubler, +70/+45 V Darlington regulator, +150 V ladder,
  heater series resistors.
- Loads come from the audio circuit state: op-amp output stages, plate currents, phantom (13 mA),
  LED/relay constant loads, heaters.
- **Transformer secondary is 22-0-22 V AC.**
  VSEC = 22 Vrms per half; RSEC and LSEC are fitted so the loaded rails match their design values
  (+25, +75, +45, about +150 V).
- Controls: mains 50/60 Hz (sets ripple frequency), mains voltage (115 / 230 V +/-10 %), Plate Volt
  High/Normal (10 to 20 s slew), phantom on/off (loads the +45 V rail).
- Power panel: per-rail live bar, ripple scope, heater glow, animated current flow. The panel
  reads atomics from the audio thread at 30 to 60 Hz.

## 5. Parameters

Ranges are the design ranges unless marked.

**Input**
- Source: Mic (XLR) / Instrument (high-Z, 2.5 MOhm). Instrument bypasses the mic stage, as the
  front jack does.
- Input trim (plugin): maps dBFS to volts. Mic source impedance (plugin): 150 to 600 ohm.
- Impedance: 150 ohm to 3.4 kohm. Gain: mic 0 to +56 dB, instrument +3 to +50 dB.
- Gain switch (+20 dB tube gain), Phantom 48 V, Invert, Low cut 10 to 250 Hz (6 dB/oct).
- Plate Volt: Normal / High. Preamp output: mute to +10 dB.

**Compressor**
- Active / Bypass. Threshold -20 to +20 dBm . Ratio: the detector circuit's own range, measured
  1.9:1 to 6.7:1 at 6 to 12 dB above the onset (soft knee). The knob spans what the circuit does and is labelled 2:1 to 7:1.
  A click on the RATIO knob pops it up: the circuit compressor and its detector are out (no CPU) and a
  brickwall peak limiter holds the compressor output at the THRESHOLD level, with the RELEASE knob as its
  release; its attack is instant (no look-ahead, no latency).
- Attack about 4 to 33 ms, release about 0.1 to 1.1 s (measured on the circuit with the vactrol). Detect: Auto / Manual.
- Comp output: mute to +10 dB. Vactrol calibration (advanced). Gain-reduction meter.
- Sidechain source: Internal / External. Stereo link on/off.

**EQ** (Active / Bypass, +/-12 dB per band)
- Low shelf, turnover 40 / 120 Hz. High shelf, turnover 6 / 18 kHz.
- Lo-Mid: 20 Hz to 2 kHz (two ranges), Q Narrow / Wide. Hi-Mid: 200 Hz to 20 kHz (two ranges), Q.

**Output**
- Master (gain or attenuation), Output level -10 dBV / +4 dBu, VU source (Pre/Post, Comp/Main).
- Alignment (plugin): 0 VU = +4 dBu = -18 dBFS by default (selectable). Output trim.

**Plugin**
- Quality, +L, Channel config, Link, Seed, Mains, UI skin, A/B, presets, undo.

## 6. Platforms and formats

macOS (arm64 + x86_64 universal), Windows (x64, arm64 optional), Linux (x86_64, arm64 optional).
Formats: AU (macOS), VST3, AAX (macOS, Windows), CLAP (clap-juce-extensions), LV2 (all platforms, native on Linux), Standalone.
One CMake build, portable SIMD only. Windows and Linux binaries need a runner or machine to verify.

## 7. Modelling notes

| Item | Note |
|---|---|
| Transformer | 22-0-22 V secondary; series resistance and leakage inductance are fitted so the loaded rails match their design values. |
| Instrument jack | Plugging in the front jack switches the mic stage off; modelled as the Source switch. |
| Wide / Narrow | The higher band-amp gain (x1.221) gives the higher loop Q, so it is Narrow. |
| Compressor detector | Netlists/det.net, solved as a circuit: threshold network, gain stage with zener soft limit, full-wave rectifier, ratio stage, attack followers and timing capacitor, release network, Auto/Manual, LED driver. The vactrol follows its datasheet curve (steep below 1 mA) and times. |
| Ratio | The circuit's own range, measured 1.8:1 to 6.8:1 at 6 to 12 dB above the onset (soft knee), labelled 2:1 to 7:1. Clicking the knob swaps the circuit compressor for a brickwall limiter. |
| Attack / release | About 4 to 33 ms and 0.1 to 1.1 s, measured on the circuit with the vactrol. |
| Warm network | In the audio path: the tube stage output feeds a network into the tail of the input pair, and a peak detector reduces the bias. |
| Maximum levels | Mic input +18 dBu, balanced output +24 dBu, unbalanced +20 dBu. |

## 8. Verification

- Reference simulation of each block (LTspice netlists) against the DSP: frequency response,
  THD, transients, rail voltages and sag.
- Oversampling: alias sweeps, latency and null tests in every Quality and +L combination.
- Format checks: pluginval, auval, AAX validator; DAW passes; sign and notarise.
- CPU profile per Quality mode. The oversampled rate is never reduced to save CPU.
