# Plugin QA: VST3: Tube Channel

| Check | Status | Detail |
|---|---|---|
| loads | PASS | VST3: Tube Channel (OpenGrid), 90 parameters |
| finite output | PASS | peak -7.5 dBFS |
| not silent | PASS | rms -21.0 dBFS |
| deterministic render | PASS | repeat differs by -240.0 dB |
| editor opens | PASS | 1822x521, 0.0% animates by itself |
| every parameter changes the sound | PASS |  |
| every parameter is shown in the GUI | PASS |  |
| state survives save and reopen | PASS |  |
| editor reopens | PASS | 5 close/open cycles |

| # | Parameter | Context | Audio | Visual |
|---|---|---|---|---|
| 0 | Instrument input |  | ok (-10.3 dB) | ok (0.506%) |
| 1 | Gain |  | ok (-0.2 dB) | ok (0.382%) |
| 2 | Gain +20 dB |  | ok (-1.9 dB) | ok (0.527%) |
| 3 | Impedance |  | ok (-18.7 dB) | ok (0.368%) |
| 4 | Phantom 48V |  | ok (-81.9 dB) | ok (0.481%) |
| 5 | Invert |  | ok (-0.7 dB) | ok (0.480%) |
| 6 | Low cut |  | ok (-13.4 dB) | ok (0.369%) |
| 7 | Plate voltage high |  | ok (-95.4 dB) | ok (0.521%) |
| 8 | Preamp output |  | ok (-7.7 dB) | ok (0.339%) |
| 9 | Compressor on |  | ok (-12.7 dB) | ok (0.521%) |
| 10 | Threshold |  | ok (-12.3 dB) | ok (0.370%) |
| 11 | Ratio |  | ok (-16.7 dB) | ok (0.333%) |
| 12 | Attack |  | ok (-22.4 dB) | ok (0.343%) |
| 13 | Release |  | ok (-24.6 dB) | ok (0.365%) |
| 14 | Detect auto |  | ok (-24.5 dB) | ok (0.490%) |
| 15 | Comp output |  | ok (-6.4 dB) | ok (0.369%) |
| 16 | External sidechain |  | ok (-12.3 dB) | ok (0.514%) |
| 17 | Brickwall limiter |  | ok (-8.5 dB) | ok (0.447%) |
| 18 | EQ on |  | ok (-23.9 dB) | ok (0.487%) |
| 19 | Low |  | ok (-2.1 dB) | ok (0.342%) |
| 20 | Low shift 40 Hz |  | ok (-60.2 dB) | ok (0.510%) |
| 21 | High |  | ok (-5.5 dB) | ok (0.370%) |
| 22 | High shift 6 kHz |  | ok (-77.6 dB) | ok (0.484%) |
| 23 | Lo-Mid level |  | ok (-0.8 dB) | ok (0.343%) |
| 24 | Lo-Mid freq |  | ok (-69.3 dB) | ok (0.342%) |
| 25 | Lo-Mid x10 |  | ok (-58.9 dB) | ok (0.481%) |
| 26 | Lo-Mid narrow |  | ok (-91.9 dB) | ok (0.498%) |
| 27 | Hi-Mid level |  | ok (0.1 dB) | ok (0.342%) |
| 28 | Hi-Mid freq |  | ok (-53.0 dB) | ok (0.342%) |
| 29 | Hi-Mid x10 |  | ok (-41.4 dB) | ok (0.481%) |
| 30 | Hi-Mid narrow |  | ok (-72.7 dB) | ok (0.497%) |
| 31 | Master |  | ok (-7.9 dB) | ok (0.346%) |
| 32 | Output +4 dBu |  | ok (-10.1 dB) | ok (0.489%) |
| 33 | Input trim |  | ok (-1.8 dB) | ok (0.456%) |
| 34 | Output trim |  | ok (-0.6 dB) | ok (0.326%) |
| 35 | 0 VU level |  | ok (-20.3 dB) | ok (0.318%) |
| 36 | Mic source impedance |  | ok (-15.0 dB) | ok (0.327%) |
| 37 | Instrument source impedance | random state 2 | CONDITIONAL (-19.2 dB) | ok (0.325%) |
| 38 | Quality | Eco mode=0, Offline Bounce override=0 | ok (-27.1 dB) | ok (0.355%) |
| 39 | Linear phase (+L) |  | ok (-5.8 dB) | ok (0.327%) |
| 40 | Stereo link |  | ok (-30.5 dB) | ok (0.356%) |
| 41 | Dual mono |  | ok (-30.5 dB) | ok (0.710%) |
| 42 | Mains frequency |  | ok (-77.9 dB) | ok (0.325%) |
| 43 | Mains voltage |  | ok (-63.0 dB) | ok (0.355%) |
| 44 | Component tolerances | random state 1 | CONDITIONAL (-67.5 dB) | ok (0.325%) |
| 45 | Unit seed |  | ok (-97.7 dB) | ok (0.354%) |
| 46 | Circuit noise | random state 2 | CONDITIONAL (-89.0 dB) | ok (0.317%) |
| 47 | Power supply model |  | ok (-83.5 dB) | ok (0.354%) |
| 48 | Instrument input B | random state 1 | CONDITIONAL (-13.0 dB) | ok (0.354%) |
| 49 | Gain B | random state 1 | CONDITIONAL (-3.4 dB) | ok (0.354%) |
| 50 | Gain +20 dB B | random state 1 | CONDITIONAL (-12.1 dB) | ok (0.354%) |
| 51 | Impedance B | random state 1 | CONDITIONAL (-12.6 dB) | ok (0.327%) |
| 52 | Phantom 48V B | Dual mono=1 | ok (-83.3 dB) | ok (0.709%) |
| 53 | Invert B | random state 1 | CONDITIONAL (5.2 dB) | ok (0.327%) |
| 54 | Low cut B | random state 1 | CONDITIONAL (-15.0 dB) | ok (0.354%) |
| 55 | Plate voltage high B | random state 1 | CONDITIONAL (-29.9 dB) | ok (0.327%) |
| 56 | Preamp output B | random state 1 | CONDITIONAL (-14.3 dB) | ok (0.354%) |
| 57 | Compressor on B | random state 1 | CONDITIONAL (-11.6 dB) | excused (0.355%) |
| 58 | Threshold B | random state 1 | CONDITIONAL (-1.0 dB) | ok (0.326%) |
| 59 | Ratio B | random state 5 | CONDITIONAL (5.4 dB) | ok (0.325%) |
| 60 | Attack B | random state 5 | CONDITIONAL (2.2 dB) | ok (0.323%) |
| 61 | Release B | random state 1 | CONDITIONAL (-39.5 dB) | ok (0.354%) |
| 62 | Detect auto B | Dual mono=1 | ok (-24.5 dB) | ok (0.710%) |
| 63 | Comp output B | random state 1 | CONDITIONAL (-12.5 dB) | ok (0.354%) |
| 64 | External sidechain B | Dual mono=1 | ok (-14.5 dB) | ok (0.709%) |
| 65 | Brickwall limiter B | random state 1 | CONDITIONAL (-3.6 dB) | excused (0.354%) |
| 66 | EQ on B | random state 1 | CONDITIONAL (-10.1 dB) | ok (0.325%) |
| 67 | Low B | random state 1 | CONDITIONAL (-62.3 dB) | ok (0.327%) |
| 68 | Low shift 40 Hz B | random state 1 | CONDITIONAL (-85.8 dB) | ok (0.325%) |
| 69 | High B | random state 1 | CONDITIONAL (-46.9 dB) | ok (0.326%) |
| 70 | High shift 6 kHz B | random state 1 | CONDITIONAL (-63.2 dB) | ok (0.327%) |
| 71 | Lo-Mid level B | random state 1 | CONDITIONAL (-62.0 dB) | ok (0.354%) |
| 72 | Lo-Mid freq B | random state 1 | CONDITIONAL (-70.5 dB) | ok (0.326%) |
| 73 | Lo-Mid x10 B | random state 1 | CONDITIONAL (-77.1 dB) | ok (0.326%) |
| 74 | Lo-Mid narrow B | random state 1 | CONDITIONAL (-90.5 dB) | ok (0.327%) |
| 75 | Hi-Mid level B | random state 1 | CONDITIONAL (-68.4 dB) | ok (0.353%) |
| 76 | Hi-Mid freq B | random state 1 | CONDITIONAL (-87.5 dB) | ok (0.326%) |
| 77 | Hi-Mid x10 B | random state 1 | CONDITIONAL (-82.0 dB) | ok (0.354%) |
| 78 | Hi-Mid narrow B | random state 2 | CONDITIONAL (-15.4 dB) | ok (0.326%) |
| 79 | Master B | random state 1 | CONDITIONAL (-0.2 dB) | ok (0.327%) |
| 80 | Output +4 dBu B | random state 1 | CONDITIONAL (-3.5 dB) | ok (0.326%) |
| 81 | Eco mode | random state 1 | CONDITIONAL (-3.1 dB) | ok (1.366%) |
| 82 | Oversampling | random state 1 | CONDITIONAL (-2.2 dB) | ok (0.355%) |
| 83 | Offline Bounce |  | ok (-9.3 dB) | excused (0.354%) |
| 84 | Offline Bounce override |  | ok (-7.5 dB) | ok (0.327%) |
| 85 | Bypass |  | ok (-4.1 dB) | excused (0.270%) |
| 86 | Program |  | ok (-14.8 dB) | ok (0.572%) |
| 87 | Bypass |  | ok (-4.1 dB) | excused (0.490%) |
| 88 | Wet |  | ok (-4.1 dB) | ok (0.330%) |
| 89 | Delta |  | ok (-3.2 dB) | ok (0.407%) |
