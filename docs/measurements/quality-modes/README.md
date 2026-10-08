# Quality-mode implementation — v1.1.2

The user selected both detector responses in the existing quality selector:

| Mode | Compressor detector | 44.1/48 kHz | 88.2/96 kHz | 176.4/192 kHz |
| --- | --- | --- | --- | --- |
| Default | Round-8 reduced prototype | 2x | 1x | 1x |
| HQ | Original circuit | 2x | 1x | 1x |
| Derp | Original circuit | 4x | 2x | 1x |

The preamp, tube/warm feedback, compressor audio path, optical response, EQ, output and supply keep
their original implementations. +L retains linear-phase filtering and host-reported latency. The
quality parameter ID, choice indices and saved states remain compatible; its host display name is
now Quality. Switching modes follows the existing prepared-chain handover and short output fade,
without adding a DSP thread. The system panel reports the active compressor choice and rate.

The prototype's measured timing differences are intentionally available in Default. Selecting HQ
restores the original detector response at the same audio oversampling rate. The prior global
experimental-detector build flag was retired so it cannot silently force HQ to use the prototype.
Offline tools can explicitly force either detector with `ChainConfig::detectorMode`.

## Checks

- All 11 native tests passed (`native-tests.txt`).
- `quality_mode_test.cpp` compares complete stereo outputs at 48/96 kHz with separately forced
  backends: Default matches the prototype exactly, HQ matches the original at Default's factor
  exactly, and Derp matches the original at its higher factor exactly. Default and HQ produce
  different compressed audio despite equal factors.
- The processor and GUI tests passed (`host-tests.txt`). Host checks cover all three qualities,
  minimum/linear phase at 96 kHz, live mode changes at equal and differing factors, retirement-slot
  reuse and saved HQ restoration after Default. The active backend is checked after each adoption.
- Expanded real-time allocation checks pass 15 scenarios, including actual Default/HQ/Derp stereo
  modes at 48 kHz with automation, slow release, Auto, compressor bypass and limiter switching.
  `realtime-scenarios.txt` records zero allocations/frees and finite audio in all scenarios.
- Existing oversampler tests cover 44.1/48/96/192 kHz and both phase choices. The factor table is
  explicitly checked through 384 kHz.

The arm64 Release VST3, AU and standalone were rebuilt. `build-identity.json` records their paths,
versions and executable hashes. These are local build artifacts, not a signed/notarized installer.
Plugin Doctor was being used for another plugin; its session was preserved. No fresh analyzer or
CPU results are claimed for this mode-wiring change. The earlier [calibration measurements](../hybrid-calibration/README.md)
remain diagnostic evidence of the two detector responses; the independent quality tests verify
that each new mode selects the intended existing implementation.

The 80% CPU aspiration and multi-instance OpenGrid playback reliability remain unproven. Changing
this selection does not repair the prototype's documented timing errors; HQ provides the original
response, and Default makes the measured CPU/sound tradeoff available.
