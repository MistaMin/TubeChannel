# Offline Bounce — 1.2.1 implementation and verification

The toolbar dropdown between Redo and System contains exactly nine combinations:
Eco, Circuit and HQ, each at 2x, 4x and Derp/8x. It is visible in both skins/pages.
Dual Mono channel tabs fit beside the preset selector without overlapping it.
New instances use HQ/8x for host-reported offline processing. Realtime exports,
playback and recording retain the playback engine. No transport heuristic or manual
force-offline option is used.

Existing parameter indices and automation mappings are unchanged. The new choice
and compatibility flag are appended with AU version hint 3. Saved pre-1.2.1 states
restore with the override off; choosing a dropdown option enables it. Factory
channel presets leave the global rendering selection alone; project state retains it.

The engine pair is prepared outside the realtime callback. Only one engine processes
each block. A bounce-only edit preserves the running playback model and delay history.
An offline host that changes the render flag/parameters without prepare or a message
loop is handled before its first block; fallback setup/destruction is restricted to
offline processing. Returning to realtime never constructs or destroys an engine.
Offline entry bypasses the live mode-change fade. Repeated offline transitions and
host reset calls reinitialize the bounce engine deterministically.

Linear phase reserves a fixed budget across the available oversampling factors and
pads each path to it. Tested values are 58 samples at 48 kHz and 23 at 96 kHz;
minimum phase is zero. The budget is reported before normal rendering and does not
change when the host switches between playback and the configured bounce profile.

## Automated results

- Full native suite: 17/17 passed.
- Universal arm64 processor, offline-bounce and GUI suites: 3/3 passed.
- x86_64 under Rosetta: offline, processor and GUI tests passed with zero failures.
- All 36 rate/phase/profile combinations (48/96 kHz, minimum/linear, nine choices)
  match independently configured manual processing sample-for-sample, accounting
  only for the declared padding. Tests include the first sample, 1/17/64/129-frame
  blocks, larger-than-prepared callbacks and zero-input continuation.
- Realtime exports and playback resumption match uninterrupted reference playback
  history exactly. Allocation/free counters cover the real processor callback,
  prepared-state adoption and return from bounce: zero callback allocations/frees.
- Stereo/mono, mono/stereo external sidechains, Dual Mono, early/late offline flags,
  saved-state migration/round trips, repeated bounces and host reset are covered.

Screenshots of the editor are rendered from the real components. The menu diagrams
are the approved illustrations, labeled as such in the guide. The runner has no
connected display; native popup interaction in a DAW remains a manual validation
item. Headless clicks are guarded safely instead of dereferencing a missing display.
Host wrappers must report offline mode correctly; no claim is made about every DAW.

## Release package

Universal VST3, AU, CLAP, LV2 and Standalone binaries were built for arm64/x86_64,
version 1.2.1. Developer ID signatures and the installer signature are verified.
The package includes the guide, four refreshed editor images, two menu illustrations,
release notes, source/dependency manifest and applicable license files. No AAX is
included. Runtime source SHA256 values match the packaged build manifest.

Apple accepted submission `fc0cc03a-f405-4326-8269-26f0f21436f5`. The notarization
ticket is stapled and validated; Gatekeeper accepts the installer as Notarized
Developer ID. See `notarization.txt` and `installer.sha256` for the final package.
The earlier `installer-local-signed.sha256` records the package before stapling.
See `package-check.txt` for payload/signature checks and the recorded test logs
for the validation scope.
