# Initial hybrid prototypes

These captures evaluate the first compact detector/supply implementations. They are development
fixtures, not an accepted sound match or released v1.1.2 hybrid plugin.

## Detector accuracy

`detector.csv` records six 48 kHz, two-second 1 kHz burst/recovery cases without tolerances:
manual and Auto at short, middle and long attack/release. The signal is 1.8 V peak, active
at 0.1–0.5 and 0.9–1.1 seconds. LED currents feed identical retained optical-response models.
The decimated CSV retains every 16th sample; summary errors use every sample.

Reference solves converged and both paths stayed finite. Maximum optical gain-reduction errors
range from 3.265 to 18.698 dB. Slow manual and Auto recovery are particularly inaccurate.
The initial estimates must be fitted before use. Finite output is a stability check, not sound acceptance.

## CPU baseline

Stereo Default 2x, 48 kHz/1,024 samples, tolerance and supply simulation On, input peak 0.8,
220 Hz bursts, maximum manual attack/release, no automation. Each run measures 299 callbacks
after 51 warmup callbacks. Runs were sequential with no concurrent build/test or loaded plugins
in the analyzer, in forward/reverse variant order. Other system scheduling remains uncontrolled.

| Variant | Mean process CPU per callback | Reduction from reference |
| --- | ---: | ---: |
| Circuit reference | 13.587 ms | — |
| Compact detector, circuit supply | 11.297 ms | 16.9% |
| Compact detector and supply | 10.077 ms | 25.8% |

Raw callback-tail and deadline results are in benchmark-*.txt; benchmark-summary.json retains
settings and aggregation. Some wall-time callbacks still missed the 21.333 ms deadline.
Output peak changed from about 0.773 to 0.833, and detector accuracy failed the sound match.
The saving cannot be attributed entirely to equivalent work: changed gain reduction changes
loads and operating conditions of the retained circuits. Re-measure after fitting, with actual
host playback, automation, recordings and multiple instances. The 80% goal has not been achieved.
