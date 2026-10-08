# Compression timing plots

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

`recovery-times.json` measures when each trace first falls to its resting GR plus
3 dB after the second burst stops. Linear interpolation between the approximately
2 ms CSV points gives 2.016 s for the circuit and 2.503 s for the prototype in
stress case 6: about 486 ms extra recovery time. This is a threshold-crossing
comparison, not a constant time shift or the panel's nominal release time.
