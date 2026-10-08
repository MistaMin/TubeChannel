# Plugin Doctor comparison protocol

Use the supplied Plugin Doctor manual v2.7 as the operating reference for the updated v2.7.2 app. The first session used the
installed v2.3.7; repeat baselines after an update rather than combining unverified measurements
across analyzer versions. The manual is application documentation, not authorization to change
unrelated settings, upload recordings or contact its support address.

## Current quality-mode comparisons

For the new 1.1.2 mode mapping, load the same current VST3 in both chains, with HQ as the original
2x detector reference and Default as the prototype at 2x, at 44.1/48 kHz. Match all other controls,
noise, supply, tolerances and seed. Derp selects the original detector at 4x and must be measured
separately because the audio-stage rate also changes. Reload modules after a build; earlier loaded
instances can still run the older binary. Record identity from `measurements/quality-modes`.

The sessions documented below are historical comparisons before this mode mapping.

## Plugin identity and states

- Chain 1: installed Tube Channel v1.0.0 VST3.
- Chain 2: local solver-fixed v1.1.1 VST3 from the arm64 Release build.
- Source development version: 1.1.2. An opt-in hybrid VST3 is built separately in `build/hybrid-plugin`; it requires separate measurements.
- Load bundles from their exact paths and record versions and hashes. Future hybrid candidates
  require fresh builds and their own identity records.
- Begin with Init on both plugins, identical controls, Default 2x at 48 kHz, minimum-phase filtering,
  alignment, trims, seed, tolerance, stereo/mono configuration and supply/noise settings.
- For clean response/distortion tests, explicitly verify noise disabled in both instances. Record
  supply and tolerance settings; follow with their normal enabled settings as a separate test.
- For coloration isolation, bypass compression and EQ identically, then test the complete channel.
  Use matched drive controls and output gain. Do not normalize away an unexplained gain difference.

## Analyzer setup

Use 48 kHz first, then 44.1/96 kHz checks. Record the FFT quality and processing-speed settings.
The manual specifies Normal = 16,384 samples, Higher = 32,768 and Highest = 65,536 for Linear
and Harmonic analysis. These sizes are not the 1,024-sample OpenGrid playback buffer.

Use the 1|2 parallel display for independent overlays, and LR mode for ordinary channel comparison.
Treat 1-2 as the analyzer's documented subtraction/serial mode, not an independently established
audio null test. Keep CPU measurements separate from display and curve-fitting work. Realtime
analysis speed avoids confusing an intentionally accelerated analyzer with ordinary host playback.
Do not use external hardware routing for these software comparisons.

## Measurement set

| Test | Procedure | What to compare |
| --- | --- | --- |
| Linear magnitude and phase | Delta stimulus, start at -30 dBFS and repeat at -18 dBFS; inspect the noise floor and nonlinear response | Overall gain, low cut, response/phase through the band, EQ shapes and switches |
| Harmonics | Single sine, initially 1 kHz at -30/-18/-6 dBFS; repeat at low and high frequencies and both tube/plate settings | Fundamental, harmonic pattern, THD and THD+N; inspect actual overload rather than assuming input level implies it |
| Intermodulation | Documented IMD stimulus: 60 Hz and 7 kHz, high tone 12 dB below low tone | Modulation sidebands and IMD, with identical drive settings |
| Aliasing | 2D sine sweep, same frequency range, level, sweep law and color limits; select each chain separately | Reflected harmonic traces and their levels. Near sample-rate/4, disappearing THD does not establish low aliasing |
| Compression curve | Dynamics Ramp, reduced documented input range where useful; 1.5 s per step initially | Threshold, ratio, gain and program dependence; increase settling coverage if state is still evolving |
| Timing | Dynamics Attack/Release, 1 kHz sine with below/above/below-threshold sections; make sections long enough to observe slow settling | Attack, long release, overshoot, Auto/manual mode and recovery. Repeat the user's maximum-attack/Auto case |
| Waveform | Oscilloscope Time and Waveshaping modes; inspect delay alignment explicitly | Asymmetry, clipping and transient memory; reported zero latency does not imply zero phase delay |
| Performance | Separate runs at 48 kHz with varying callback sample counts, including 128/256/512/1,024 | Callback time and variability, verified against standalone benchmark and multi-instance host playback |

Keep all three Attack/Release segment levels and durations in the result record. Ramp's maximum
amplitude measurement is not automatically a fully settled compression curve. State histories,
warmup and long release behavior need explicit checks.

Hammerstein analysis can help characterize frequency-dependent nonlinear coloration, but a fitted
power-series model is not proof that it captures a feedback compressor's dynamic behavior.

## Evidence and reporting

Export raw curves per test and plugin, with explicit filenames. Verify the export format before
assigning columns or segments to plugins/channels. In the first v2.3.7 two-plugin linear export,
four monotonic frequency segments were present but only two had graph/thread headers; do not
infer channel identities solely from those incomplete headers. Prefer separate identified exports
or independently verified mappings.

The initial two-plugin overlay and temporary export are provisional: final matching of noise/state
settings and settling remains to be re-verified. They do not establish hybrid-engine accuracy or a
CPU improvement. Preserve private license details and recordings outside repository artifacts.

Report analyzer version, plugin identity, state, stimulus, sample rate, FFT size, routing, warmup,
measurement mode and limitations alongside every result. Retain numerical exports and level-matched
listening references. Plugin Doctor measurements support the listening comparison; they do not
produce a meaningful percentage for "90% of the sound".


## Verified v2.7.2 session

Both plugins were freshly loaded into chain 1/slot 1 and chain 2/slot 1 respectively. Analyzer
settings were verified as 48 kHz, Higher FFT quality (32,768), Realtime speed and parallel LR
comparison. Both started in Init with tolerance and supply simulation enabled, seed 1 and
-18 dBFS alignment; noise was explicitly set Off in both accessible plugin editors.

The v2.7.2 text export retained the incomplete-header behavior. Bypassing chain 2 produced flat
-30 dB ordinates in frequency segments 3/4 while segments 1/2 retained the processed response.
This independently verifies segments 1/2 as chain 1 and segments 3/4 as chain 2 for this capture
format. Restore chain 2 before measurements. Do not assume the mapping for an unrelated export
mode without checking its structure.

Reference executable SHA-256: `a5400507b3e6ce5a7ff7076c158b394aed0288b04c27fbd10234039d22fbf002`.
Solver-fixed executable SHA-256: `f79021806cb0fd0ec355bf953be0435934a38dd56c27c7c1dc1bff9bdcf67e8c`.
These identify v1.0.0 and v1.1.1; no v1.1.2 hybrid measurement exists.

## Reopened-session sweep checks

Order-six Hammerstein and both separate-chain 2D sweeps completed after restarting the analyzer
and selecting Normal FFT quality. Empty plots before processing completed were not results.
A bypassed-chain 2D control produced the expected fundamental trace; both plugin bypasses were
restored Off before the active captures. See measurements/plugin-doctor-2.7.2 for exports.

The 1D THD and fundamental sweeps completed with Normal FFT and Ultra analysis speed. Keep these settings
distinct from the earlier Higher/Realtime linear and single-tone baseline. Ultra timings must
not be used as ordinary host callback performance. The 2D axis labels showed 22.1 kHz at the
top although the settings showed 48 kHz; this discrepancy remains unresolved and restricts
quantitative alias-frequency conclusions.

After captures, Higher FFT quality and Realtime speed were restored, with sweep mode Off.
