//! The parameter table of Tube Channel 1.2.x (Source/Plugin/Params.cpp): same ids, order, ranges,
//! defaults and value text. Ids, order and ranges are part of saved sessions: never rename, reorder
//! or remove a row, only append (PR-3).
//!
//! Hosts saw every parameter of the JUCE build as 0..1, so each row shows hosts normalized values in
//! every format (`with_normalized_in`) and automation recorded against 1.2.x plays back unchanged.
//! Each row is pinned to the id the JUCE build gave hosts: CLAP the 32-bit `juce::String::hashCode`
//! of the id, VST3 and Audio Unit the same hash without its top bit. AAX uses the id string, the
//! default. tests/params.rs checks every pin, default and value text against the scans of the
//! release in plugin/port-scan.

use openplugin::core::HostSpace;
use openplugin::prelude::*;

/// `juce::String::hashCode` of an ASCII id.
pub const fn juce_hash(id: &str) -> u32 {
    let b = id.as_bytes();
    let mut h: u32 = 0;
    let mut i = 0;
    while i < b.len() {
        h = h.wrapping_mul(31).wrapping_add(b[i] as u32);
        i += 1;
    }
    h
}

const NORMALIZED: HostSpace = HostSpace::CLAP.union(HostSpace::SHELLS);

const fn pinned(p: ParamInfo) -> ParamInfo {
    let h = juce_hash(p.id);
    p.with_normalized_in(NORMALIZED).with_clap_id(h).with_vst3_id(h & 0x7fff_ffff).with_au_id(h & 0x7fff_ffff)
}

const fn toggle(id: &'static str, name: &'static str, on: bool) -> ParamInfo {
    pinned(ParamInfo::new(id, name, 0.0, 1.0, if on { 1.0 } else { 0.0 }, Unit::Toggle))
}

const fn knob(id: &'static str, name: &'static str, default: f32, format: fn(f32) -> String, parse: fn(&str) -> Option<f32>) -> ParamInfo {
    pinned(ParamInfo::new(id, name, 0.0, 1.0, default, Unit::None).with_text(format, parse))
}

const fn choice(id: &'static str, name: &'static str, choices: &'static [&'static str], default: f32) -> ParamInfo {
    pinned(ParamInfo::new(id, name, 0.0, (choices.len() - 1) as f32, default, Unit::Choice).with_choices(choices))
}

const fn plain(id: &'static str, name: &'static str, min: f32, max: f32, default: f32, format: fn(f32) -> String, parse: fn(&str) -> Option<f32>) -> ParamInfo {
    pinned(ParamInfo::new(id, name, min, max, default, Unit::None).with_text(format, parse))
}

// ---- display, the same mappings as Source/Plugin/Params.h -------------------------------------------

fn fmt(v: f64, dp: usize, unit: &str) -> String {
    // avoid a misleading "-0.0 dB"
    let v = if dp > 0 && (v * 10f64.powi(dp as i32)).round() == 0.0 { 0.0 } else { v };
    if dp == 0 { format!("{} {unit}", v.round_ties_even() as i64) } else { format!("{v:.dp$} {unit}") }
}

/// The leading number of a value text ("1.00 kHz", "-11.5 dB", "2.0:1"); "mute" and "off" are 0.
fn number(text: &str) -> Option<f64> {
    let t = text.trim();
    let lower = t.to_ascii_lowercase();
    if t.is_empty() || lower.starts_with("mute") || lower.starts_with("off") {
        return Some(0.0);
    }
    let end = t.char_indices().find(|&(i, c)| !(c.is_ascii_digit() || c == '.' || ((c == '-' || c == '+') && i == 0))).map_or(t.len(), |(i, _)| i);
    t[..end].parse().ok()
}

