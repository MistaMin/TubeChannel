#![allow(clippy::needless_range_loop)]
//! How long the output stays above -90 dBFS after one second of loud noise stops, for settings that
//! ring longest (the source of `TAIL_SECONDS` in src/lib.rs): `cargo run --release --example tail`.
use openplugin::prelude::*;
use tube_channel::TubeChannel;

fn main() {
    let sets: &[&[(&str, f32)]] = &[
        &[],
        &[("noise", 0.0)],
        &[("comp", 0.0), ("noise", 0.0)],
        &[("release", 1.0), ("threshold", 0.0), ("noise", 0.0)],
        &[("gain", 1.0), ("gain20", 1.0), ("noise", 0.0)],
        &[("instrument", 1.0), ("gain", 1.0), ("noise", 0.0)],
        &[("low", 1.0), ("loshift", 1.0), ("lmlevel", 1.0), ("lmfreq", 0.0), ("lmnarrow", 1.0), ("noise", 0.0)],
        &[("ecomode", 0.0), ("noise", 0.0)],
        &[("ecomode", 0.0), ("quality", 1.0), ("noise", 0.0)],
        &[("psumodel", 1.0), ("noise", 0.0)],
    ];
    for rate in [44_100.0f32, 96_000.0] {
        for set in sets {
            let mut p = TubeChannel::default();
            for (id, v) in *set {
                assert!(p.set_param(id, *v));
            }
            p.prepare(rate, 512, 2);
            let mut seed = 1u32;
            let (mut last, mut n, mut peak, mut floor) = (0usize, 0usize, 0f32, 0f32);
            let total = (rate * 12.0) as usize;
            let stop = rate as usize;
            while n < total {
                let mut io = vec![vec![0.0f32; 512]; 2];
                for i in 0..512 {
                    if n + i < stop {
                        seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
                        let s = ((seed >> 8) as f32 / 8388608.0 - 1.0) * 0.5;
                        io[0][i] = s;
                        io[1][i] = s;
                    }
                }
                p.process(&mut ChannelsMut::new(&mut io), 512);
                for i in 0..512 {
                    let a = io[0][i].abs().max(io[1][i].abs());
                    if n + i < stop {
                        peak = peak.max(a);
                    } else {
                        if a > 3.16e-5 {
                            last = n + i - stop;
                        }
                        if n + i > total - rate as usize {
                            floor = floor.max(a);
                        }
                    }
                }
                n += 512;
            }
            println!("{rate} {set:?}: peak {:.3}, above -90 dBFS until {:.3} s, last second {:.1} dBFS", peak, last as f32 / rate, 20.0 * floor.max(1e-9).log10());
        }
    }
}
