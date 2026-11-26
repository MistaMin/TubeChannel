//! PF-5: `cargo run --release --example bench -- --out bench/baseline.json` stores the baseline;
//! `-- --baseline bench/baseline.json` fails when a case got slower (the commit tester runs it).

fn main() -> std::process::ExitCode {
    openplugin::testing::bench::cli(tube_channel::TubeChannel::default, std::env::args().skip(1))
}
