# Tube lookup experiment — development 1.1.4

This is an offline experiment, not the default engine. `TC_TUBE_EXPERIMENT` is only enabled on the comparison and profiling tools. The normal plugin build leaves the option disabled; `ChannelModel::prepare()` rejects a request for a compact tube there. The processor does not map a UI control or quality mode to this experiment. Default, HQ and Derp therefore continue to use the original modeled tubes.

## What the lookup changes

The table approximates two smooth calculations inside the existing triode equation: the softplus and its derivative, and the plate current power law and its derivative. It retains both triodes, the rest of the circuit, supply rails, nonlinear devices, feedback, capacitor histories and the original solver. It does not replace a tube with a static audio waveshaper. Table data is prepared before audio processing; values outside its valid range fall back to the original formula.

The isolated curve check passes for the three exponents used in the project. Relative maximum errors are under `5.2e-12` for current, `1.5e-9` for its derivative, `1.6e-10` for softplus and `3.1e-8` for its derivative. This very close curve fit did not guarantee the same path through the nonlinear solver at every setting.

## Channel comparison

The 48 kHz stereo Default comparison kept the detector backend, controls and processing settings matched. Three interleaved original/lookup timing runs averaged **10.891 ms** and **10.327 ms** of process CPU per 1024-sample block, a preliminary **5.18% reduction**. Other desktop activity remained present, so treat this as an estimate. One continuous-sine benchmark cannot establish crackle-free or multi-instance playback.

At moderate level, three harmonic fixtures were effectively identical. A 2-second exponential sweep also matched in three level/input fixtures while the supply was preconditioned equally and then held steady. The harmonic fixture with the strongest input, however, failed the comparison: output residual was **-26.1 dB RMS**, peak sample difference **0.0222**, and some harmonic levels differed by as much as **2.95 dB**. The native separated-order analysis is diagnostic only; it is not a Plugin Doctor export or a fitted Hammerstein model.

Eight longer channel fixtures include +20 dB gain, plate-supply switches, instrument input, slow compressor timing, sidechain, automation, noise, tolerances and multiple sample rates. Three loud or changing-control cases fail: residuals are around **-25 dB**, with peak sample errors up to **0.96**. All outputs stayed finite, but that is not an acceptable fidelity result. The isolated formulas differ very slightly, but the full nonlinear solver follows different recovery paths in these fixtures; switching back to the exact formula after a later overload sample cannot restore the earlier state. A separate sensitivity run also showed similar divergence after changing a tube-law parameter by roughly one part in a trillion. That points to solver-path sensitivity as a factor and makes this approximation unsafe to promote until the high-level tests match.

Therefore the lookup is **not promoted to Default**. The existing tube law remains active in every normal mode. See `validation-final.txt`, `sweep-final.txt`, `harmonic-final.txt`, `sweeps/sweep-analysis.json`, and `initial-benchmark.json` for raw comparisons and settings. The `sensitivity-final.txt` and guarded retry variants are failed diagnostics, not acceptance evidence.

## Exact state reduction

A separate experiment eliminated linear internal nodes while retaining the exact nonlinear equations. It removed 13 of 33 tube-stage unknowns and 15 of 36 compressor-stage unknowns, but ran slower on the measured single-channel fixture: 74.0 ms vs. 56.7 ms for the tube and 103.3 ms vs. 81.2 ms for the compressor. Waveform residuals were below -197 dB. This algebraic reduction preserves the solution but did not reduce work; use it only with a better reduced-solver layout.

## Reproduce

```sh
cmake --build build/hybrid --target TubeChannelTriodeLookupTest TubeChannelTubeCompare TubeChannelTubeLookupBench
ctest --test-dir build/hybrid -R '^triode_lookup$' --output-on-failure
build/hybrid/TubeChannelTubeCompare Netlists validation
TUBE_LOOKUP=1 build/hybrid/TubeChannelTubeLookupBench Netlists 2 1 1 350 0.126 1024 0
```

`TubeChannelTubeCompare Netlists validation` returns failure intentionally when any comparison exceeds its fidelity gate. The run records that result instead of disguising the failing fixtures as a test pass.
