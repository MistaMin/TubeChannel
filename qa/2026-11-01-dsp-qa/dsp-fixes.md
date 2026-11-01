# DSP engineering fixes — Tube Channel (2026-11-01)

Gates run: dsp_lint (0 findings), ctest 18/18, pluginval strict 10, clap-validator 44 tests,
auval. Findings fixed (committed 4d22fe1, pushed):

1. Parameter text round trips (clap-validator param-conversions): every float parameter's
   displayed text now parses back to the same value. The parse functions return the DENORMALISED
   value (getValueForText applies convertTo0to1). Inverse mappings added for the knob-derived
   displays (ratio, attack/release seconds, comp output dB, master dB, impedances, trims,
   mains voltage). File: Source/Plugin/Params.cpp, Source/Plugin/Params.h.
2. UTF-8 choice names: the Offline Bounce menu contains the middle dot. juce::String(const char*)
   reads UTF-8 bytes as 8-bit, so host-typed text arrives as "Eco Â· 4x" and cannot match.
   Choice names are now decoded with fromUTF8 and the choice accepts the mojibake form by
   normalising 8-bit characters back through UTF-8 before matching.
3. Bool parameters now report the effective 0/1 value to hosts (hosts may send fractional
   normalised values to switches), matching Blue Channel's SwitchParameter. Fixes pluginval
   "Plugin state restoration" (7 intermittent failures).
4. State restore host refresh: setStateInformation re-announces values before and after
   replaceState (so stale value-tree adapter caches cannot skip a restore) and calls
   updateHostDisplay(programChanged) so hosts re-read values after a load; the CLAP wrapper
   sends paramsRescan(VALUES) on programChanged. Fixes clap-validator state-reproducibility
   (basic/binary/buffered) and the VST3 host-cache staleness.

Verification after fixes: pluginval SUCCESS (0 failures), clap-validator 32 passed / 0 failed,
auval PASS, ctest 18/18. No sound change: the fixes touch text conversion and state hygiene only;
the audio engine is untouched (null-test not required: no DSP change).