/// The number of a text with an optional unit suffix, scaled by the suffix's factor.
fn scaled(text: &str, units: &[(&str, f64)], bare: f64) -> Option<f64> {
    let t = text.trim();
    let lower = t.to_ascii_lowercase();
    for (unit, factor) in units {
        if lower.ends_with(unit) {
            return number(&t[..t.len() - unit.len()]).map(|v| v * factor);
        }
    }
    number(t).map(|v| v * bare)
}

fn clamp01(v: f64) -> f32 {
    v.clamp(0.0, 1.0) as f32
}

fn bisect(f: impl Fn(f64) -> f64, target: f64, rising: bool, steps: usize) -> f64 {
    let (mut lo, mut hi) = (0.0, 1.0);
    for _ in 0..steps {
        let mid = 0.5 * (lo + hi);
        if (f(mid) < target) == rising { lo = mid } else { hi = mid }
    }
    0.5 * (lo + hi)
}

pub fn gain_db(k: f64) -> f64 { 40.6 * k }
pub fn impedance_ohms(k: f64) -> f64 { 150.0 + 3250.0 * (10f64.powf(2.0 * k) - 1.0) / 99.0 }
pub fn low_cut_hz(k: f64) -> f64 { 11.2 * (250.0f64 / 11.2).powf(k) }
pub fn threshold_dbm(k: f64) -> f64 { -20.0 + 40.0 * k }
pub fn eq_gain_db(k: f64) -> f64 { (k - 0.5) * 24.0 }
pub fn lm_freq_hz(k: f64, x10: bool) -> f64 { let (a, b): (f64, f64) = if x10 { (178.0, 2350.0) } else { (16.0, 214.0) }; a * (b / a).powf(k) }
pub fn hm_freq_hz(k: f64, x10: bool) -> f64 { let (a, b): (f64, f64) = if x10 { (1800.0, 23000.0) } else { (162.0, 2100.0) }; a * (b / a).powf(k) }

/// The labelled ratio for a RATIO knob position (Params.h `ratioValue`): the detector circuit's
/// measured effective ratio for the rheostat value, spread over the 2:1 .. 7:1 scale.
pub fn ratio_value(k: f64) -> f64 {
    const R: [f64; 10] = [0.0, 500.0, 1000.0, 2000.0, 3000.0, 5000.0, 8000.0, 12000.0, 20000.0, 40000.0];
    const RATIO: [f64; 10] = [6.71, 5.61, 4.95, 4.13, 3.63, 3.06, 2.60, 2.25, 1.89, 1.54];
    let k = k.clamp(0.0, 1.0);
    let r = 25e3 * (1.0 - k).powi(3); // ChannelModel::ratioPotOhms
    let eff = if r >= R[9] {
        RATIO[9]
    } else {
        let mut i = 1;
        while r > R[i] { i += 1; }
        RATIO[i - 1] + (RATIO[i] - RATIO[i - 1]) * (r - R[i - 1]) / (R[i] - R[i - 1])
    };
    (2.0 + 5.0 * (eff - 1.8) / (6.71 - 1.8)).clamp(2.0, 7.0)
}

pub fn attack_seconds(k: f64) -> f64 {
    const R: [f64; 4] = [0.0, 5e3, 20e3, 50e3];
    const T: [f64; 4] = [4.3e-3, 10.8e-3, 21.3e-3, 33.3e-3];
    let r = 50e3 * (10f64.powf(2.0 * k) - 1.0) / 99.0;
    let mut i = 1;
    while i < 3 && r > R[i] { i += 1; }
    T[i - 1] + (T[i] - T[i - 1]) * (r - R[i - 1]) / (R[i] - R[i - 1])
}

pub fn release_seconds(k: f64) -> f64 {
    const K: [f64; 5] = [0.0, 0.25, 0.5, 0.75, 1.0];
    const T: [f64; 5] = [0.097, 0.137, 0.185, 0.267, 1.107];
    let mut i = 1;
    while i < 4 && k > K[i] { i += 1; }
    T[i - 1] + (T[i] - T[i - 1]) * (k - K[i - 1]) / (K[i] - K[i - 1])
}

