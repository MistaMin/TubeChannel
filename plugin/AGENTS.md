<!-- From the openplugin-dev template; generated from the wiki by scripts/gen_skills.py. -->
# tube-channel: notes for coding agents

An audio plugin on OpenPluginSDK. The rules below are the short form of the OpenPluginSDK plugin
author wiki; the Claude Code plugin `openplugin-dev` enforces them with hooks and has a skill for
each task (`new-plugin`, `write-dsp`, `test-plugin`, `prepare-release`). Load the skill before
the task.

## Rules

- **RT-1** Audio-path calls never allocate or free memory.
- **RT-2** Audio-path calls never take a lock, wait on a channel or block.
- **RT-3** Nothing on the audio path can panic.
- **RT-4** Audio-path calls never print, log, touch files or sockets, or sleep.
- **RT-5** The cost of a block does not depend on the data in a way you cannot bound.
- **RT-6** All time constants, coefficients and buffer sizes are computed from the sample rate given to the last `prepare`.
- **RT-7** The editor, state code and workers reach the audio thread only through the parameter plane, a `Seqlock`, the SPSC queue or `worker::Loader` / `Slot`.
- **ST-2** After NaN or infinity at the input, output is finite again within one second plus latency plus tail.
- **ST-4** `set_param` clamps every value, rejects NaN, returns `true` for a real id and `false` for an unknown one, and `param` always returns a finite value inside `min..max`.
- **ST-6** `load_state` returns `false` on anything it does not understand and never panics, and state round-trips bit for bit.
- **ST-9** `process` works before `prepare`, with any `frames` up to the buffer length, and after a `prepare` at a new rate with no `reset`.
- **PR-2** The plugin id, the format ids (CLAP id, VST3 class ids, AU type, subtype and manufacturer, AAX ids, LV2 URI) and the vendor never change after the first public build.
- **PR-3** After a release, parameter ids are never renamed or reused, parameters are never reordered or removed, and ranges are only widened.
- **PR-4** Work happens on a branch or in a worktree, never directly on `main`, and every commit passes the quick tier first.

## Before every commit

These checks must pass. With `openplugin-dev` installed, the Stop hook runs them for you and the
commit tester runs the full list on every commit in the background; without it, run them yourself:

```bash
cargo test --test conformance                 # quick conformance (debug, Options::quick)
cargo clippy --all-targets -- -D warnings
cargo test --release                          # full conformance and host behaviour
```

A failing check is fixed before new work starts. Never weaken a test or an option of the
conformance kit to make it pass; fix the plugin.
