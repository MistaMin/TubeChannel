# Tube Channel on OpenPluginSDK

Tube Channel 2.0 on [OpenPluginSDK](https://github.com/MistaMin/OpenPluginSDK), with its editor drawn by GoodLookin DRI. This is the plug-in that is developed and released. The JUCE build in the repository root is what 1.2.x was made from; it is kept as the reference and is no longer developed.

| File | What it holds |
|---|---|
| `src/params.rs` | The 1.2.x parameter table: same ids, order, ranges, defaults and value text, each pinned to the id the JUCE build gave CLAP, VST3 and Audio Unit hosts. Hosts still see 0..1 values. Autogain and Delta are appended. |
| `cpp/bridge.cpp`, `src/engine.rs` | The signal path: `../Source` (Channel, Circuit, DSP, Hybrid, `Plugin/Chain.h`; no JUCE), unchanged, behind a C ABI. `engine.rs` also has the playback and offline bounce chains with their latency padding, and the builder thread that makes a new chain when Quality, Eco, Oversampling, Linear phase, tolerances, seed or the bounce choice change. |
| `src/lib.rs` | The plugin: buses, identity, state, Autogain (BS.1770 loudness of the input) and Delta (the result minus the latency-aligned input). |
| `src/state_import.rs` | Reads the state the JUCE release saved, so 1.x sessions and presets open, editor view included. |
| `src/programs.rs` | The six factory programs. |
| `src/editor/` | The analogue panel, the digital skin and each skin's System page, in the release's 1200 x 284 layout. `lettering.rs` and `place.rs` come from Blue Channel's editor. |
| `tests/` | The SDK conformance kit, the parameter table against scans of the 1.2.5 release (`port-scan/`), the release's captured states (`fixtures/`), reset. |
| `examples/` | `tail` (how long the output rings), `startup`, `snapshot` (a picture of the editor). |

## Building and testing

Cargo takes OpenPluginSDK from the git revision `Cargo.toml` pins, so a clean checkout builds and `Cargo.lock` matches it. `scripts/build.sh` finds an OpenPluginSDK checkout (or set `OPENPLUGIN_SDK`) for its bundle scripts; keep it at the same revision. Run Cargo from this folder.

```bash
cargo test
scripts/build.sh --formats clap,vst3,au --archs arm64 --out dist   # nothing is installed or signed beyond ad hoc
cargo run --release --example snapshot -- out.ppm digital 2
```

The bundles carry the release's ids: installing them replaces the JUCE build in every host. `scripts/build.sh --qa` builds "Tube Channel QA" with its own ids, which installs beside a release for testing.

## Checked against the 1.2.5 release

- `openplugin-port null`, VST3 to VST3, 48 kHz, 512-frame blocks: bit-identical output (difference 0) for sweep and noise at the defaults, and with switch and knob automation one block in (gain, threshold, EQ, ratio, attack, release, limiter, instrument input, dual mono with channel B, trims, alignment, supply model, mains).
- Not nulled: the Circuit and HQ engines and explicit oversampling. Neither build can switch engines inside the tool's render: the release rebuilds on its message thread, which the tool does not run, and the port's builder thread cannot keep up with a render far faster than real time. Both run the same `Chain` code.
- The release's CLAP crashes the null tool's renderer (SIGSEGV), so the comparison is through VST3.

## Differences from the JUCE release

- New: Autogain and Delta (off by default), with keys on both skins.
- A choice exactly half-way between two entries (a host writing normalized 0.5 to Mains, or 0.25 to Quality) rounds up; JUCE rounded to even.
- There is no undo history and no "Program" parameter. The factory programs are in the editor's list and in the SDK's preset interface. A/B and Copy are kept.
- The release's popup menus (programs, offline bounce) are lists drawn over the page.
- An explicit Quality change from the host no longer turns Eco off by itself. The editor's mode keys set both, as before.
- A 2.5 s tail is reported (the release reported none). `examples/tail.rs` measured 1.76 s at most.
- `reset` clears the circuit by swapping in a freshly built spare. When none is ready yet, an offline block builds a clean one itself and a realtime block takes the spare as soon as it is there. The release kept the circuit's state.
- The System page shows no CPU figure.
