//! What is on the editor: one list of widgets per page, in the 1200 x 284 units of the JUCE
//! release's editor (Source/Plugin/PluginEditor.cpp `Content::build`), at the same places.
//!
//! Four pages share the toolbar: the analogue front panel, the digital front page, and each
//! skin's System page. Channel controls address channel A, or channel B when the Dual mono
//! tabs select it.

use crate::params::{idx, B};

/// (x, y, w, h) in editor units.
pub type R = (f32, f32, f32, f32);

pub const W: f32 = 1200.0;
pub const H: f32 = 284.0;
pub const TOP: f32 = 40.0;
pub const EAR: f32 = 40.0;

pub const POWER_P: R = (8.0, 46.0, 612.0, 236.0);
pub const OS_P: R = (628.0, 46.0, 290.0, 236.0);
pub const UNIT_P: R = (926.0, 46.0, 266.0, 236.0);
pub const VU_BOUNDS: R = (985.0, 54.0, 155.0, 100.0);

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Page {
    Analogue,
    Digital,
    AnalogueSystem,
    DigitalSystem,
}

impl Page {
    pub fn of(digital: bool, system: bool) -> Self {
        match (digital, system) {
            (false, false) => Page::Analogue,
            (true, false) => Page::Digital,
            (false, true) => Page::AnalogueSystem,
            (true, true) => Page::DigitalSystem,
        }
    }
    pub fn digital(self) -> bool {
        matches!(self, Page::Digital | Page::DigitalSystem)
    }
    pub fn system(self) -> bool {
        matches!(self, Page::AnalogueSystem | Page::DigitalSystem)
    }
}

