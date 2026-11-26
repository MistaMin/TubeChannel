//! States the 1.2.5 release saved (`openplugin-port capture`, tests/fixtures) open in the port with
//! the values the release itself read back: the default state, one with edited controls on both
//! channels and the setup, and every factory program, through CLAP, VST3 and Audio Unit containers.

use openplugin::core::foreign_state::ForeignStateKind;
use openplugin::core::HostSpace;
use openplugin::prelude::*;
use serde_json::Value;
use tube_channel::{idx, TubeChannel, PARAMS};

fn fixtures(format: &str) -> Value {
    let path = format!("{}/tests/fixtures/tube-channel.{format}.capture.json", env!("CARGO_MANIFEST_DIR"));
    serde_json::from_str(&std::fs::read_to_string(&path).unwrap_or_else(|e| panic!("{path}: {e}"))).unwrap()
}

/// The values the release read after reopening the "set" blob (`capture --load-component`). The
/// capture's own readback of that state was taken from the live instance before it was saved, where
/// Eco still read 1; the saved state and a reopened session have Eco 0 (an explicit Quality change
/// clears Eco).
fn reopened_set() -> Value {
    fixtures("vst3.reload-set")["states"].as_array().unwrap().iter().find(|s| s["name"] == "loaded").unwrap()["params"].clone()
}

fn check(format: &str, load: impl Fn(&mut TubeChannel, &[u8]) -> bool) -> usize {
    let capture = fixtures(format);
    let mut count = 0;
    for state in capture["states"].as_array().unwrap() {
        let name = state["name"].as_str().unwrap();
        let file = state["blobs"][0]["file"].as_str().unwrap();
        let blob = std::fs::read(format!("{}/tests/fixtures/{file}", env!("CARGO_MANIFEST_DIR"))).unwrap();
        let mut p = TubeChannel::default();
        assert!(load(&mut p, &blob), "{format} {name}: refused");
        // CLAP and VST3 set the same normalized values, so the VST3 reopen is their expectation. The
        // Audio Unit set its choices by index (another state): its own readback, without the Eco row.
        let expected = if name == "set" && format != "au" { reopened_set() } else { state["params"].clone() };
        for v in expected.as_array().unwrap() {
            let pname = v["name"].as_str().unwrap();
            let Some(i) = PARAMS.iter().position(|q| q.name == pname) else { continue };
            if format == "au" && name == "set" && pname == "Eco mode" {
                continue;
            }
            // The JUCE build showed every format 0..1, so its plain value is the normalized one.
            let want = v["normalized"].as_f64().or(v["plain"].as_f64()).unwrap() as f32;
            // The JUCE Audio Unit showed choices as indexed parameters: their plain value is the index.
            let got = if format == "au" && PARAMS[i].unit == Unit::Choice { p.values()[i] } else { PARAMS[i].to_host(HostSpace::CLAP, p.values()[i]) };
            assert!((got - want).abs() < 1e-5, "{format} {name}: {pname} is {got}, the release read {want}");
        }
        assert_eq!(p.values()[idx::AUTOGAIN], 0.0);
        assert_eq!(p.values()[idx::DELTA], 0.0);
        count += 1;
    }
    count
}

#[test]
fn clap_states_open() {
    assert_eq!(check("clap", |p, b| p.load_state(b)), 2);
}

#[test]
fn vst3_states_open() {
    assert_eq!(check("vst3", |p, b| p.load_foreign_state(ForeignStateKind::Vst3Component, b)), 8);
}

#[test]
fn audio_unit_states_open() {
    assert_eq!(check("au", |p, b| p.load_foreign_state(ForeignStateKind::AuClassInfo, b)), 8);
}

/// A state from before Eco, split oversampling and the bounce choice opens with the original engine.
#[test]
fn a_released_state_without_the_later_rows_keeps_its_engine() {
    let xml = br#"<?xml version="1.0" encoding="UTF-8"?> <TubeChannelState><PARAM id="gain" value="0.5"/><PARAM id="quality" value="1"/></TubeChannelState>"#;
    let mut blob = b"VC2!".to_vec();
    blob.extend_from_slice(&((xml.len() + 1) as u32).to_le_bytes());
    blob.extend_from_slice(xml);
    blob.push(0);
    let mut p = TubeChannel::default();
    assert!(p.load_state(&blob));
    let v = p.values();
    assert_eq!((v[1], v[idx::QUALITY], v[idx::ECO], v[idx::OVERSAMPLING], v[idx::OFFLINE_BOUNCE], v[idx::OFFLINE_BOUNCE_ENABLED]), (0.5, 1.0, 0.0, 0.0, 8.0, 0.0));
}
