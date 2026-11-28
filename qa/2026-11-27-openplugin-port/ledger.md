# Scope ledger: Tube Channel 2.0.0 (port to OpenPluginSDK)

Project: Tube Channel   Branch: openplugin-port (same as main)   Started: 2026-11-27
SDK: OpenPluginSDK git revision d93d926. Build under test: "Tube Channel QA" (test identity), arm64,
release configuration, installed in the user plug-in folders.

Sources of scope:
- Request: port to OpenPluginSDK, full QA, release 2.0.0 for macOS universal, Windows and Linux.
- `docs/RELEASE_2.0.0.md` (what is the same, new, changed and removed).
- The 1.2.x release: every parameter, id, range, default and text (`plugin/port-scan/*.manifest.json`),
  and states saved by 1.x (`plugin/tests/fixtures`).

Evidence files are in this folder unless a path says otherwise.

## Items

| ID | Item | Source | Code | GUI | Test | Evidence | Status |
|---|---|---|---|---|---|---|---|
| S1 | Every 1.x parameter keeps id, order, range, default and text | release notes | `plugin/src/params.rs` | all pages | `tests/params.rs`: `clap_rows_are_the_release_rows_in_order`, `vst3_and_au_ids_are_the_release_ids` | full tier PASSED (T-TEST) | verified |
| S2 | Projects and presets saved with 1.x open with the same settings (CLAP, VST3, AU) | release notes | `plugin/src/lib.rs` (`import_original`) | none: state | `tests/state.rs`: `clap_states_open`, `vst3_states_open`, `audio_unit_states_open`, `a_released_state_without_the_later_rows_keeps_its_engine` | full tier PASSED | verified |
| S3 | Own state round trips; a refused state changes nothing | SDK rule ST-6 | `plugin/src/lib.rs` | none | lib tests `own_state_round_trips`, `a_refused_state_changes_nothing`; conformance `state` | full tier PASSED | verified |
| S4 | State survives save and reopen in a host | request | same | none | REAPER suite, all four formats; OpenGrid9 VST3 and LV2 | `reaper-*.log`, `opengrid9-*.log` | verified; OpenGrid9 AU see E6 |
| S5 | Every parameter changes the sound | plugin-qa rule 2 | `plugin/src/engine.rs`, `plugin/cpp` | - | REAPER audio sweep, four formats; OpenGrid9 three formats | parameter table below | verified |
| S6 | Every parameter has a control in each skin | plugin-qa rule 2 | `plugin/src/editor/panel.rs` | analogue, digital and both system pages, channel A and B | `editor/tests.rs`: `every_parameter_has_a_control_in_each_skin`, `channel_b_tabs_address_channel_b`, `widgets_stay_inside_the_editor`; REAPER visual sweep | parameter table below | verified (LV2 after fix F3) |
| S7 | Autogain (BS.1770 weighting, 3 s, held in silence), off by default | release notes | `plugin/src/engine.rs` | toolbar switch | REAPER audio and visual sweep | parameter table | parameter verified; loudness match within 0.5 LU not measured |
| S8 | Delta plays only what the processing changes, off by default | release notes | `plugin/src/engine.rs` | toolbar switch | REAPER audio and visual sweep | parameter table | parameter verified; null at no processing not measured |
| S9 | Engine, oversampling, phase, tolerance or seed change is built in the background and faded in | release notes | `plugin/src/engine.rs` (builder, `install`) | status text | conformance `no_alloc`, soak, fuzz; `engine::tests::the_latency_table_predicts_every_built_state` | full tier PASSED | verified |
| S10 | Offline render starts with the circuit at rest; repeated bounces are equal | release notes | `plugin/src/lib.rs` (`reset`) | Offline Bounce light | `tests/reset.rs`: `reset_clears_the_tail`, `offline_passes_repeat_after_quick_resets`; suite "deterministic render" | all formats repeat at -240 dB (OpenGrid9 LV2 -132 dB) | verified |
| S11 | Reports a 2.5 s tail | release notes | `plugin/src/lib.rs` | none | `examples/tail.rs`; build flag `--tail` | not rerun on this build | unverified (not rerun) |
| S12 | Realtime safety: no allocation, lock, panic or I/O on the audio path | SDK rules RT-1..4 | audio path | none | T-LINT, conformance `no_alloc`, T-SOAK, T-FUZZ | full tier PASSED (6 of 6, twice) | verified |
| S13 | Host behaviour: process before prepare, any frame count, rate change without reset, non-finite input | SDK rules ST-2, ST-9 | `plugin/src/lib.rs` | none | `tests/host_behaviour.rs`, conformance | full tier PASSED | verified |
| S14 | Loads, finite, not silent, editor opens and reopens in REAPER: VST3, AU, CLAP, LV2 | request | - | editor | REAPER suite | `reaper-VST3-run2.log`, `reaper-AU-d93d926`, `reaper-CLAP-final.log`, `reaper-LV2-final.log` | verified |
| S15 | AU passes `auval` | release skill | SDK AU shell | none | `auval -v aufx TbQA OpGr` | `auval.log`; rerun after the last rebuild: SUCCEEDED | verified |
| S16 | Editor is fast enough: a frame well inside the 33 ms tick on every page at 2x | plugin-gui-performance | `plugin/src/editor/paint.rs` | all pages | `examples/snapshot.rs` prints ms per frame; `column_fill_covers_the_area_once` | table "Editor frame cost" | verified offscreen; on-screen GPU and host CPU not measured |
| S17 | LV2 editor shows meters and status in REAPER | found in QA | `plugin/src/bin/lv2_ttl.rs` | meters, status lights | REAPER visual sweep | `reaper-LV2-required-probe.log`, `reaper-LV2-final.log` | verified |
| S18 | AAX loads and validates in Pro Tools | request (formats) | SDK AAX shell | editor | AAX validator, Pro Tools | needs PACE signing and a system install by the owner | unverified (not run) |
| S19 | Windows and Linux builds | request | `plugin/scripts` | - | native build and host test on each system | needs the owner's machines | unverified (not run) |
| S20 | Text is readable (8 pt or larger), readouts follow the display rules | plugin-qa | `plugin/src/editor/paint.rs` | all pages | grep of text sizes and formats | one 7 pt faceplate legend (`paint.rs:237`); decimal readouts `GR x.x dB`, trims `0.0 dB` | open question |

