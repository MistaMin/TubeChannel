# Tube Channel QA — 2026-10-31: REAPER state round-trip + visual sweep failures

Scope: the three failed checks from the 2026-10-31 QA pass on `bundle-release` (uncommitted working
tree changes: prior session's eq-oversampling ctest only; this session changed no plugin source).

## Verdicts

1. **VST3 state round-trip** — not a plugin bug. REAPER 7.80 only propagates
   `TrackFX_SetParamNormalized` into the VST3 state while audio processes; the harness's 0.01 s
   flush render fixes the sync. Re-run with the fix: **PASS** (also hardened: the check now compares
   the plugin's own serialized state, see below).

2. **AU state round-trip (20 booleans "off -> on")** — REAPER 7.80 AU-bridge display quirk, not a
   plugin bug and not a one-off flake. Proven by a second route (live bridge experiments, decoded
   state chunks): REAPER displays any JUCE boolean below 1.0 as "off" while
   `AudioParameterBool::setValue` stores >= 0.5 as "on"; the harness's round-trip values
   `(0.13+0.37*i)%1` land in [0.5, 1.0) for exactly the 20 failed booleans, and the failed run's own
   saved chunk already held `1.0` for exactly those 20 — the plugin saved exactly what it was told.
   The same values display "On" on VST3. **Harness fix**: the state check now parses and compares the
   plugin's serialized state (AU ClassInfo `jucePluginState` / VST3 VC2!-framed XML) before save vs
   after reopen, with the old displayed-text comparison as fallback for non-JUCE chunks
   (`plugin-daw-test/scripts/reaper_qa.py`, `state_params_from_chunk()`). AU end-to-end: 0 lost.
   This also fixes the same class on HybridEQ (5) and ArcVu (10).

3. **`Offline Bounce` NOT SHOWN (AU visual sweep)** — false positive of the pixel method. The control
   exists and is bound ("OFFLINE BOUNCE ▾" button, componentID `offline-bounce`,
   PluginEditor.cpp:291-326; 9-choice popup menu, tooltip, documented in USER_MANUAL.md). The closed
   button's pixels do not depend on the choice in a realtime GUI, so a host-side change alters nothing
   (measured 0.000%; the sibling "Offline Bounce override" does change pixels, and the VST3 run of the
   same editor measures threshold-level 0.086%). Excused with a written reason in `qa/exceptions.txt`
   (same class as the Water Channel band-Q controls).

## Additional finding from the re-run (audio sweep)

Three parameters reported DEAD on the VST3 re-run; all are "silent by design in the sweep's state",
verified live with renders:

- **Quality** is overridden by Eco mode (default on) and, in offline renders like the harness's, by
  the Offline Bounce override (default on) — USER_MANUAL.md. With both off: 5.4 dB.
- **Detect auto B / External sidechain B / Phantom 48V B** act only in Dual mono (Chain.h: `cb =
  ctrlA` while the default Stereo link is on). In dual mono: -11.4 dB / 1.0 dB / -83.3 dB.
- Fix: context lines in `qa/exceptions.txt` (tested in the state where each parameter is audible —
  the plugin-qa "context line" pattern, preferred over exceptions).

## Checks

| Check | VST3 (re-run) | AU (re-run) |
|---|---|---|
| loads | PASS | PASS |
| finite output / not silent | PASS | PASS |
| deterministic render | PASS | PASS |
| editor opens | PASS | PASS |
| every parameter changes the sound | PASS | PASS |
| every parameter is shown in the GUI | PASS | PASS (Offline Bounce excused) |
| state survives save and reopen | PASS | PASS |
| editor reopens | PASS | PASS |

ctest: 18/18 PASS (incl. the prior session's eq_oversampling test).

## Exceptions (qa/exceptions.txt)

- `visual:Offline Bounce` — menu-only display, no pixel reaction in a realtime GUI (reason above).
- `Quality @ Eco mode=0.0, Offline Bounce override=0.0` — engine choice only reaches the DSP with
  both overrides off (measured 5.4 dB).
- `Detect auto B @ Dual mono=1.0`, `External sidechain B @ Dual mono=1.0`,
  `Phantom 48V B @ Dual mono=1.0` — B-channel controls only act in dual mono (measured -11.4 / 1.0 /
  -83.3 dB).

## Fixes made

- `plugin-daw-test/scripts/reaper_qa.py`: chunk-based state round-trip comparison
  (`state_params_from_chunk()` + fallback) — replaces the host-display-text comparison that REAPER
  7.80's AU bridge defeats for booleans.
- `qa/exceptions.txt`: created (context lines + one visual excusal, each with a written reason).
- No plugin source changes were needed; the working tree diff is unchanged from the prior session
  (CMakeLists.txt +1 line, tests/processor_stress.cpp +3 lines, tests/eq_oversampling_test.h).

## Not done / needs the user

- A final VST3 suite re-run WITH the Phantom 48V B context line was skipped for the time budget: the
  VST3 suite already exited 0 without it (phantom passed via the random retry) and the line was
  verified live on VST3 (-82.8 dB). Cheap to re-confirm.
- `Oversampling` has the same offline-override relationship as Quality but passes (CONDITIONAL);
  a context line could make it deterministic the same way.
- The closed "OFFLINE BOUNCE ▾" button does not show the selected combination (menu/tooltip only);
  a label showing the current choice would make the visual sweep able to see it. Product decision.
- One sibling run's REAPER cleanup (`pkill` in the shared au_run.sh helpers) can kill a concurrent
  test instance — the helpers should kill only the PID in `<qa-dir>/reaper.pid`.
