//! The parameter table against the release: `openplugin-port scan` of the 1.2.5 CLAP, VST3 and Audio
//! Unit (plugin/port-scan). Every released parameter keeps its position, its id in each format, its
//! default and its value text.

use openplugin::core::HostSpace;
use openplugin::prelude::Unit;
use serde_json::Value;
use tube_channel::PARAMS;

fn scan(format: &str) -> Vec<Value> {
    let path = format!("{}/port-scan/tube-channel.{format}.manifest.json", env!("CARGO_MANIFEST_DIR"));
    let text = std::fs::read_to_string(&path).unwrap_or_else(|e| panic!("{path}: {e}"));
    let v: Value = serde_json::from_str(&text).unwrap();
    v["plugin"]["params"].as_array().unwrap().clone()
}

#[test]
fn clap_rows_are_the_release_rows_in_order() {
    let clap = scan("clap");
    assert_eq!(clap.len(), 85);
    for (i, p) in clap.iter().enumerate() {
        let ours = &PARAMS[i];
        assert_eq!(ours.name, p["name"].as_str().unwrap(), "row {i}");
        assert_eq!(ours.pins.clap, Some(p["id"].as_u64().unwrap() as u32), "{}", ours.id);
        let (lo, hi, def) = ours.host_range(HostSpace::CLAP);
        assert_eq!((lo, hi), (0.0, 1.0), "{}: hosts saw 0..1", ours.id);
        assert!((def - p["default_normalized"].as_f64().unwrap() as f32).abs() < 1e-6, "{} default", ours.id);
        for s in p["samples"].as_array().unwrap() {
            let normalized = s["normalized"].as_f64().unwrap() as f32;
            let position = normalized * (ours.max - ours.min);
            if ours.unit == Unit::Choice && position.fract() == 0.5 {
                // Exactly between two choices JUCE rounds to even, the SDK away from zero (README, Differences).
                continue;
            }
            let plain = ours.from_host(HostSpace::CLAP, normalized);
            assert_eq!(ours.format(plain), s["text"].as_str().unwrap(), "{} at {}", ours.id, s["normalized"]);
        }
    }
}

#[test]
fn vst3_and_au_ids_are_the_release_ids() {
    for format in ["vst3", "au"] {
        for p in scan(format) {
            let name = p["name"].as_str().unwrap();
            if format == "vst3" && (name == "Bypass" || name == "Program") {
                continue; // the JUCE wrapper's own; the SDK has its own bypass
            }
            let ours = PARAMS.iter().find(|q| q.name == name).unwrap_or_else(|| panic!("{format}: {name} is gone"));
            let pin = if format == "vst3" { ours.pins.vst3 } else { ours.pins.au };
            assert_eq!(pin, Some(p["id"].as_u64().unwrap() as u32), "{format} {name}");
        }
    }
}
