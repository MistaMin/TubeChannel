# Tube Channel 1.2.3 — Windows QA + release build, 2026-11-02

Commit: 78d4df1c9a980760e5a1b6fd8648420507f934da (tag v1.2.3, detached HEAD; tree clean except untracked qa/)
Machine: Windows x64, VS 2026 (MSVC 14.51), Ninja, Release. REAPER 7.80 x64.

## Build

`build_win.bat configure && build_win.bat build` — clean, 293 targets (build.log).
Formats in `build/TubeChannel_artefacts/Release/`: VST3, CLAP, LV2, Standalone, AAX (unsigned).
Warnings only (C4458/C4457/C4459 hidden names, C4996 getenv) — pre-existing, none new.

## ctest (ctest.log)

17/17 PASS, 241 s total: oversampler, sparse_lu, circuit, sidechain, limiter, lockstep,
realtime, detector_guard, quality_modes, linear_layout, triode_lookup, hybrid_modules,
hybrid_linear, eco, processor_stress, offline_bounce, gui.

## Host suite (report.md / report.json, suite.log)

Plugin under test VERIFIED as the new build: after a REAPER cache purge, the loaded module was
`C:\Users\marco\AppData\Local\Programs\Common\VST3\Tube Channel.vst3` (moduleinfo Version 1.2.3),
not the system-path 1.2.0/1.2.1 bundle (checked via process module list of reaper.exe).

Suite verdict: FAIL on "every parameter changes the sound" — Quality, Detect auto B, External
sidechain B. Everything else PASS: loads (90 params; 1.2.1 had 88 — the two new ones are
"Offline Bounce" and "Offline Bounce override", both ok), finite, not silent, deterministic,
editor opens, all params shown in GUI, state round trip, editor lifecycle x5.

vs 1.2.1 baseline:
- Detect auto B: DEAD in all 5 random states — unchanged from 1.2.1 (pre-existing finding,
  still not fixed in 1.2.3).
- External sidechain B: DEAD in all 5 random states — unchanged from 1.2.1 (also not fixed).
  Note: the suite renders a stereo file with no sidechain input, so "External sidechain" params
  have nothing to switch to; DEAD may partly be a coverage limitation. Channel A's "External
  sidechain" DOES act (-12.3 dB) under the same conditions, so B behaving differently is still
  suspicious.
- Quality: DEAD in all states (1.2.1: CONDITIONAL, acted once at 5.1 dB). Investigated, see below.
- Eco mode / Oversampling: CONDITIONAL on 1.2.3 (acted in random state 1) vs ok-at-defaults on
  1.2.1; Component tolerances, Phantom 48V, Mains frequency/voltage, Power supply model flipped
  between ok and CONDITIONAL. The suite's random states are seeded per-parameter-index, and 1.2.3
  added 2 parameters, so the state vectors differ from the 1.2.1 run — these flips are
  expected RNG noise, not regressions.

## Quality investigation (renders: v_*, x_*, y_*, w_*, z_* series)

Hypothesis from the suite: "Quality" (Circuit / HQ / HQ legacy Derp) never changes the sound.

Method errors found and ruled out along the way:
- qtest_*/qq_* series: invalid — after remove_fx + add_fx the plugin sat on a new track with no
  audio item; renders were the dry signal. (Also: an earlier instance once rendered silence after
  the suite + rapid param restore; NOT reproduced on fresh instances — unconfirmed anomaly.)
- y_ series threshold probe at gain max: invalid — compressor detector railed, masking everything.

Decisive runs (fresh instance, plugin + testsignal on the same track, method validated with Gain):
- x_ series: Threshold acts before AND after an Eco toggle (-42.1 dB). Eco on/off itself:
  bit-identical at defaults. The suite's Eco/Oversampling "acts" (-3.1 / -2.2 dB, one state each)
  are consistent with mid-render engine-swap fades (processBlock applies a fade on adoption),
  i.e. likely measurement artifacts, not real differences.
- z_ series (eco off, gain 0.35, threshold min, ratio max): compression demonstrably active —
  comp on vs off differs by -18.1 dB in every quality mode. Circuit vs HQ vs Derp:
  BIT-IDENTICAL (-240 dB), with compression on and off.
- w_ series with 3 s settle after each quality change (rules out the async rebuild race):
  still bit-identical.

Cross-check: `quality_modes` ctest PASSES on this build and asserts (tests/quality_mode_test.cpp:64)
that Default(Circuit) and HQ produce DIFFERENT compressed audio. That test drives tc::Chain
directly; the REAPER path goes through the plugin wrapper. Conclusion: in the VST3 wrapper the
Quality switch does not reach the audio engine (or reaches it without effect), while the engine
itself is correct. `Key::operator==` (PluginProcessor.h:90) does include quality, and the
listener wiring exists (PluginProcessor.cpp:33, PluginProcessor.h:81), so the break is elsewhere
(requestedKey/buildChain/adoption path). NOT fixed by this run — needs a developer.

This matches the 1.2.1 CONDITIONAL sighting being a fade artifact: likely Quality has been inert
in the host for both versions. Whether macOS 1.2.3 behaves the same is untested here.

## Packaging

`qa/2026-11-02-win-release/pkg-tools/` holds make_bundle_zip.ps1 + write_opengrid_manifest.py
taken from main (f8d8680) — the tag 78d4df1 does not contain them (added on main in 3255123,
after the tag). The tag's source is untouched.

- C:\Users\marco\Developer\Release Binaries\MistaMin\TubeChannel\1.2.3\Tube-Channel-1.2.3-windows-x64-bundles.zip
  VST3 + CLAP + LV2 + OpenGrid\Tube Channel resources (manual PDF, licenses). No Standalone
  (user decision), no AAX (unsigned), no Thumbs.db/.DS_Store/._*.
- C:\Users\marco\Developer\Release Binaries\MistaMin\TubeChannel\1.2.3\opengrid-manifest.json
  schema 2, one archive: windows x64, min_os 10.0, build_class modern (agrees), sha256+size
  verified against the zip. The macOS archive is NOT in this manifest (built elsewhere; merge
  with `--archive` for both zips where both exist).
- No RELEASE-READY marker (user approval pending).
- Unsigned AAX set aside: qa/2026-11-02-win-release/aax-unsigned/Tube Channel.aaxplugin
  (original binary from the tested build). A complete copy (relinked binary, identical source)
  was restored to build/TubeChannel_artefacts/Release/AAX/ so the build tree is consistent
  (`ninja: no work to do`).

## Machine state notes

- Repo is left on detached HEAD at v1.2.3 (78d4df1). `git checkout main` to go back.
- %LOCALAPPDATA%\Programs\Common\VST3\Tube Channel.vst3 (1.2.3) remains installed; the system
  bundle (C:\Program Files\Common Files\VST3, internal version 1.2.0) was not touched (read-only).
  REAPER's cache (reaper-vstplugins64.ini) keys plugins by bare filename, so only one of the two
  is visible at a time; the stale 1.2.0 cache entry was removed (backup:
  qa/2026-11-02-win-release/reaper-vstplugins64.ini.bak) and REAPER now picks the user-path 1.2.3.
- A stale REAPER (pid 8516, bridge unresponsive) was closed before the run; no REAPER is left
  running.
- numpy caveat: `python` resolves differently between shells; the daimon-bundle interpreter has
  no numpy, the daimon-share .venv one does. The suite must run with the latter (first attempt
  died on `import numpy` before touching REAPER).