## Checks

| Check | Result |
|---|---|
| OpenPlugin full tier (lint, clippy, tests, soak, fuzz, bench) | PASSED 6 of 6 (before the editor change; clippy and lib tests rerun after it: pass) |
| `auval` | SUCCEEDED |
| REAPER AU | 9 of 9 PASS |
| REAPER VST3 | 9 of 9 PASS (run 2, after the harness fix H1) |
| REAPER CLAP | 9 of 9 PASS (first build and final build) |
| REAPER LV2 | 8 of 9 before fix F3 (GUI check failed, 47 parameters); 9 of 9 PASS on the final build |
| OpenGrid9 VST3 | PASS, editor checks SKIPPED (headless) |
| OpenGrid9 LV2 | PASS, editor checks SKIPPED (headless) |
| OpenGrid9 AU | state check FAIL on switches and choices (E6); the rest PASS; editor checks SKIPPED |
| OpenGrid9 CLAP | SKIPPED: the bridge does not load CLAP |
| pluginval, clap-validator | SKIPPED: not installed |
| AAX validator, Pro Tools | SKIPPED: needs signing and a system install |
| On-screen GPU / CPU, native popups | SKIPPED: host windows stay hidden in unattended runs |

## Editor frame cost (offscreen, 2x, 30 frames with moving meters)

| Page | 1200 wide, before | 1200 wide, after | 1800 wide, before | 1800 wide, after |
|---|---|---|---|---|
| Analogue | 10.4 ms | 10.1 ms | 23.2 ms | 22.7 ms |
| Digital | 107.3 ms | 8.3 ms | 237.4 ms | 17.4 ms |
| Analogue system | 14.5 ms | 14.2 ms | 29.6 ms | 29.1 ms |
| Digital system | 12.4 ms | 12.3 ms | 25.6 ms | 25.1 ms |

Only the digital page's code changed. The "after" column was taken at load 5.

## Fixes made in this QA round

