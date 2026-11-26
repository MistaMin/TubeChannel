#![allow(clippy::needless_range_loop)]
//! Writes the editor to a PPM: `snapshot out.ppm [analogue|digital|analogue-system|digital-system] [scale] [param=value ...]`.
//! The editor is bound to a plugin that has just processed a second of noise, so its meters and
//! analyser show something. `size=1.5` gives the window that many points per editor unit.
use openplugin::core::{Editor, EditorHost};
use openplugin::prelude::*;
use tube_channel::{TubeChannel, TubeEditor, PARAMS};

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let Some(out) = args.get(1) else {
        eprintln!("usage: snapshot <out.ppm> [analogue|digital|analogue-system|digital-system] [scale] [size=1] [param=value ...]");
        std::process::exit(2);
    };
    let page = args.get(2).map_or("analogue", String::as_str);
    let scale: f32 = args.get(3).and_then(|s| s.parse().ok()).unwrap_or(1.0);
    let size = args.iter().skip(4).find_map(|a| a.strip_prefix("size=")).map_or(1.0, |s| s.parse::<f32>().expect("a size"));
    let mut plugin = TubeChannel::default();
    let settings: Vec<(usize, f32)> = args
        .iter()
        .skip(4)
        .filter(|a| !a.starts_with("size="))
        .map(|s| {
            let (id, value) = s.split_once('=').expect("param=value");
            (PARAMS.iter().position(|p| p.id == id).expect("a parameter id"), value.parse().expect("a number"))
        })
        .collect();
    for (i, v) in &settings {
        plugin.set_param_index(*i, *v);
    }
    plugin.prepare(48_000.0, 512, 2);
    let mut seed = 1u32;
    for _ in 0..94 {
        let mut io = vec![vec![0.0f32; 512]; 2];
        for i in 0..512 {
            seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
            let s = ((seed >> 8) as f32 / 8388608.0 - 1.0) * 0.3;
            io[0][i] = s;
            io[1][i] = s * 0.8;
        }
        plugin.process(&mut ChannelsMut::new(&mut io), 512);
    }
    let mut editor = TubeEditor::new(EditorHost::detached());
    editor.bind(plugin.editor_state());
    for (i, v) in plugin.values().iter().enumerate() {
        editor.param_changed(i, *v);
    }
    editor.show(u8::from(page.starts_with("digital")), page.ends_with("system"));
    editor.resize((1200.0 * size).round() as u32, (284.0 * size).round() as u32);
    for _ in 0..30 {
        editor.tick(1.0 / 30.0);
    }
    let frame = editor.render(scale);
    let mut ppm = format!("P6\n{} {}\n255\n", frame.width, frame.height).into_bytes();
    for p in frame.pixels {
        ppm.extend_from_slice(&[(p >> 16) as u8, (p >> 8) as u8, *p as u8]);
    }
    std::fs::write(out, ppm).expect("write snapshot");
}
