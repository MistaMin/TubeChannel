# Plugin QA: VST3: Tube Channel

| Check | Status | Detail |
|---|---|---|
| loads | PASS | VST3: Tube Channel (OpenGrid), 88 parameters |
| finite output | PASS | peak -9.0 dBFS |
| not silent | PASS | rms -20.1 dBFS |
| deterministic render | PASS | repeat differs by -240.0 dB |
| editor opens | PASS | 1215x347, 0.0% animates by itself |
| every parameter changes the sound | FAIL | Detect auto B, External sidechain B |
| every parameter is shown in the GUI | PASS |  |
| state survives save and reopen | PASS |  |
| editor reopens | PASS | 5 close/open cycles |

| # | Parameter | Context | Audio | Visual |
|---|---|---|---|---|
| 0 | Instrument input |  | ok (-13.0 dB) | ok (0.399%) |
| 1 | Gain |  | ok (1.2 dB) | ok (0.464%) |
| 2 | Gain +20 dB |  | ok (-2.8 dB) | ok (0.399%) |
| 3 | Impedance |  | ok (-22.9 dB) | ok (0.399%) |
| 4 | Phantom 48V | random state 4 | CONDITIONAL (5.3 dB) | ok (0.399%) |
| 5 | Invert |  | ok (-2.5 dB) | ok (0.399%) |
| 6 | Low cut |  | ok (-13.2 dB) | ok (0.432%) |
| 7 | Plate voltage high |  | ok (-44.8 dB) | ok (0.399%) |
| 8 | Preamp output |  | ok (-9.0 dB) | ok (0.432%) |
| 9 | Compressor on |  | ok (-16.5 dB) | ok (0.399%) |
| 10 | Threshold |  | ok (-13.1 dB) | ok (0.433%) |
| 11 | Ratio |  | ok (-14.9 dB) | ok (0.430%) |
| 12 | Attack |  | ok (-23.6 dB) | ok (0.399%) |
| 13 | Release |  | ok (-29.2 dB) | ok (0.399%) |
| 14 | Detect auto |  | ok (-32.8 dB) | ok (0.399%) |
| 15 | Comp output |  | ok (-7.6 dB) | ok (0.432%) |
| 16 | External sidechain |  | ok (-13.1 dB) | ok (0.399%) |
| 17 | Brickwall limiter |  | ok (-10.2 dB) | ok (0.648%) |
| 18 | EQ on |  | ok (-21.6 dB) | ok (0.399%) |
| 19 | Low |  | ok (-1.8 dB) | ok (0.435%) |
| 20 | Low shift 40 Hz |  | ok (-59.0 dB) | ok (0.399%) |
| 21 | High |  | ok (-7.5 dB) | ok (0.399%) |
| 22 | High shift 6 kHz |  | ok (-61.6 dB) | ok (0.399%) |
| 23 | Lo-Mid level |  | ok (-0.6 dB) | ok (0.435%) |
| 24 | Lo-Mid freq |  | ok (-73.2 dB) | ok (0.399%) |
| 25 | Lo-Mid x10 |  | ok (-61.9 dB) | ok (0.399%) |
| 26 | Lo-Mid narrow |  | ok (-94.6 dB) | ok (0.399%) |
| 27 | Hi-Mid level |  | ok (0.2 dB) | ok (0.435%) |
| 28 | Hi-Mid freq |  | ok (-55.5 dB) | ok (0.399%) |
| 29 | Hi-Mid x10 |  | ok (-45.8 dB) | ok (0.399%) |
| 30 | Hi-Mid narrow |  | ok (-76.4 dB) | ok (0.399%) |
| 31 | Master |  | ok (-9.0 dB) | ok (0.399%) |
| 32 | Output +4 dBu |  | ok (-11.6 dB) | ok (0.399%) |
| 33 | Input trim |  | ok (0.3 dB) | ok (0.399%) |
| 34 | Output trim |  | ok (-0.6 dB) | ok (0.399%) |
| 35 | 0 VU level |  | ok (-18.7 dB) | ok (0.399%) |
| 36 | Mic source impedance |  | ok (-18.7 dB) | ok (0.399%) |
| 37 | Instrument source impedance | random state 2 | CONDITIONAL (-19.2 dB) | ok (0.399%) |
| 38 | Quality | random state 4 | CONDITIONAL (5.1 dB) | ok (0.399%) |
| 39 | Linear phase (+L) | random state 1 | CONDITIONAL (0.0 dB) | ok (0.399%) |
| 40 | Stereo link |  | ok (-26.4 dB) | ok (0.399%) |
| 41 | Dual mono |  | ok (-26.4 dB) | ok (0.794%) |
| 42 | Mains frequency | random state 4 | CONDITIONAL (5.9 dB) | ok (0.399%) |
| 43 | Mains voltage | random state 4 | CONDITIONAL (5.8 dB) | ok (0.399%) |
| 44 | Component tolerances |  | ok (-65.1 dB) | ok (0.399%) |
| 45 | Unit seed |  | ok (-68.0 dB) | ok (0.399%) |
| 46 | Circuit noise | random state 2 | CONDITIONAL (-89.0 dB) | ok (0.399%) |
| 47 | Power supply model | random state 4 | CONDITIONAL (5.5 dB) | ok (0.399%) |
| 48 | Instrument input B | random state 1 | CONDITIONAL (-13.0 dB) | ok (0.399%) |
| 49 | Gain B | random state 1 | CONDITIONAL (-3.4 dB) | ok (0.399%) |
| 50 | Gain +20 dB B | random state 1 | CONDITIONAL (-12.1 dB) | ok (0.399%) |
| 51 | Impedance B | random state 1 | CONDITIONAL (-12.6 dB) | ok (0.399%) |
| 52 | Phantom 48V B | random state 4 | CONDITIONAL (5.5 dB) | ok (0.399%) |
| 53 | Invert B | random state 1 | CONDITIONAL (5.2 dB) | ok (0.399%) |
| 54 | Low cut B | random state 1 | CONDITIONAL (-15.0 dB) | ok (0.399%) |
| 55 | Plate voltage high B | random state 1 | CONDITIONAL (-29.9 dB) | ok (0.399%) |
| 56 | Preamp output B | random state 1 | CONDITIONAL (-14.3 dB) | ok (0.399%) |
| 57 | Compressor on B | random state 1 | CONDITIONAL (-11.6 dB) | ok (0.399%) |
| 58 | Threshold B | random state 1 | CONDITIONAL (-1.0 dB) | ok (0.399%) |
| 59 | Ratio B | random state 4 | CONDITIONAL (5.6 dB) | ok (0.399%) |
| 60 | Attack B | random state 4 | CONDITIONAL (5.1 dB) | ok (0.399%) |
| 61 | Release B | random state 1 | CONDITIONAL (-39.5 dB) | ok (0.399%) |
| 62 | Detect auto B |  | DEAD (-240.0 dB) | ok (0.399%) |
| 63 | Comp output B | random state 1 | CONDITIONAL (-12.5 dB) | ok (0.399%) |
| 64 | External sidechain B |  | DEAD (-240.0 dB) | ok (0.399%) |
| 65 | Brickwall limiter B | random state 1 | CONDITIONAL (-3.6 dB) | ok (0.399%) |
| 66 | EQ on B | random state 1 | CONDITIONAL (-10.1 dB) | ok (0.399%) |
| 67 | Low B | random state 1 | CONDITIONAL (-62.3 dB) | ok (0.399%) |
| 68 | Low shift 40 Hz B | random state 1 | CONDITIONAL (-85.8 dB) | ok (0.399%) |
| 69 | High B | random state 1 | CONDITIONAL (-46.9 dB) | ok (0.399%) |
| 70 | High shift 6 kHz B | random state 1 | CONDITIONAL (-63.2 dB) | ok (0.399%) |
| 71 | Lo-Mid level B | random state 1 | CONDITIONAL (-62.0 dB) | ok (0.399%) |
| 72 | Lo-Mid freq B | random state 1 | CONDITIONAL (-70.5 dB) | ok (0.399%) |
| 73 | Lo-Mid x10 B | random state 1 | CONDITIONAL (-77.1 dB) | ok (0.399%) |
| 74 | Lo-Mid narrow B | random state 1 | CONDITIONAL (-90.5 dB) | ok (0.399%) |
| 75 | Hi-Mid level B | random state 1 | CONDITIONAL (-68.4 dB) | ok (0.399%) |
| 76 | Hi-Mid freq B | random state 1 | CONDITIONAL (-87.5 dB) | ok (0.399%) |
| 77 | Hi-Mid x10 B | random state 1 | CONDITIONAL (-82.0 dB) | ok (0.399%) |
| 78 | Hi-Mid narrow B | random state 2 | CONDITIONAL (-15.4 dB) | ok (0.399%) |
| 79 | Master B | random state 1 | CONDITIONAL (-0.2 dB) | ok (0.399%) |
| 80 | Output +4 dBu B | random state 1 | CONDITIONAL (-3.5 dB) | ok (0.399%) |
| 81 | Eco mode |  | ok (-18.5 dB) | ok (0.399%) |
| 82 | Oversampling |  | ok (-6.9 dB) | ok (0.399%) |
| 83 | Bypass |  | ok (-8.0 dB) | ok (0.399%) |
| 84 | Program |  | ok (-15.0 dB) | ok (0.704%) |
| 85 | Bypass |  | ok (-8.0 dB) | ok (0.800%) |
| 86 | Wet |  | ok (-8.0 dB) | ok (0.399%) |
| 87 | Delta |  | ok (-3.2 dB) | ok (0.399%) |