fn audio_taper(k: f64) -> f64 { (10f64.powf(2.0 * k) - 1.0) / 99.0 }

fn gain_text(v: f32) -> String { fmt(gain_db(v.into()), 1, "dB") }
fn gain_parse(t: &str) -> Option<f32> { number(t).map(|db| clamp01(db / 40.6)) }
fn impedance_text(v: f32) -> String {
    let ohm = impedance_ohms(v.into());
    if ohm.round_ties_even() >= 1000.0 { fmt(ohm / 1000.0, 1, "kohm") } else { fmt(ohm, 0, "ohm") }
}
fn impedance_parse(t: &str) -> Option<f32> { scaled(t, &[("kohm", 1000.0), ("ohm", 1.0)], 1.0).map(|ohm| clamp01((1.0 + 99.0 * (ohm - 150.0) / 3250.0).log10() / 2.0)) }
fn low_cut_text(v: f32) -> String { fmt(low_cut_hz(v.into()), 0, "Hz") }
fn low_cut_parse(t: &str) -> Option<f32> { number(t).map(|hz| clamp01((hz / 11.2).ln() / (250.0f64 / 11.2).ln())) }
fn level_text(v: f32, reference: f64) -> String { if v < 0.005 { "mute".into() } else { fmt(20.0 * (f64::from(v) / reference).log10(), 1, "dB") } }
fn level_parse(t: &str, reference: f64) -> Option<f32> {
    if t.trim().to_ascii_lowercase().starts_with("mute") { return Some(0.0); }
    number(t).map(|db| clamp01(reference * 10f64.powf(db / 20.0)))
}
fn preout_text(v: f32) -> String { level_text(v, 1.0) }
fn preout_parse(t: &str) -> Option<f32> { level_parse(t, 1.0) }
fn master_text(v: f32) -> String { level_text(v, 0.8) }
fn master_parse(t: &str) -> Option<f32> { level_parse(t, 0.8) }
fn threshold_text(v: f32) -> String { fmt(threshold_dbm(v.into()), 1, "dBm") }
fn threshold_parse(t: &str) -> Option<f32> { number(t).map(|dbm| clamp01((dbm + 20.0) / 40.0)) }
fn ratio_text(v: f32) -> String { format!("{:.1}:1", ratio_value(v.into())) }
fn ratio_parse(t: &str) -> Option<f32> { number(t).map(|r| bisect(ratio_value, r, true, 30) as f32) }
fn attack_text(v: f32) -> String {
    let s = attack_seconds(v.into());
    if s < 1e-3 { fmt(s * 1e6, 0, "us") } else { fmt(s * 1e3, if s < 0.01 { 1 } else { 0 }, "ms") }
}
fn attack_parse(t: &str) -> Option<f32> { scaled(t, &[("us", 1e-6), ("ms", 1e-3), ("s", 1.0)], 1e-3).map(|s| bisect(attack_seconds, s, true, 40) as f32) }
fn release_text(v: f32) -> String {
    let s = release_seconds(v.into());
    if s < 1.0 { fmt(s * 1e3, 0, "ms") } else { fmt(s, 2, "s") }
}
fn release_parse(t: &str) -> Option<f32> { scaled(t, &[("ms", 1e-3), ("s", 1.0)], 1.0).map(|s| bisect(release_seconds, s, true, 40) as f32) }
fn comp_out_text(v: f32) -> String { if v < 0.01 { "mute".into() } else { fmt(20.0 * (audio_taper(v.into()) / audio_taper(0.88)).log10(), 1, "dB") } }
fn comp_out_parse(t: &str) -> Option<f32> {
    if t.trim().to_ascii_lowercase().starts_with("mute") { return Some(0.0); }
    number(t).map(|db| clamp01((1.0 + 99.0 * audio_taper(0.88) * 10f64.powf(db / 20.0)).log10() / 2.0))
}
fn eq_text(v: f32) -> String { fmt(eq_gain_db(v.into()), 1, "dB") }
fn eq_parse(t: &str) -> Option<f32> { number(t).map(|db| clamp01(db / 24.0 + 0.5)) }
fn lm_freq_text(v: f32) -> String { fmt(lm_freq_hz(v.into(), false), 0, "Hz") }
fn lm_freq_parse(t: &str) -> Option<f32> { number(t).map(|hz| clamp01((hz / 16.0).ln() / (214.0f64 / 16.0).ln())) }
fn hm_freq_text(v: f32) -> String {
    let hz = hm_freq_hz(v.into(), false);
    if hz.round_ties_even() >= 1000.0 { fmt(hz / 1000.0, 2, "kHz") } else { fmt(hz, 0, "Hz") }
}
fn hm_freq_parse(t: &str) -> Option<f32> { scaled(t, &[("khz", 1000.0), ("hz", 1.0)], 1.0).map(|hz| clamp01((hz / 162.0).ln() / (2100.0f64 / 162.0).ln())) }
fn trim_text(v: f32) -> String { fmt(v.into(), 1, "dB") }
fn trim_parse(t: &str) -> Option<f32> { number(t).map(|v| v as f32) }
fn mic_z_text(v: f32) -> String { fmt(v.into(), 0, "ohm") }
fn mic_z_parse(t: &str) -> Option<f32> { number(t).map(|v| v as f32) }
fn inst_z_text(v: f32) -> String {
    let k = f64::from(v) / 1000.0;
    if k.round_ties_even() >= 100.0 { fmt(k, 0, "kohm") } else { fmt(k, 1, "kohm") }
}
fn inst_z_parse(t: &str) -> Option<f32> { scaled(t, &[("kohm", 1.0)], 1.0).map(|k| (k * 1000.0) as f32) }
fn percent_text(v: f32) -> String { fmt(f64::from(v) * 100.0, 0, "%") }
fn percent_parse(t: &str) -> Option<f32> { number(t).map(|v| (v / 100.0) as f32) }
fn seed_text(v: f32) -> String { format!("{}", v.round_ties_even() as i32) }
fn seed_parse(t: &str) -> Option<f32> { number(t).map(|v| v as f32) }

