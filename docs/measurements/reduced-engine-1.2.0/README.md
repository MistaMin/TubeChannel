# 1.2.0 reference and validation checkpoint

See [the active plan](../../SOUND_MODEL_PLAN.md). The minimum target is 10x and the
stretch target is 20x against the pinned v1.1.4 source in `reference.json`.

The saved reference tools were compiled before the 1.2.0 version and measurement
changes. `reference-48k-256.txt` records a fresh reference run. `harness-48k-256.txt`
and `harness-96k-64.txt` exercise the extended benchmark, using the unchanged
v1.1.4 DSP source. They do not compare a reduced engine or establish any speedup.
Noise and PSU are explicitly selected in the new harness; the old reference uses
its historical noise-on setting. Runs are sequential, after builds/tests finish.
Desktop activity is uncontrolled; single runs are measurement-tool checks only.

`native-tests.txt` records engine regression checks. Host/plugin packaging and
multi-instance listening have not been tested for a redesigned engine at this
checkpoint. No installed plugin or release package is replaced.
