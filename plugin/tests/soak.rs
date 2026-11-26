//! TS-2 and TS-3. Ignored in a plain `cargo test`; the commit tester runs them with `--ignored`.
//! OPENPLUGIN_SOAK_SEEDS sets the number of seeds (24 per commit, 200 for a release).

use tube_channel::TubeChannel;
use openplugin::testing::{Options, conformance};

openplugin::testing::install_allocator!();

// Quality, Eco, Oversampling, Linear phase and the offline bounce choose the oversampler, and with it
// the latency (tests/conformance.rs).

fn seeds() -> u64 {
    std::env::var("OPENPLUGIN_SOAK_SEEDS").ok().and_then(|s| s.parse().ok()).unwrap_or(24)
}

#[test]
#[ignore]
fn soak_many_seeds() {
    // The circuit model costs about two minutes a seed, so the seeds run on every core. The kit
    // counts allocations per thread, so no_alloc still holds for each run.
    use std::sync::atomic::{AtomicU64, Ordering::Relaxed};
    let next = AtomicU64::new(1);
    let workers = std::thread::available_parallelism().map_or(4, |n| n.get());
    let failures: Vec<String> = std::thread::scope(|s| {
        let handles: Vec<_> = (0..workers)
            .map(|_| {
                s.spawn(|| {
                    let mut failed = Vec::new();
                    loop {
                        let seed = next.fetch_add(1, Relaxed);
                        if seed > seeds() {
                            return failed;
                        }
                        let options = Options { fuzz_sessions: 12, latency_depends_on_params: true, ..Options::default() };
                        let report = conformance(TubeChannel::default, options.with_seed(seed));
                        if !report.passed() {
                            failed.push(format!("seed {seed}:\n{report}"));
                        }
                    }
                })
            })
            .collect();
        handles.into_iter().flat_map(|h| h.join().expect("a soak worker panicked")).collect()
    });
    assert!(failures.is_empty(), "{}", failures.join("\n"));
}

#[test]
#[ignore]
fn fuzz_strict() {
    let options =
        Options { fuzz_sessions: 32, strict_hostile_output: true, strict_denormals: true, latency_depends_on_params: true, ..Options::default() };
    conformance(TubeChannel::default, options).assert_pass();
}
