//! What a freshly prepared chain outputs from silence, per 50 ms: `cargo run --release --example startup`.
use openplugin::prelude::*;
use tube_channel::TubeChannel;

fn main() {
    for set in [&[][..], &[("noise", 0.0)][..], &[("ecomode", 0.0)][..]] {
        let mut p = TubeChannel::default();
        for (id, v) in set {
            p.set_param(id, *v);
        }
        p.prepare(48_000.0, 512, 2);
        let mut line = String::new();
        let mut peak = 0f32;
        for block in 0..94 {
            let mut io = vec![vec![0.0f32; 512]; 2];
            p.process(&mut ChannelsMut::new(&mut io), 512);
            peak = io.iter().flatten().fold(peak, |m, x| m.max(x.abs()));
            if block % 5 == 4 {
                line += &format!(" {:.0}", 20.0 * peak.max(1e-9).log10());
                peak = 0.0;
            }
        }
        println!("{set:?}:{line}");
    }
}