static ALIGNMENT: &[&str] = &["-14 dBFS", "-16 dBFS", "-18 dBFS", "-20 dBFS", "-22 dBFS"];
static QUALITY: &[&str] = &["Circuit", "HQ", "HQ (legacy Derp)"];
static MAINS: &[&str] = &["60 Hz", "50 Hz"];
static OVERSAMPLING: &[&str] = &["Legacy", "Off (1x)", "2x", "4x", "8x (Derp)"];
pub static OFFLINE_BOUNCE: &[&str] = &[
    "Eco · 2x", "Eco · 4x", "Eco · Derp (8x)", "Circuit · 2x", "Circuit · 4x", "Circuit · Derp (8x)", "HQ · 2x", "HQ · 4x", "HQ · Derp (8x)",
];

/// The whole table, written once: channel A, the setup, channel B (the same rows with the suffixes
/// `_b` and ` B`), then the rows later releases appended. Everything after channel A is
/// `PRESET_EXCLUDE`: a factory program sets channel A and leaves the setup and channel B alone
/// (`setCurrentProgram`).
macro_rules! table {
    ([$($f:ident($id:literal, $name:literal $(, $arg:expr)*)),* $(,)?], [$($global:expr),* $(,)?]) => {
        [
            $($f($id, $name $(, $arg)*),)*
            $(excluded($global),)*
            $(excluded($f(concat!($id, "_b"), concat!($name, " B") $(, $arg)*)),)*
            // Appended in 1.1 and later, after channel B, so every released index stays where it was.
            excluded(toggle("ecomode", "Eco mode", true)),
            excluded(choice("oversampling", "Oversampling", OVERSAMPLING, 1.0)),
            excluded(choice("offlinebounce", "Offline Bounce", OFFLINE_BOUNCE, 8.0)),
            excluded(toggle("offlinebounceenabled", "Offline Bounce override", true)),
            // New in the OpenPluginSDK port (2.0).
            excluded(toggle("autogain", "Autogain", false)),
            excluded(toggle("delta", "Delta", false)),
        ]
    };
}

