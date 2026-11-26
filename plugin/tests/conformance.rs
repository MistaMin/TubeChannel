//! TS-1: the conformance kit on this plugin: every sample rate and block size, hostile input,
//! parameter fuzz, state round trips, tail and layouts. Debug builds run `Options::quick()` (the Stop
//! hook); release builds run every rate and block size (the commit tester).
//!
//! Its allocation check counts Rust's allocator only: an allocation inside the C++ chain would not
//! be seen (SDK docs/CPP_BRIDGE.md says how to count those).

use openplugin_testing::{conformance, Check, Options, Status};
use tube_channel::TubeChannel;

openplugin_testing::install_allocator!();

#[test]
fn conforms() {
    let mut options = if cfg!(debug_assertions) { Options::quick() } else { Options::default() };
    // Quality, Eco, Oversampling, Linear phase and the offline bounce choose the oversampler, and
    // with it the latency (reported with the parameter change, src/lib.rs `request`).
    options.latency_depends_on_params = true;
    let report = conformance(TubeChannel::default, options);
    report.assert_pass();
    assert_eq!(report.status(Check::NoAlloc), Status::Pass, "{report}");
}
