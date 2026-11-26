//! ST-9: host behaviour the conformance kit does not drive.

use tube_channel::TubeChannel;
use openplugin::prelude::*;

fn stereo(frames: usize, value: f32) -> Vec<Vec<f32>> {
    vec![vec![value; frames]; 2]
}

fn run(plugin: &mut TubeChannel, buffers: &mut [Vec<f32>], frames: usize) {
    let mut io: Vec<&mut [f32]> = buffers.iter_mut().map(|c| c.as_mut_slice()).collect();
    plugin.process(&mut io, frames);
}

fn finite(buffers: &[Vec<f32>]) -> bool {
    buffers.iter().flatten().all(|s| s.is_finite())
}

#[test]
fn process_before_prepare_is_defined() {
    let mut plugin = TubeChannel::default();
    let mut buffers = stereo(64, 0.5);
    run(&mut plugin, &mut buffers, 64);
    assert!(finite(&buffers));
}

#[test]
fn a_rate_change_without_a_reset_keeps_working() {
    let mut plugin = TubeChannel::default();
    plugin.prepare(44100.0, 512, 2);
    run(&mut plugin, &mut stereo(512, 0.25), 512);
    plugin.prepare(96000.0, 512, 2); // a device switch: no reset in between
    for _ in 0..200 {
        let mut buffers = stereo(512, 0.25);
        run(&mut plugin, &mut buffers, 512);
        assert!(finite(&buffers));
    }
}

#[test]
fn every_frame_count_up_to_the_buffer_length_works() {
    let mut plugin = TubeChannel::default();
    plugin.prepare(48000.0, 512, 2);
    for frames in [1usize, 2, 3, 7, 64, 333, 511, 512] {
        let mut buffers = stereo(512, 0.1);
        run(&mut plugin, &mut buffers, frames);
        assert!(finite(&buffers), "frames = {frames}");
    }
}