const fn excluded(p: ParamInfo) -> ParamInfo {
    p.with_flag(ParamFlags::PRESET_EXCLUDE)
}

/// Controls per channel, in table order; channel A starts at 0, channel B at [`B`].
pub const PER_CHANNEL: usize = 33;
pub const B: usize = PER_CHANNEL + 15;
pub const COUNT: usize = 2 * PER_CHANNEL + 15 + 4 + 2;

pub static PARAMS: [ParamInfo; COUNT] = table!(
    [
        toggle("instrument", "Instrument input", false),
        knob("gain", "Gain", 0.0, gain_text, gain_parse),
        toggle("gain20", "Gain +20 dB", false),
        knob("impedance", "Impedance", 0.4, impedance_text, impedance_parse),
        toggle("phantom", "Phantom 48V", false),
        toggle("invert", "Invert", false),
        knob("lowcut", "Low cut", 0.0, low_cut_text, low_cut_parse),
        toggle("platehigh", "Plate voltage high", true),
        knob("preout", "Preamp output", 1.0, preout_text, preout_parse),
        toggle("comp", "Compressor on", true),
        knob("threshold", "Threshold", 0.25, threshold_text, threshold_parse),
        knob("ratio", "Ratio", 0.3, ratio_text, ratio_parse),
        knob("attack", "Attack", 0.5, attack_text, attack_parse),
        knob("release", "Release", 0.5, release_text, release_parse),
        toggle("autodetect", "Detect auto", false),
        knob("compout", "Comp output", 0.88, comp_out_text, comp_out_parse),
        toggle("extsc", "External sidechain", false),
        toggle("ratiolimit", "Brickwall limiter", false),
        toggle("eq", "EQ on", true),
        knob("low", "Low", 0.5, eq_text, eq_parse),
        toggle("loshift", "Low shift 40 Hz", true),
        knob("high", "High", 0.5, eq_text, eq_parse),
        toggle("hishift", "High shift 6 kHz", false),
        knob("lmlevel", "Lo-Mid level", 0.5, eq_text, eq_parse),
        knob("lmfreq", "Lo-Mid freq", 0.5, lm_freq_text, lm_freq_parse),
        toggle("lmx10", "Lo-Mid x10", false),
        toggle("lmnarrow", "Lo-Mid narrow", true),
        knob("hmlevel", "Hi-Mid level", 0.5, eq_text, eq_parse),
        knob("hmfreq", "Hi-Mid freq", 0.5, hm_freq_text, hm_freq_parse),
        toggle("hmx10", "Hi-Mid x10", false),
        toggle("hmnarrow", "Hi-Mid narrow", true),
        knob("master", "Master", 0.8, master_text, master_parse),
        toggle("outlevel", "Output +4 dBu", true),
    ],
    [
        plain("inputtrim", "Input trim", -24.0, 24.0, 0.0, trim_text, trim_parse),
        plain("outputtrim", "Output trim", -24.0, 24.0, 0.0, trim_text, trim_parse),
        choice("alignment", "0 VU level", ALIGNMENT, 2.0),
        plain("micz", "Mic source impedance", 50.0, 600.0, 150.0, mic_z_text, mic_z_parse),
        plain("instz", "Instrument source impedance", 1000.0, 100_000.0, 10_000.0, inst_z_text, inst_z_parse),
        choice("quality", "Quality", QUALITY, 0.0),
        toggle("linearphase", "Linear phase (+L)", false),
        toggle("stereolink", "Stereo link", true),
        toggle("dualmono", "Dual mono", false),
        choice("mains", "Mains frequency", MAINS, 0.0),
        plain("mainsvolts", "Mains voltage", 0.85, 1.1, 1.0, percent_text, percent_parse),
        toggle("tolerances", "Component tolerances", true),
        plain("seed", "Unit seed", 0.0, 99.0, 1.0, seed_text, seed_parse),
        toggle("noise", "Circuit noise", true),
        toggle("psumodel", "Power supply model", false),
    ]
);

