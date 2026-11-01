# Tube Channel QA ledger — 2026-11-01 DSP/QA pass

Scope sources: README.md, docs/USER_MANUAL.md, docs/RELEASE_1.2.3.md, docs/measurements/,
plugin-qa/projects/tubechannel.md, qa/exceptions.txt, QA report commit 02a4705 (2026-10-31).

Build under test: HEAD 4d22fe1 (bundle-release), Release, universal (arm64+x86_64), Ninja
build/universal. Formats shipped: VST3, AU, CLAP, LV2, AAX (+Standalone local).

| # | Item | Source | Code | GUI | Test | Evidence | Status |
|---|---|---|---|---|---|---|---|
| S1 | ~84 host parameters load, automatable | manual | Source/Plugin/Params.cpp | — | REAPER suite | report | todo |
| S2 | Every parameter changes sound (or exception/context) | qa/exceptions.txt | Source/Channel | — | audio sweep | report | todo |
| S3 | Every parameter visible in GUI (Offline Bounce excused: popup selection) | qa/exceptions.txt | Source/Plugin/PluginEditor.cpp | — | visual sweep | report | todo |
| S4 | Editor opens; analogue/digital skins; A/B, undo/redo | manual | PluginEditor | — | GUI test + suite | ctest gui pass | verified |
| S5 | State survives save/reopen (incl. B channel, presets) | suite | setStateInformation | — | state check | report | todo |
| S6 | ctest 18/18 (circuit, eco, detectors, chain, offline bounce, eq-oversampling, stress) | profile | tests/ | — | ctest | 100% passed (2026-11-01) | verified |
| S7 | VST3 pluginval strict 10 | profile | — | — | pluginval | SUCCESS (2026-11-01, after fixes) | verified |
| S8 | CLAP clap-validator 44 tests | profile | — | — | clap-validator | 32 passed, 0 failed (2026-11-01, after fixes) | verified |
| S9 | AU auval PASS | profile | — | — | auval aufx TbCh OpGr | AU VALIDATION SUCCEEDED (2026-11-01) | verified |
| S10 | Parameter text round trips (dB/Hz/ohm/ms/ratio/·) | clap-validator | Params.cpp parse lambdas | — | param-conversions | fixed 2026-11-01, 0 failures | verified |
| S11 | State restore re-announces to host; CLAP paramsRescan after load | clap-validator | setStateInformation | — | state-reproducibility | fixed 2026-11-01, 0 failures | verified |
| S12 | Bool params report exact 0/1 to hosts | pluginval | Params.cpp SwitchParameter | — | pluginval state restoration | fixed 2026-11-01 | verified |
| S13 | AAX PACE-signed | RELEASE_NOTES | AAX bundle | — | wraptool verify | signed (2026-10-31) | verified |
| S14 | dsp_lint clean | dsp rule 9 | — | — | dsp_lint | 0 findings (2026-11-01) | verified |
| S15 | Payload lint clean | dsp rule 9 | — | — | dsp_lint --payload | 0 findings (2026-11-01) | verified |
| S16 | Standalone icon | CHANGELOG 26b8789 | CMake ICON_BIG/SMALL | app | check_standalone_icon | AppIcon.icns verified | verified |
| S17 | Latency/alloc-free callback | tests | processor | — | ctest (stress, detector_guard) | pass | verified |

Fixes made this pass (all committed as 4d22fe1, pushed to main):
- Params.cpp/Params.h: denormalised text parsers for every float parameter; UTF-8 choice names +
  8-bit-mojibake-tolerant choice matching.
- Params.cpp: SwitchParameter bools (exact 0/1 host reads).
- PluginProcessor.cpp: pre/post restore re-announce + updateHostDisplay(programChanged).

Not run this pass (SKIPPED): Pro Tools AAX acceptance (no admin AAX install), Studio One interactive.
