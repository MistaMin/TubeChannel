#![allow(clippy::needless_range_loop)]
//! A reset in the middle of a tail clears it: the builder's spare, a freshly built circuit, is
//! swapped in (the chain has no reset of its own).

use openplugin::prelude::*;
use tube_channel::TubeChannel;

fn peak_after_reset(p: &mut TubeChannel) -> f32 {
    p.prepare(48_000.0, 512, 2);
    p.reset(); // hosts reset right after activating; the spare must still be there for the next one
    let mut seed = 1u32;
    for _ in 0..40 {
        let mut io = vec![vec![0.0f32; 512]; 2];
        for i in 0..512 {
            seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
            let s = ((seed >> 8) as f32 / 8388608.0 - 1.0) * 0.5;
            io[0][i] = s;
            io[1][i] = s;
        }
        p.process(&mut ChannelsMut::new(&mut io), 512);
    }
    p.reset();
    let mut peak = 0f32;
    for _ in 0..20 {
        let mut io = vec![vec![0.0f32; 512]; 2];
        p.process(&mut ChannelsMut::new(&mut io), 512);
        peak = io.iter().flatten().fold(peak, |m, x| m.max(x.abs()));
    }
    20.0 * peak.max(1e-9).log10()
}

#[test]
fn reset_clears_the_tail() {
    let db = peak_after_reset(&mut TubeChannel::default());
    assert!(db < -90.0, "{db:.1} dBFS after reset");
}