| ID | What | Where | Evidence |
|---|---|---|---|
| F1 | A reset with no spare circuit ready still clears the circuit | commit 510a344 | `tests/reset.rs` |
| F2 | Digital page was slow: the EQ and compressor graphs were single long polygons. Now pixel columns and short curve pieces | `plugin/src/editor/paint.rs` (`columns`, `area`, `glow`, `curve`) | 107 ms to 8.6 ms per frame; pictures compared at two sizes; test `column_fill_covers_the_area_once` |
| F3 | LV2 editor had dark meters in REAPER: REAPER passes instance-access only when the editor requires it; the SDK writes it as optional | `plugin/src/bin/lv2_ttl.rs` | probe with the edited description: GUI check PASS; final run |
| H1 | Harness, not plugin: REAPER set media offline when it lost focus, so renders were silent. The bridge now sets media online before each render | the QA harness's `reaper_bridge.lua` (outside this repository) | `reaper-vst3-probe3`: offline render is bit-identical to the silent renders |

## Parameters (host list vs ledger)

Host list dumped from: REAPER, four formats. Count: 87 plugin parameters, plus REAPER's own Bypass,
Wet and Delta rows, plus the LV2 Freewheel port. Each cell is audio / GUI. CONDITIONAL means the
parameter acts only in some state of the others (channel B needs Dual mono, Quality needs Eco off).

| Parameter | AU | VST3 | CLAP | LV2 |
|---|---|---|---|---|
| Instrument input | ok / ok | ok / ok | ok / ok | ok / ok |
| Gain | ok / ok | ok / ok | ok / ok | ok / ok |
| Gain +20 dB | ok / ok | ok / ok | ok / ok | ok / ok |
| Impedance | ok / ok | ok / ok | ok / ok | ok / ok |
| Phantom 48V | ok / ok | ok / ok | ok / ok | ok / ok |
| Invert | ok / ok | ok / ok | ok / ok | ok / ok |
| Low cut | ok / ok | ok / ok | ok / ok | ok / ok |
| Plate voltage high | ok / ok | ok / ok | ok / ok | ok / ok |
| Preamp output | ok / ok | ok / ok | ok / ok | ok / ok |
| Compressor on | ok / ok | ok / ok | ok / ok | ok / ok |
| Threshold | ok / ok | ok / ok | ok / ok | ok / ok |
| Ratio | ok / ok | ok / ok | ok / ok | ok / ok |
| Attack | ok / ok | ok / ok | ok / ok | ok / ok |
| Release | ok / ok | ok / ok | ok / ok | ok / ok |
| Detect auto | ok / ok | ok / ok | ok / ok | ok / ok |
| Comp output | ok / ok | ok / ok | ok / ok | ok / ok |
| External sidechain | ok / ok | ok / ok | ok / ok | ok / ok |
| Brickwall limiter | ok / ok | ok / ok | ok / ok | ok / ok |
| EQ on | ok / ok | ok / ok | ok / ok | ok / ok |
| Low | ok / ok | ok / ok | ok / ok | ok / ok |
| Low shift 40 Hz | ok / ok | ok / ok | ok / ok | ok / ok |
| High | ok / ok | ok / ok | ok / ok | ok / ok |
| High shift 6 kHz | ok / ok | ok / ok | ok / ok | ok / ok |
| Lo-Mid level | ok / ok | ok / ok | ok / ok | ok / ok |
| Lo-Mid freq | ok / ok | ok / ok | ok / ok | ok / ok |
| Lo-Mid x10 | ok / ok | ok / ok | ok / ok | ok / ok |
| Lo-Mid narrow | ok / ok | ok / ok | ok / ok | ok / ok |
| Hi-Mid level | ok / ok | ok / ok | ok / ok | ok / ok |
| Hi-Mid freq | ok / ok | ok / ok | ok / ok | ok / ok |
| Hi-Mid x10 | ok / ok | ok / ok | ok / ok | ok / ok |
| Hi-Mid narrow | ok / ok | ok / ok | ok / ok | ok / ok |
| Master | ok / ok | ok / ok | ok / ok | ok / ok |
| Output +4 dBu | ok / ok | ok / ok | ok / ok | ok / ok |
| Input trim | ok / ok | ok / ok | ok / ok | ok / ok |
| Output trim | ok / ok | ok / ok | ok / ok | ok / ok |
| 0 VU level | ok / ok | ok / ok | ok / ok | ok / ok |
| Mic source impedance | ok / ok | ok / ok | ok / ok | ok / ok |
| Instrument source impedance | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Quality | ok / ok | ok / ok | ok / ok | ok / ok |
| Linear phase (+L) | ok / ok | ok / ok | ok / ok | ok / ok |
| Stereo link | ok / ok | ok / ok | ok / ok | ok / ok |
| Dual mono | ok / ok | ok / ok | ok / ok | ok / ok |
| Mains frequency | CONDITIONAL / ok | ok / ok | ok / ok | ok / ok |
| Mains voltage | CONDITIONAL / ok | ok / ok | ok / ok | ok / ok |
| Component tolerances | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Unit seed | ok / ok | ok / ok | ok / ok | ok / ok |
| Circuit noise | ok / ok | ok / ok | ok / ok | ok / ok |
| Power supply model | ok / ok | ok / ok | ok / ok | ok / ok |
| Instrument input B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Gain B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Gain +20 dB B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Impedance B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Phantom 48V B | ok / ok | ok / ok | ok / ok | ok / ok |
| Invert B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Low cut B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Plate voltage high B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Preamp output B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Compressor on B | CONDITIONAL / excused | CONDITIONAL / excused | CONDITIONAL / excused | CONDITIONAL / excused |
| Threshold B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Ratio B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Attack B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Release B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Detect auto B | ok / ok | ok / ok | ok / ok | ok / ok |
| Comp output B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| External sidechain B | ok / ok | ok / ok | ok / ok | ok / ok |
| Brickwall limiter B | CONDITIONAL / excused | CONDITIONAL / excused | CONDITIONAL / excused | CONDITIONAL / excused |
| EQ on B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Low B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Low shift 40 Hz B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| High B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| High shift 6 kHz B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Lo-Mid level B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Lo-Mid freq B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Lo-Mid x10 B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Lo-Mid narrow B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Hi-Mid level B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Hi-Mid freq B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Hi-Mid x10 B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Hi-Mid narrow B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Master B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Output +4 dBu B | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Eco mode | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Oversampling | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok | CONDITIONAL / ok |
| Offline Bounce | ok / excused | ok / excused | ok / excused | ok / excused |
| Offline Bounce override | ok / ok | ok / ok | ok / ok | ok / ok |
| Autogain | ok / ok | ok / ok | ok / ok | ok / ok |
| Delta | ok / ok | ok / ok | ok / ok | ok / ok |
| Freewheel | - | - | - | excused / excused |
| Bypass | ok / excused | ok / excused | ok / excused | ok / excused |
| Wet | ok / ok | ok / ok | ok / ok | ok / ok |

