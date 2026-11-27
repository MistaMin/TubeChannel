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

fn pass(p: &mut TubeChannel) -> Vec<f32> {
    let mut out = Vec::new();
    for b in 0..40 {
        let mut io = vec![vec![0.0f32; 512]; 2];
        for i in 0..512 {
            let s = 0.4 * ((b * 512 + i) as f32 * 0.05).sin();
            io[0][i] = s;
            io[1][i] = s;
        }
        p.process(&mut ChannelsMut::new(&mut io), 512);
        out.extend_from_slice(&io[0]);
    }
    out
}

/// Resets that come faster than the builder makes spares still clear the circuit: an offline pass
/// builds its own state, so every pass is the first pass.
#[test]
fn offline_passes_repeat_after_quick_resets() {
    let mut p = TubeChannel::default();
    p.prepare(48_000.0, 512, 2);
    p.set_param("offlinebounceenabled", 0.0); // the playback chain renders: no fresh bounce chain per pass
    p.set_offline(true);
    let first = pass(&mut p);
    for n in 2..=4 {
        p.reset(); // takes the spare
        let mut io = vec![vec![0.5f32; 512]; 2];
        p.process(&mut ChannelsMut::new(&mut io), 512);
        p.reset(); // right behind it: no spare can be ready yet
        let again = pass(&mut p);
        let diff = first.iter().zip(&again).map(|(a, b)| (a - b).abs()).fold(0f32, f32::max);
        assert!(diff < 1e-6, "pass {n} differs from the first by {:.1} dB", 20.0 * diff.max(1e-12).log10());
    }
}