/// A drop-down list open over the page (the release's popup menus).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Overlay {
    None,
    Presets,
    Bounce,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Action {
    Presets,
    Ab,
    Copy,
    Bounce,
    System,
    Skin,
    Channel(bool),
    Program(usize),
    BounceChoice(usize),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Bank {
    /// Eco / Circuit / HQ: the Eco and Quality parameters together.
    Quality,
    /// 2x / 4x / Derp; the lit key again turns oversampling off.
    Oversampling,
    Mains,
    /// The meter's source: PRE / COMP / EQ / OUT (saved with the session, not a parameter).
    VuSource,
}

impl Bank {
    pub fn names(self) -> &'static [&'static str] {
        match self {
            Bank::Quality => &["Eco", "Circuit", "HQ"],
            Bank::Oversampling => &["2x", "4x", "Derp"],
            Bank::Mains => &["60 Hz", "50 Hz"],
            Bank::VuSource => &["PRE", "COMP", "EQ", "OUT"],
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Ladder {
    GainReduction,
    Warmth,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Kind {
    /// Title above, printed scale around, value below when `value`. `click` is the on/off
    /// parameter a plain click on the knob flips (the RATIO knob pops out to LIMITER).
    Knob { param: usize, title: &'static str, legends: &'static [&'static str], body: f32, value: bool, click: Option<usize> },
    Slider { param: usize, title: &'static str, title_w: f32, value_w: f32 },
    Led { param: usize, on: &'static str, off: &'static str, led: Option<u32> },
    Button { action: Action, text: &'static str },
    Bank(Bank),
    Vu,
    Ladder(Ladder),
    Clip { output: bool },
    Power,
    /// The graphs set the controls of the channel at offset `ch`.
    Comp { ch: usize },
    Eq { ch: usize },
    Status,
    /// The open list's backdrop: catches clicks outside it.
    Backdrop,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Widget {
    pub rect: R,
    pub kind: Kind,
}

impl Widget {
    /// The parameters the widget sets.
    pub fn params(&self) -> Vec<usize> {
        match self.kind {
            Kind::Knob { param, click, .. } => std::iter::once(param).chain(click).collect(),
            Kind::Slider { param, .. } | Kind::Led { param, .. } => vec![param],
            Kind::Bank(Bank::Quality) => vec![idx::QUALITY, idx::ECO, idx::OVERSAMPLING],
            Kind::Bank(Bank::Oversampling) => vec![idx::OVERSAMPLING],
            Kind::Bank(Bank::Mains) => vec![idx::MAINS],
            Kind::Comp { ch } => [THRESHOLD, RATIO, COMPOUT, RATIO_LIMIT].iter().map(|p| p + ch).collect(),
            Kind::Eq { ch } => [LOW, LOSHIFT, HIGH, HISHIFT, LMLEVEL, LMFREQ, LMX10, LMNARROW, HMLEVEL, HMFREQ, HMX10, HMNARROW, EQ].iter().map(|p| p + ch).collect(),
            _ => Vec::new(),
        }
    }
}

/// Knob widget geometry (`Knob::widthFor`, `heightFor`, `centreY`).
pub fn knob_width(d: f32) -> f32 {
    d + 42.0
}
pub fn knob_height(d: f32, value: bool) -> f32 {
    (d * 0.5 + 28.0 + (d * 0.5 + 11.0) * 0.81 + 12.0).floor() + if value { 12.0 } else { 0.0 }
}
/// The knob body's centre and radius.
pub fn knob_circle(w: &Widget) -> (f32, f32, f32) {
    let Kind::Knob { body, .. } = w.kind else { return (0.0, 0.0, 0.0) };
    (w.rect.0 + w.rect.2 * 0.5, w.rect.1 + 28.0 + (body / 2.0).floor(), body * 0.5)
}

pub const LED_H: f32 = 34.0;

/// Positions of a channel's parameter (`base` is the channel A index).
struct Builder {
    out: Vec<Widget>,
    ch: usize,
}

#[allow(clippy::too_many_arguments)] // one argument per layout field, as in the release's helpers
impl Builder {
    fn p(&self, base: usize) -> usize {
        base + self.ch
    }
    fn knob(&mut self, base: usize, title: &'static str, legends: &'static [&'static str], cx: f32, top: f32, d: f32, value: bool, per_channel: bool) {
        let param = if per_channel { self.p(base) } else { base };
        let w = knob_width(d);
        self.out.push(Widget { rect: (cx - (w / 2.0).floor(), top, w, knob_height(d, value)), kind: Kind::Knob { param, title, legends, body: d, value, click: None } });
    }
    fn slider(&mut self, base: usize, title: &'static str, x: f32, y: f32, w: f32, per_channel: bool, title_w: f32, value_w: f32) {
        let param = if per_channel { self.p(base) } else { base };
        self.out.push(Widget { rect: (x, y, w, 24.0), kind: Kind::Slider { param, title, title_w, value_w } });
    }
    fn led(&mut self, base: usize, on: &'static str, off: &'static str, cx: f32, top: f32, w: f32, per_channel: bool, led: Option<u32>) {
        let param = if per_channel { self.p(base) } else { base };
        self.out.push(Widget { rect: (cx - (w / 2.0).floor(), top, w, LED_H), kind: Kind::Led { param, on, off, led } });
    }
    fn add(&mut self, rect: R, kind: Kind) {
        self.out.push(Widget { rect, kind });
    }
}

// Channel control positions in the parameter table (src/params.rs).
mod c {
    pub const INSTRUMENT: usize = 0;
    pub const GAIN: usize = 1;
    pub const GAIN20: usize = 2;
    pub const IMPEDANCE: usize = 3;
    pub const PHANTOM: usize = 4;
    pub const INVERT: usize = 5;
    pub const LOWCUT: usize = 6;
    pub const PLATE_HIGH: usize = 7;
    pub const PREOUT: usize = 8;
    pub const COMP: usize = 9;
    pub const THRESHOLD: usize = 10;
    pub const RATIO: usize = 11;
    pub const ATTACK: usize = 12;
    pub const RELEASE: usize = 13;
    pub const AUTO: usize = 14;
    pub const COMPOUT: usize = 15;
    pub const EXTSC: usize = 16;
    pub const RATIO_LIMIT: usize = 17;
    pub const EQ: usize = 18;
    pub const LOW: usize = 19;
    pub const LOSHIFT: usize = 20;
    pub const HIGH: usize = 21;
    pub const HISHIFT: usize = 22;
    pub const LMLEVEL: usize = 23;
    pub const LMFREQ: usize = 24;
    pub const LMX10: usize = 25;
    pub const LMNARROW: usize = 26;
    pub const HMLEVEL: usize = 27;
    pub const HMFREQ: usize = 28;
    pub const HMX10: usize = 29;
    pub const HMNARROW: usize = 30;
    pub const MASTER: usize = 31;
    pub const OUTLEVEL: usize = 32;
}
pub use c::*;

/// The widgets of `page`, channel B's controls when `channel_b`. `dual` shows the channel tabs.
pub fn widgets(page: Page, channel_b: bool, dual: bool, overlay: Overlay) -> Vec<Widget> {
    let mut b = Builder { out: Vec::new(), ch: if channel_b { B } else { 0 } };
    let red = Some(0xffe5_382a);

    // ---- toolbar (always visible) ----
    b.add((300.0, 7.0, if dual { 100.0 } else { 220.0 }, 26.0), Kind::Button { action: Action::Presets, text: "" });
    if dual {
        b.add((410.0, 7.0, 52.0, 26.0), Kind::Button { action: Action::Channel(false), text: "CH A" });
        b.add((466.0, 7.0, 52.0, 26.0), Kind::Button { action: Action::Channel(true), text: "CH B" });
    }
    b.add((530.0, 7.0, 40.0, 26.0), Kind::Button { action: Action::Ab, text: "A" });
    b.add((574.0, 7.0, 52.0, 26.0), Kind::Button { action: Action::Copy, text: "COPY" });
    b.add((768.0, 7.0, 170.0, 26.0), Kind::Button { action: Action::Bounce, text: "OFFLINE BOUNCE  \u{25be}" });
    b.add((950.0, 7.0, 100.0, 26.0), Kind::Button { action: Action::System, text: "SYSTEM" });
    b.add((1070.0, 7.0, 116.0, 26.0), Kind::Button { action: Action::Skin, text: "" });

    match page {
        Page::Analogue => {
            let (r1, r2, keys) = (46.0, 144.0, 228.0);
            b.knob(GAIN, "GAIN", &["0", "10", "20", "30", "40"], 110.0, r1, 46.0, false, true);
            b.knob(LOWCUT, "LOW CUT", &["10", "", "250"], 195.0, r1 + 4.0, 38.0, false, true);
            b.knob(PREOUT, "PREAMP", &["MUTE", "", "0"], 265.0, r1 + 4.0, 38.0, false, true);
            b.knob(IMPEDANCE, "IMPEDANCE", &["150", "", "3.4k"], 110.0, r2, 38.0, false, true);
            b.knob(THRESHOLD, "THRESHOLD", &["-20", "0", "+20"], 350.0, r1 + 4.0, 38.0, false, true);
            b.knob(RATIO, "RATIO", &["2:1", "", "7:1"], 420.0, r1 + 4.0, 38.0, false, true);
            if let Some(Widget { kind: Kind::Knob { click, .. }, .. }) = b.out.last_mut() {
                *click = Some(RATIO_LIMIT + if channel_b { B } else { 0 });
            }
            b.knob(COMPOUT, "OUTPUT", &["MUTE", "", "+5"], 490.0, r1 + 4.0, 38.0, false, true);
            b.knob(ATTACK, "ATTACK", &["4ms", "", "33ms"], 395.0, r2, 38.0, false, true);
            b.knob(RELEASE, "RELEASE", &[".1s", "", "1.1s"], 465.0, r2, 38.0, false, true);
            b.knob(LOW, "LOW", &["-12", "0", "+12"], 620.0, r1 + 4.0, 38.0, false, true);
            b.knob(LMLEVEL, "LOW-MID", &["-12", "0", "+12"], 690.0, r1 + 4.0, 38.0, false, true);
            b.knob(HMLEVEL, "HIGH-MID", &["-12", "0", "+12"], 760.0, r1 + 4.0, 38.0, false, true);
            b.knob(HIGH, "HIGH", &["-12", "0", "+12"], 830.0, r1 + 4.0, 38.0, false, true);
            b.knob(LMFREQ, "LM FREQ", &[], 690.0, r2, 38.0, false, true);
            b.knob(HMFREQ, "HM FREQ", &[], 760.0, r2, 38.0, false, true);
            b.knob(MASTER, "MASTER", &["MIN", "", "MAX"], 915.0, r1, 46.0, false, true);
            let k2 = r2 + 30.0;
            b.led(INSTRUMENT, "INST", "MIC", 170.0, k2, 52.0, true, None);
            b.led(GAIN20, "+20 dB", "", 222.0, k2, 52.0, true, None);
            b.led(PHANTOM, "+48 V", "", 274.0, k2, 52.0, true, red);
            b.led(INVERT, "PHASE", "", 326.0, k2, 52.0, true, None);
            b.led(PLATE_HIGH, "PLATE HI", "NORM", 336.0, keys, 56.0, true, None);
            b.led(COMP, "ACTIVE", "BYPASS", 400.0, keys, 56.0, true, None);
            b.led(AUTO, "AUTO", "MANUAL", 460.0, keys, 56.0, true, None);
            b.led(EXTSC, "EXT SC", "INT SC", 520.0, keys, 56.0, true, Some(0xff5a_c8ff));
            b.led(LOSHIFT, "40Hz", "120Hz", 600.0, keys, 46.0, true, None);
            b.led(LMX10, "X10", "", 648.0, keys, 46.0, true, None);
            b.led(LMNARROW, "NARROW", "WIDE", 696.0, keys, 46.0, true, None);
            b.led(HMX10, "X10", "", 752.0, keys, 46.0, true, None);
            b.led(HMNARROW, "NARROW", "WIDE", 800.0, keys, 46.0, true, None);
            b.led(HISHIFT, "6kHz", "18kHz", 848.0, keys, 46.0, true, None);
            b.led(EQ, "EQ IN", "EQ OUT", 896.0, keys, 46.0, true, None);
            b.add((525.0, r1 + 14.0, 46.0, 52.0), Kind::Clip { output: false });
            b.add(VU_BOUNDS, Kind::Vu);
            b.add((985.0, 160.0, 155.0, 12.0), Kind::Ladder(Ladder::GainReduction));
            b.add((985.0, 190.0, 155.0, 12.0), Kind::Ladder(Ladder::Warmth));
            b.add((985.0, 218.0, 155.0, 20.0), Kind::Bank(Bank::VuSource));
            b.led(OUTLEVEL, "+4 dBu", "-10 dBV", 1100.0, 244.0, 56.0, true, None);
            // Not on the release's panel, which predates them: the port's Autogain and Delta.
            b.led(idx::AUTOGAIN, "AUTOGAIN", "", 1010.0, 244.0, 48.0, false, None);
            b.led(idx::DELTA, "DELTA", "", 1052.0, 244.0, 36.0, false, Some(0xffff_c04a));
        }
        Page::Digital => {
            // One grid: four columns, hero area y 66..216, slider rows 36 apart, ladder row at 198, key row at 232.
            let k_row = 232.0;
            let key = |x0: f32, spacing: f32, i: usize| x0 + 25.0 + i as f32 * spacing;
            b.knob(GAIN, "GAIN", &[], 80.0, 62.0, 56.0, true, true);
            b.slider(idx::INPUT_TRIM, "TRIM", 22.0, 186.0, 110.0, false, 28.0, 46.0);
            b.slider(IMPEDANCE, "IMPEDANCE", 142.0, 74.0, 180.0, true, 66.0, 52.0);
            b.slider(LOWCUT, "LOW CUT", 142.0, 110.0, 180.0, true, 66.0, 52.0);
            b.slider(PREOUT, "PREAMP OUT", 142.0, 146.0, 180.0, true, 66.0, 52.0);
            b.add((142.0, 198.0, 180.0, 10.0), Kind::Ladder(Ladder::Warmth));
            b.led(INSTRUMENT, "INST", "MIC", key(22.0, 60.0, 0), k_row, 50.0, true, None);
            b.led(GAIN20, "+20 dB", "", key(22.0, 60.0, 1), k_row, 50.0, true, None);
            b.led(PHANTOM, "+48 V", "", key(22.0, 60.0, 2), k_row, 50.0, true, red);
            b.led(INVERT, "PHASE", "", key(22.0, 60.0, 3), k_row, 50.0, true, None);
            b.led(PLATE_HIGH, "PLATE HI", "NORM", key(22.0, 60.0, 4), k_row, 50.0, true, None);
            b.add((336.0, 66.0, 140.0, 150.0), Kind::Comp { ch: b.ch });
            b.slider(ATTACK, "ATTACK", 488.0, 74.0, 164.0, true, 56.0, 52.0);
            b.slider(RELEASE, "RELEASE", 488.0, 110.0, 164.0, true, 56.0, 52.0);
            b.slider(COMPOUT, "OUTPUT", 488.0, 146.0, 164.0, true, 56.0, 52.0);
            b.add((488.0, 198.0, 164.0, 10.0), Kind::Ladder(Ladder::GainReduction));
            let cx0 = 336.0 + (316.0 - 4.0 * 60.0 + 10.0) / 2.0 - 5.0;
            b.led(COMP, "ACTIVE", "BYPASS", key(cx0, 60.0, 0), k_row, 50.0, true, None);
            b.led(AUTO, "AUTO", "MANUAL", key(cx0, 60.0, 1), k_row, 50.0, true, None);
            b.led(EXTSC, "EXT SC", "INT SC", key(cx0, 60.0, 2), k_row, 50.0, true, Some(0xff39_d0ff));
            b.led(RATIO_LIMIT, "LIMIT", "", key(cx0, 60.0, 3), k_row, 50.0, true, Some(0xffff_c04a));
            b.add((666.0, 66.0, 356.0, 150.0), Kind::Eq { ch: b.ch });
            b.led(LOSHIFT, "40Hz", "120Hz", key(666.0, 51.0, 0) - 2.0, k_row, 50.0, true, None);
            b.led(LMX10, "LM X10", "", key(666.0, 51.0, 1) - 2.0, k_row, 50.0, true, None);
            b.led(LMNARROW, "NARROW", "WIDE", key(666.0, 51.0, 2) - 2.0, k_row, 50.0, true, None);
            b.led(HMX10, "HM X10", "", key(666.0, 51.0, 3) - 2.0, k_row, 50.0, true, None);
            b.led(HMNARROW, "NARROW", "WIDE", key(666.0, 51.0, 4) - 2.0, k_row, 50.0, true, None);
            b.led(HISHIFT, "6kHz", "18kHz", key(666.0, 51.0, 5) - 2.0, k_row, 50.0, true, None);
            b.led(EQ, "EQ IN", "EQ OUT", key(666.0, 51.0, 6) - 2.0, k_row, 50.0, true, None);
            b.knob(MASTER, "MASTER", &[], 1107.0, 62.0, 30.0, true, true);
            // The port's Autogain and Delta, beside Master.
            b.led(idx::AUTOGAIN, "AUTOGAIN", "", 1060.0, 72.0, 48.0, false, None);
            b.led(idx::DELTA, "DELTA", "", 1154.0, 72.0, 44.0, false, Some(0xffff_c04a));
            b.slider(idx::OUTPUT_TRIM, "TRIM", 1036.0, 150.0, 142.0, false, 32.0, 52.0);
            b.add((1036.0, 176.0, 142.0, 34.0), Kind::Vu);
            b.add((1036.0, 212.0, 142.0, 18.0), Kind::Bank(Bank::VuSource));
            b.led(OUTLEVEL, "+4 dBu", "-10 dBV", 1076.0, k_row, 56.0, true, None);
            b.add((1136.0, k_row, 42.0, 36.0), Kind::Clip { output: true });
        }
        Page::AnalogueSystem | Page::DigitalSystem => {
            let (px, py, pw, ph) = POWER_P;
            let (ox, oy, ..) = OS_P;
            let (ux, uy, ..) = UNIT_P;
            b.add((px + 8.0, py + 26.0, pw - 16.0, ph - 32.0), Kind::Power);
            b.add((ox + 12.0, oy + 28.0, 164.0, 36.0), Kind::Bank(Bank::Quality));
            b.add((ox + 180.0, oy + 28.0, 98.0, 36.0), Kind::Bank(Bank::Oversampling));
            b.led(idx::LINEAR_PHASE, "+L LINEAR", "+L MIN PH", ox + 62.0, oy + 76.0, 100.0, false, None);
            b.led(idx::STEREO_LINK, "LINK", "UNLINK", ox + 145.0, oy + 76.0, 76.0, false, None);
            b.led(idx::DUAL_MONO, "DUAL", "MONO", ox + 232.0, oy + 76.0, 76.0, false, None);
            b.add((ox + 14.0, oy + 112.0, 262.0, 30.0), Kind::Status);
            b.add((ux + 12.0, uy + 28.0, 100.0, 30.0), Kind::Bank(Bank::Mains));
            b.led(idx::TOLERANCES, "TOL", "IDEAL", ux + 134.0, uy + 26.0, 48.0, false, None);
            b.led(idx::NOISE, "NOISE", "SILENT", ux + 186.0, uy + 26.0, 48.0, false, None);
            b.led(idx::PSU_MODEL, "PSU SIM", "FIXED", ux + 238.0, uy + 26.0, 48.0, false, None);
            if page == Page::AnalogueSystem {
                b.knob(idx::INPUT_TRIM, "IN TRIM", &[], ox + 50.0, oy + 142.0, 34.0, true, false);
                b.knob(idx::ALIGNMENT, "0 VU", &[], ox + 145.0, oy + 142.0, 34.0, true, false);
                b.knob(idx::OUTPUT_TRIM, "OUT TRIM", &[], ox + 240.0, oy + 142.0, 34.0, true, false);
                let (kx, ky) = (ux + 45.0, uy + 98.0);
                b.knob(idx::MAINS_VOLTS, "MAINS V", &[], kx, ky, 34.0, true, false);
                b.knob(idx::SEED, "SEED", &[], kx + 63.0, ky, 34.0, true, false);
                b.knob(MIC_Z, "MIC Z", &[], kx + 126.0, ky, 34.0, true, false);
                b.knob(INST_Z, "INST Z", &[], kx + 189.0, ky, 34.0, true, false);
            } else {
                b.slider(idx::INPUT_TRIM, "IN TRIM", ox + 12.0, oy + 150.0, 266.0, false, 74.0, 52.0);
                b.slider(idx::ALIGNMENT, "0 VU", ox + 12.0, oy + 178.0, 266.0, false, 74.0, 52.0);
                b.slider(idx::OUTPUT_TRIM, "OUT TRIM", ox + 12.0, oy + 206.0, 266.0, false, 74.0, 52.0);
                b.slider(idx::MAINS_VOLTS, "MAINS V", ux + 12.0, uy + 100.0, 242.0, false, 74.0, 52.0);
                b.slider(idx::SEED, "SEED", ux + 12.0, uy + 134.0, 242.0, false, 74.0, 52.0);
                b.slider(MIC_Z, "MIC Z", ux + 12.0, uy + 168.0, 242.0, false, 74.0, 52.0);
                b.slider(INST_Z, "INST Z", ux + 12.0, uy + 202.0, 242.0, false, 74.0, 52.0);
            }
        }
    }

    // ---- an open list, over everything ----
    match overlay {
        Overlay::None => {}
        Overlay::Presets => {
            b.add((0.0, 0.0, W, H), Kind::Backdrop);
            for (i, p) in crate::programs::PROGRAMS.iter().enumerate() {
                b.add((300.0, 36.0 + 24.0 * i as f32, 232.0, 24.0), Kind::Button { action: Action::Program(i), text: p.name });
            }
        }
        Overlay::Bounce => {
            b.add((0.0, 0.0, W, H), Kind::Backdrop);
            for (i, name) in crate::params::OFFLINE_BOUNCE.iter().enumerate() {
                b.add((768.0, 36.0 + 24.0 * i as f32 + 4.0 * (i / 3) as f32, 232.0, 24.0), Kind::Button { action: Action::BounceChoice(i), text: name });
            }
        }
    }
    b.out
}

pub const MIC_Z: usize = idx::INPUT_TRIM + 3;
pub const INST_Z: usize = idx::INPUT_TRIM + 4;