## Exceptions (each needs a reason; full text in `qa/exceptions.txt`)

| Parameter / check | Exception | Reason | Decided by |
|---|---|---|---|
| E1 Offline Bounce | GUI sweep | set from the toolbar list; the closed button does not change with the choice | earlier QA |
| E2 Compressor on B, Brickwall limiter B | GUI sweep | their controls are on the channel B view; the sweep looks at channel A | earlier QA |
| E3 Bypass | GUI sweep | REAPER's own row, not a plugin parameter | earlier QA |
| E4 Quality, Detect auto B, External sidechain B, Phantom 48V B, Mains frequency, Mains voltage | context lines, not excused | tested in the state where they act (Eco off; Dual mono on; supply model on). Mains lines NEW | this round |
| E5 Freewheel (LV2) | audio, GUI and state | host-owned port that the host drives during offline renders; not saved. NEW | this round |
| E6 OpenGrid9 AU state check | reported as FAIL, not excused | the AU declares 0 to 1 ranges like 1.x, so the host cannot snap a switch written as 0.13; the plugin saves it as off. Continuous parameters all match, and the same check passes in REAPER | this round |

The comments in `qa/exceptions.txt` for E1 and E2 still name files of the 1.x editor.

## Open questions

- Windows and Linux: which machines, and how to reach them.
- AAX: PACE signing and the system install need the owner; Pro Tools test after that.
- Text: keep the 7 pt faceplate legend? Keep the decimal readouts (`GR 2.5 dB`, trims `0.0 dB`, level `-8.2 dBFS`)?
- Standalone app: the release notes say none. Confirm.
- Two agent-notes files under `plugin/` name a coding tool, in a public repository.
- `BINARY_LICENSE.txt` and the PDF manual script are missing.
- Should the SDK itself write instance-access as required (F3 is done in the plugin)?
- Bench baseline was recorded on a busy machine; record it again on a quiet one.

## Out of scope

| Item | Decided by | Where |
|---|---|---|
| Undo history, CPU readout, Program parameter, standalone app | owner, release notes | `docs/RELEASE_2.0.0.md` |
| A 1.2.5 JUCE release | owner, 2026-11-27 | this session |