/// Table positions of the parameters the plugin layer reads. Channel controls are `B + i` for channel B.
pub mod idx {
    use super::{B, PER_CHANNEL};
    pub const INSTRUMENT: usize = 0;
    pub const GAIN: usize = 1;
    pub const PREOUT: usize = 8;
    pub const COMP: usize = 9;
    pub const RELEASE: usize = 13;
    pub const RATIO_LIMIT: usize = 17;
    pub const MASTER: usize = 31;
    pub const INPUT_TRIM: usize = PER_CHANNEL;
    pub const OUTPUT_TRIM: usize = PER_CHANNEL + 1;
    pub const ALIGNMENT: usize = PER_CHANNEL + 2;
    pub const QUALITY: usize = PER_CHANNEL + 5;
    pub const LINEAR_PHASE: usize = PER_CHANNEL + 6;
    pub const STEREO_LINK: usize = PER_CHANNEL + 7;
    pub const DUAL_MONO: usize = PER_CHANNEL + 8;
    pub const MAINS: usize = PER_CHANNEL + 9;
    pub const MAINS_VOLTS: usize = PER_CHANNEL + 10;
    pub const TOLERANCES: usize = PER_CHANNEL + 11;
    pub const SEED: usize = PER_CHANNEL + 12;
    pub const NOISE: usize = PER_CHANNEL + 13;
    pub const PSU_MODEL: usize = PER_CHANNEL + 14;
    pub const ECO: usize = B + PER_CHANNEL;
    pub const OVERSAMPLING: usize = ECO + 1;
    pub const OFFLINE_BOUNCE: usize = ECO + 2;
    pub const OFFLINE_BOUNCE_ENABLED: usize = ECO + 3;
    pub const AUTOGAIN: usize = ECO + 4;
    pub const DELTA: usize = ECO + 5;
    pub const RELEASE_B: usize = B + RELEASE;
}

pub fn index_of(id: &str) -> Option<usize> {
    PARAMS.iter().position(|p| p.id == id)
}

pub fn defaults() -> [f32; COUNT] {
    std::array::from_fn(|i| PARAMS[i].default)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn layout() {
        assert_eq!(PARAMS[idx::INPUT_TRIM].id, "inputtrim");
        assert_eq!(PARAMS[idx::PSU_MODEL].id, "psumodel");
        assert_eq!(PARAMS[B].id, "instrument_b");
        assert_eq!(PARAMS[idx::RELEASE_B].id, "release_b");
        assert_eq!(PARAMS[idx::MASTER].id, "master");
        assert_eq!(PARAMS[idx::ECO].id, "ecomode");
        assert_eq!(PARAMS[idx::DELTA].id, "delta");
        let mut ids: Vec<_> = PARAMS.iter().map(|p| p.id).collect();
        ids.sort_unstable();
        ids.dedup();
        assert_eq!(ids.len(), COUNT, "duplicate ids");
    }

    #[test]
    fn text_round_trips() {
        for p in PARAMS.iter() {
            for k in 0..=20 {
                let v = p.min + (p.max - p.min) * k as f32 / 20.0;
                let v = p.clamp(v);
                let text = p.format(v);
                let back = p.parse(&text).unwrap_or_else(|| panic!("{}: {text:?} does not parse", p.id));
                assert_eq!(p.format(p.clamp(back)), text, "{} at {v}", p.id);
            }
        }
    }
}
