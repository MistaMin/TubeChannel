//! Drawing the pages with GoodLookin DRI, in editor units: the JUCE release's Content::paint, the
//! widgets of Source/UI/Widgets.h and Panels.h, and the displays of Digital.h.

use std::hash::{Hash, Hasher};

use openplugin::dri::goodlookindri::color::Color;
use openplugin::dri::goodlookindri::design::Item;
use openplugin::dri::goodlookindri::meters::{self, VuFace};
use openplugin::dri::goodlookindri::scene::{Point, Rect, Scene};
use openplugin::dri::goodlookindri::{draw, hardware};

use super::graphs::{Bands, CompCurve, CompPlot, EqPlot, Spectrum, BAND_COLOURS, BAND_NAMES, COMP_HI, COMP_LO};
use super::lettering;
use super::panel::*;
use crate::engine::{self, SCOPE};
use crate::params::{idx, COUNT, PARAMS};

// ---- skin ---------------------------------------------------------------------------------------------------

#[derive(Clone, Copy)]
pub struct Skin {
    pub digital: bool,
    pub legend: Color,
    pub legend_dim: Color,
    pub led: Color,
    pub accent: Color,
    pub line: Color,
    pub extra_top: Color,
    pub extra_bottom: Color,
    pub panel_bottom: Color,
}

impl Skin {
    pub fn of(digital: bool) -> Self {
        if digital {
            Self {
                digital,
                legend: Color(0xffdb_e8ff),
                legend_dim: Color(0xff6f_86a8),
                led: Color(0xff39_d0ff),
                accent: Color(0xff39_d0ff),
                line: Color(0xff3a_5679),
                extra_top: Color(0xff0f_1d2f),
                extra_bottom: Color(0xff09_131f),
                panel_bottom: Color(0xff08_111c),
            }
        } else {
            Self {
                digital,
                legend: Color(0xfff0_eee8),
                legend_dim: Color(0xff9a_9a96),
                led: Color(0xffff_7a22),
                accent: Color(0xffff_9a2a),
                line: Color(0xffd9_d7d0),
                extra_top: Color(0xff26_282a),
                extra_bottom: Color(0xff17_191a),
                panel_bottom: Color(0xff12_1415),
            }
        }
    }
}

// ---- what the widgets show ------------------------------------------------------------------------------------

/// Display-only peak response: immediate attack, 24 dB/s release, one-second peak hold (MeterBallistics.h).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Ballistics {
    pub db: f32,
    pub held_db: f32,
    hold: f32,
}

pub const FLOOR_DB: f32 = -60.0;

impl Default for Ballistics {
    fn default() -> Self {
        Self { db: FLOOR_DB, held_db: FLOOR_DB, hold: 0.0 }
    }
}

impl Ballistics {
    pub fn update(&mut self, amplitude: f32, elapsed: f32) {
        let target = if amplitude.is_finite() && amplitude > 1e-3 { 20.0 * amplitude.log10() } else { FLOOR_DB };
        self.db = target.max(self.db - 24.0 * elapsed.max(0.0));
        if target >= self.held_db {
            self.held_db = target;
            self.hold = 1.0;
        } else {
            let release = (elapsed - self.hold).max(0.0);
            self.hold = (self.hold - elapsed).max(0.0);
            self.held_db = self.db.max(self.held_db - 24.0 * release);
        }
    }
}

/// The meters' state, kept by the editor between ticks.
#[derive(Default)]
pub struct Meters {
    pub needle: f32,
    pub bars: [Ballistics; 2],
    pub display_db: f32,
    pub channels: usize,
    pub gain_reduction: f32,
    pub warmth: f32,
    pub clip: f32,
    pub comp_gr: f32,
    /// Channel A's telemetry for the power panel, and the rails' slow averages.
    pub power: Option<Box<engine::Meters>>,
    pub rail_avg: [f32; 6],
    /// The averages have taken a first reading.
    pub rails_read: bool,
    pub flow_phase: f32,
}

impl Meters {
    pub fn readout(&self) -> String {
        if self.display_db <= FLOOR_DB {
            return "-inf".into();
        }
        let r = (self.display_db * 10.0).round() / 10.0;
        if r > 0.0 { format!("+{r:.1}") } else { format!("{:.1}", if r == 0.0 { 0.0 } else { r }) }
    }
}

/// Everything a widget's look depends on.
pub struct Looks<'a> {
    pub values: &'a [f32; COUNT],
    pub page: Page,
    pub skin: Skin,
    /// The offset of the channel the widgets address (0, or channel B's).
    pub ch: usize,
    pub channel_b: bool,
    pub hot: Option<(usize, usize)>,
    pub down: Option<usize>,
    pub meters: &'a Meters,
    pub spectrum: &'a Spectrum,
    pub spectrum_frame: u64,
    pub vu_source: usize,
    pub program: &'a str,
    pub program_index: usize,
    pub ab_is_b: bool,
    pub offline_active: bool,
    pub status: &'a (String, String),
    /// Rates for the legacy oversampling display (`stagesFor`).
    pub sample_rate: f32,
}

fn c(argb: u32) -> Color {
    Color(argb)
}

fn rect(r: R) -> Rect {
    Rect::new(r.0, r.1, r.2, r.3)
}

fn text_c(s: &mut Scene, x: f32, y: f32, t: &str, colour: Color, size: f32, bold: bool) {
    lettering::draw(s, x - lettering::width(t, size, bold) * 0.5, y, t, colour, size, bold);
}

fn text_l(s: &mut Scene, x: f32, y: f32, t: &str, colour: Color, size: f32, bold: bool) {
    lettering::draw(s, x, y, t, colour, size, bold);
}

fn text_r(s: &mut Scene, right: f32, y: f32, t: &str, colour: Color, size: f32, bold: bool) {
    lettering::draw(s, right - lettering::width(t, size, bold), y, t, colour, size, bold);
}

fn key_fill(s: &mut Scene, r: Rect, sk: &Skin, lit: bool, radius: f32) {
    s.fill_round(r.expanded(1.0), radius + 0.5, c(0xff05_0505), c(0xff05_0505), true);
    let (top, bottom) = if lit {
        (sk.accent.darker(0.15), sk.accent.darker(0.6))
    } else if sk.digital {
        (c(0xff1a_2c42), c(0xff0e_1a2a))
    } else {
        (c(0xff30_3234), c(0xff16_1718))
    };
    s.fill_round(r, radius, top, bottom, true);
}

// ---- the page behind the widgets ------------------------------------------------------------------------------

fn plate(s: &mut Scene, sk: &Skin, r: R) {
    let r = rect(r);
    if sk.digital {
        s.fill_round(r, 5.0, sk.extra_top, sk.extra_bottom, true);
        s.stroke_round(r, 5.0, c(0xb300_0000), 1.5);
        s.stroke_round(r.reduced(1.5, 1.5), 4.0, Color(0xffff_ffff).with_alpha(0.05), 1.0);
    } else {
        hardware::draw_brushed_panel(s, r, c(0xff26_272a), c(0xff11_1213), (r.x + r.y) as u32);
        s.stroke_round(r, 0.0, c(0xcc00_0000), 1.5);
        for (x, y) in [(r.x + 9.0, r.y + 9.0), (r.right() - 9.0, r.y + 9.0), (r.x + 9.0, r.bottom() - 9.0), (r.right() - 9.0, r.bottom() - 9.0)] {
            hardware::draw_screw(s, x, y);
        }
    }
}

fn section_line(s: &mut Scene, sk: &Skin, x0: f32, x1: f32, y: f32, title: &str) {
    if sk.digital {
        let tw = lettering::width(title, 10.5, true) + 12.0;
        let cx = 0.5 * (x0 + x1);
        s.fill_rect(Rect::new(x0, y, cx - tw * 0.5 - x0, 1.2), sk.line, sk.line, true);
        s.fill_rect(Rect::new(cx + tw * 0.5, y, x1 - cx - tw * 0.5, 1.2), sk.line, sk.line, true);
        text_c(s, cx, y + 0.6, title, sk.legend, 10.5, true);
    } else {
        hardware::draw_legend_line(s, sk.legend.with_alpha(0.85), x0, x1, y, &title.to_uppercase(), 10.0);
    }
}

/// The page itself: everything that does not change while the editor is open.
pub fn layer(s: &mut Scene, page: Page) {
    let sk = Skin::of(page.digital());
    let digital = sk.digital;
    s.fill_rect(Rect::new(0.0, 0.0, W, H), c(if digital { 0xff06_0d16 } else { 0xff0b_0b0c }), c(if digital { 0xff06_0d16 } else { 0xff0b_0b0c }), true);
    let bar = c(if digital { 0xff0a_1420 } else { 0xff0a_0b0c });
    s.fill_rect(Rect::new(0.0, 0.0, W, TOP), bar, bar, true);
    text_l(s, 16.0, 20.0, "TUBE CHANNEL", sk.legend, 20.0, true);
    text_l(s, 200.0, 22.0, "OpenGrid", sk.legend_dim, 10.0, false);

    if page.system() {
        for r in [POWER_P, OS_P, UNIT_P] {
            plate(s, &sk, r);
        }
        let line = |s: &mut Scene, r: R, t: &str| section_line(s, &sk, r.0 + 14.0, r.0 + r.2 - 14.0, r.1 + 14.0, t);
        line(s, POWER_P, "POWER SUPPLY");
        line(s, OS_P, "MODE / OVERSAMPLING");
        line(s, UNIT_P, "UNIT");
        return;
    }
    if !digital {
        let face = Rect::new(EAR, TOP, W - 2.0 * EAR, H - TOP);
        hardware::draw_brushed_panel(s, face, c(0xff2a_2b2e), c(0xff11_1213), 0);
        hardware::draw_rack_ear(s, Rect::new(0.0, TOP, EAR, H - TOP), true);
        hardware::draw_rack_ear(s, Rect::new(W - EAR, TOP, EAR, H - TOP), false);
        for (x, y) in [(EAR + 10.0, TOP + 10.0), (W - EAR - 10.0, TOP + 10.0), (EAR + 10.0, H - 10.0), (W - EAR - 10.0, H - 10.0)] {
            hardware::draw_screw(s, x, y);
        }
        text_c(s, W * 0.5, TOP + 5.5, "TUBE MICROPHONE PREAMPLIFIER  -  OPTICAL COMPRESSOR  -  PARAMETRIC EQUALIZER", sk.legend.with_alpha(0.8), 7.0, true);
        section_line(s, &sk, 52.0, 330.0, 137.0, "Mic Preamp");
        section_line(s, &sk, 342.0, 568.0, 137.0, "Compressor");
        section_line(s, &sk, 582.0, 930.0, 137.0, "Equalizer");
        text_c(s, 985.0 + 77.5, 179.0, "GAIN REDUCTION", sk.legend.with_alpha(0.8), 8.0, true);
        text_c(s, 985.0 + 77.5, 209.0, "PREAMP TUBE WARMTH", sk.legend.with_alpha(0.8), 8.0, true);
        text_l(s, EAR + 20.0, H - 26.0, "TUBE CHANNEL", sk.legend, 24.0, true);
        text_l(s, EAR + 22.0, H - 11.0, "OPENGRID", sk.legend_dim, 9.0, true);
        let (vx, vy, vw, vh) = VU_BOUNDS;
        let bezel = Rect::new(vx - 5.0, vy - 5.0, vw + 10.0, vh + 10.0);
        s.fill_round(bezel, 6.0, c(0xff05_0505), c(0xff05_0505), true);
        s.stroke_round(bezel, 6.0, c(0x33ff_ffff), 1.0);
        return;
    }
    plate(s, &sk, (8.0, TOP + 6.0, W - 16.0, H - TOP - 12.0));
    for x in [329.0, 659.0, 1029.0] {
        let line = sk.line.with_alpha(0.35);
        s.fill_rect(Rect::new(x, TOP + 24.0, 1.2, H - TOP - 44.0), line, line, true);
    }
    section_line(s, &sk, 22.0, 322.0, 58.0, "INPUT");
    section_line(s, &sk, 336.0, 652.0, 58.0, "COMPRESSOR");
    section_line(s, &sk, 666.0, 1022.0, 58.0, "EQUALIZER");
    section_line(s, &sk, 1036.0, 1178.0, 58.0, "OUTPUT");
    text_l(s, 142.0, 190.0, "TUBE WARMTH", sk.legend_dim, 8.5, true);
    text_l(s, 488.0, 190.0, "GAIN REDUCTION", sk.legend_dim, 8.5, true);
}

// ---- widgets --------------------------------------------------------------------------------------------------

/// The rectangle a widget's drawing can change: itself and its glow.
pub fn reach(w: &Widget) -> R {
    let m = match w.kind {
        Kind::Knob { .. } => 8.0,
        Kind::Clip { .. } | Kind::Led { .. } => 6.0,
        Kind::Backdrop => 0.0,
        _ => 3.0,
    };
    (w.rect.0 - m, w.rect.1 - m, w.rect.2 + 2.0 * m, w.rect.3 + 2.0 * m)
}

fn hash_f(h: &mut impl Hasher, v: f32) {
    v.to_bits().hash(h);
}

/// A number that changes when the widget's look does.
pub fn signature(w: &Widget, i: usize, l: &Looks) -> u64 {
    let mut h = std::collections::hash_map::DefaultHasher::new();
    i.hash(&mut h);
    l.page.hash(&mut h);
    l.hot.filter(|h| h.0 == i).hash(&mut h);
    (l.down == Some(i)).hash(&mut h);
    for p in w.params() {
        hash_f(&mut h, l.values[p]);
    }
    let m = l.meters;
    match w.kind {
        Kind::Button { action, .. } => {
            action.hash(&mut h);
            match action {
                Action::Presets | Action::Program(_) => (l.program, l.program_index).hash(&mut h),
                Action::Ab => l.ab_is_b.hash(&mut h),
                Action::Channel(_) => l.channel_b.hash(&mut h),
                Action::Bounce | Action::BounceChoice(_) => {
                    l.offline_active.hash(&mut h);
                    hash_f(&mut h, l.values[idx::OFFLINE_BOUNCE]);
                    hash_f(&mut h, l.values[idx::OFFLINE_BOUNCE_ENABLED]);
                }
                _ => {}
            }
        }
        Kind::Bank(Bank::VuSource) => l.vu_source.hash(&mut h),
        Kind::Bank(Bank::Oversampling) => (l.sample_rate as u32, l.values[idx::ECO].to_bits(), l.values[idx::QUALITY].to_bits()).hash(&mut h),
        Kind::Vu => {
            hash_f(&mut h, (m.needle * 400.0).round());
            hash_f(&mut h, (m.display_db * 10.0).round());
            for b in &m.bars {
                hash_f(&mut h, (b.db * 2.0).round());
                hash_f(&mut h, (b.held_db * 2.0).round());
            }
            l.vu_source.hash(&mut h);
            m.channels.hash(&mut h);
        }
        Kind::Ladder(Ladder::GainReduction) => hash_f(&mut h, (m.gain_reduction * 24.0).round()),
        Kind::Ladder(Ladder::Warmth) => hash_f(&mut h, (m.warmth * 20.0).round()),
        Kind::Clip { .. } => hash_f(&mut h, (m.clip * 20.0).round()),
        Kind::Power => {
            hash_f(&mut h, m.flow_phase);
            if let Some(p) = &m.power {
                hash_f(&mut h, p.p25);
                hash_f(&mut h, p.tube_p);
                p.scope_write.hash(&mut h);
            }
            hash_f(&mut h, l.values[idx::PSU_MODEL]);
        }
        Kind::Comp { .. } | Kind::Eq { .. } => {
            for p in 0..COUNT {
                hash_f(&mut h, l.values[p]);
            }
            l.ch.hash(&mut h);
            hash_f(&mut h, (m.comp_gr * 10.0).round());
            if matches!(w.kind, Kind::Eq { .. }) {
                l.spectrum_frame.hash(&mut h);
            }
        }
        Kind::Status => l.status.hash(&mut h),
        _ => {}
    }
    h.finish()
}

pub fn widget(s: &mut Scene, w: &Widget, i: usize, l: &Looks) {
    let sk = &l.skin;
    let hot = l.hot.is_some_and(|h| h.0 == i);
    let down = l.down == Some(i);
    match w.kind {
        Kind::Knob { param, title, legends, body, value, click } => knob(s, w, l, param, title, legends, body, value, click),
        Kind::Slider { param, title, title_w, value_w } => slider(s, w.rect, sk, l.values, param, title, title_w, value_w),
        Kind::Led { param, on, off, led } => led_button(s, w.rect, sk, l.values[param] > 0.5, on, off, led.map(Color).unwrap_or(sk.led), down, hot),
        Kind::Button { action, text } => button(s, w.rect, l, action, text, hot, down),
        Kind::Bank(bank) => key_bank(s, w.rect, l, bank),
        Kind::Vu => vu(s, w.rect, l),
        Kind::Ladder(kind) => ladder(s, w.rect, sk, kind, if kind == Ladder::Warmth { l.meters.warmth } else { l.meters.gain_reduction }),
        Kind::Clip { .. } => {
            let r = rect(w.rect);
            meters::draw_lamp(s, Point::new(r.x + r.w * 0.5, r.y + r.h * 0.38), r.w.min(r.h) * 0.22, c(0xffe5_382a), l.meters.clip > 0.05);
            text_c(s, r.x + r.w * 0.5, r.bottom() - r.h * 0.16, "CLIP", c(0xff8a_96a0), 9.0, true);
        }
        Kind::Power => power(s, w.rect, l),
        Kind::Comp { .. } => comp(s, w.rect, l),
        Kind::Eq { .. } => eq(s, w.rect, l),
        Kind::Status => {
            text_l(s, w.rect.0, w.rect.1 + 8.0, &l.status.0, sk.legend, 11.0, true);
            text_l(s, w.rect.0, w.rect.1 + 23.0, &l.status.1, sk.legend_dim, 9.5, false);
        }
        Kind::Backdrop => s.fill_rect(rect(w.rect), c(0x8000_0000), c(0x8000_0000), true),
    }
}

#[allow(clippy::too_many_arguments)]
fn knob(s: &mut Scene, w: &Widget, l: &Looks, param: usize, title: &str, legends: &[&str], d: f32, value: bool, click: Option<usize>) {
    let sk = &l.skin;
    let popped = click.is_some_and(|p| l.values[p] > 0.5);
    let (cx, cy, _) = knob_circle(w);
    let ring = d * 0.5 + 11.0;
    text_c(s, cx, w.rect.1 + 6.0, if popped { "LIMITER" } else { title }, if popped { sk.accent } else { sk.legend }, 9.5, true);
    let dir = |a: f32| (a.sin(), -a.cos());
    let pi = std::f32::consts::PI;
    if !sk.digital {
        // printed tick ring, longer at the legend positions
        let n = 11;
        for i in 0..n {
            let a = (-0.8 + 1.6 * i as f32 / (n - 1) as f32) * pi;
            let major = if legends.len() > 1 { (i * (legends.len() - 1)).is_multiple_of(n - 1) } else { i.is_multiple_of(5) };
            let r0 = d * 0.5 + 4.0;
            let r1 = r0 + if major { 4.5 } else { 2.5 };
            let (dx, dy) = dir(a);
            s.line(Point::new(cx + dx * r0, cy + dy * r0), Point::new(cx + dx * r1, cy + dy * r1), sk.legend.with_alpha(if major { 0.8 } else { 0.5 }), if major { 1.1 } else { 0.8 });
        }
    }
    let item = Item { style: if sk.digital { "Neo" } else { "Rck" }.into(), colour: if sk.digital { "#39d0ff" } else { "#f2f2f0" }.into(), ..Item::default() };
    let norm = PARAMS[param].to_normalized(l.values[param]);
    if popped {
        // the knob is pulled up: a softer shadow underneath, the knob higher and larger
        for i in (1..=5).rev() {
            let e = d * 0.5 + i as f32 * 1.6;
            s.fill_ellipse_solid(Rect::new(cx - e, cy - e + 7.0, e * 2.0, e * 2.0), c(0x1600_0000));
        }
    }
    let body = if popped { Rect::new(cx - d * 0.5 - 2.0, cy - d * 0.5 - 5.0, d + 4.0, d + 4.0) } else { Rect::new(cx - d * 0.5, cy - d * 0.5, d, d) };
    draw::knob(s, body, if popped { 0.5 } else { f64::from(norm) }, &item, true, false);
    for (i, legend) in legends.iter().enumerate() {
        let t = if legends.len() > 1 { i as f32 / (legends.len() - 1) as f32 } else { 0.5 };
        let a = (-0.8 + 1.6 * t) * pi;
        let (dx, dy) = dir(a);
        text_c(s, cx + dx * ring, cy + dy * ring + if a * a > 0.8 { 2.0 } else { 0.0 }, legend, sk.legend.with_alpha(0.85), 8.0, true);
    }
    if value {
        text_c(s, cx, w.rect.1 + w.rect.3 - 6.0, &PARAMS[param].format(l.values[param]), sk.accent.with_alpha(0.9), 9.0, sk.digital);
    }
}

/// The slider's track: (x0, x1) in editor units.
pub fn slider_track(r: R, title_w: f32, value_w: f32) -> (f32, f32) {
    (r.0 + title_w + 2.0, r.0 + r.2 - value_w - 2.0)
}

#[allow(clippy::too_many_arguments)]
fn slider(s: &mut Scene, r: R, sk: &Skin, values: &[f32; COUNT], param: usize, title: &str, title_w: f32, value_w: f32) {
    let cy = r.1 + r.3 * 0.5;
    text_l(s, r.0, cy, title, sk.legend, 9.5, true);
    text_r(s, r.0 + r.2, cy, &PARAMS[param].format(values[param]), sk.accent, 9.5, true);
    let (x0, x1) = slider_track(r, title_w, value_w);
    let norm = PARAMS[param].to_normalized(values[param]);
    let track = Rect::new(x0, cy - 3.0, x1 - x0, 6.0);
    s.fill_round(track, 3.0, c(0xff05_0a12), c(0xff05_0a12), true);
    let x = x0 + (x1 - x0) * norm;
    s.fill_round(Rect::new(x0, cy - 3.0, (x - x0).max(6.0), 6.0), 3.0, sk.accent.darker(0.5), sk.accent, false);
    s.stroke_round(track, 3.0, sk.line.with_alpha(0.6), 0.8);
    s.fill_ellipse_solid(Rect::new(x - 9.0, cy - 9.0, 18.0, 18.0), sk.accent.with_alpha(0.28));
    s.fill_ellipse_solid(Rect::new(x - 5.0, cy - 5.0, 10.0, 10.0), c(0xffff_ffff));
    s.stroke_ellipse(Rect::new(x - 5.0, cy - 5.0, 10.0, 10.0), sk.accent, 1.4);
}

#[allow(clippy::too_many_arguments)]
fn led_button(s: &mut Scene, r: R, sk: &Skin, on: bool, top: &str, bottom: &str, led: Color, down: bool, hot: bool) {
    let cx = r.0 + r.2 * 0.5;
    let key = Rect::new(cx - 11.0, r.1 + 11.0, 22.0, 12.0);
    if sk.digital {
        let fill = if on { led.with_alpha(0.9) } else { sk.extra_top };
        s.fill_round(key, 2.5, fill, fill, true);
        s.stroke_round(key, 2.5, if on { led } else { sk.line }, 1.0);
    } else {
        hardware::draw_led_key(s, key, led, on, down);
    }
    let dim = sk.legend_dim.with_alpha(0.7);
    text_c(s, cx, r.1 + 5.0, top, if bottom.is_empty() || on { sk.legend } else { dim }, 8.0, true);
    if !bottom.is_empty() {
        text_c(s, cx, r.1 + 29.5, bottom, if on { dim } else { sk.legend }, 8.0, true);
    }
    if hot && !down {
        s.fill_rect(rect(r), Color(0xffff_ffff).with_alpha(0.05), Color(0xffff_ffff).with_alpha(0.05), true);
    }
}

fn button(s: &mut Scene, r: R, l: &Looks, action: Action, text: &str, hot: bool, down: bool) {
    let sk = &l.skin;
    let bounce = l.values[idx::OFFLINE_BOUNCE].round() as usize;
    let bounce_on = l.values[idx::OFFLINE_BOUNCE_ENABLED] > 0.5;
    let (lit, label): (bool, String) = match action {
        Action::Presets => (false, l.program.to_string()),
        Action::Ab => (l.ab_is_b, if l.ab_is_b { "B" } else { "A" }.into()),
        Action::System => (l.page.system(), text.into()),
        Action::Skin => (sk.digital, if sk.digital { "DIGITAL" } else { "ANALOGUE" }.into()),
        Action::Channel(b) => (b == l.channel_b, text.into()),
        Action::Bounce => (l.offline_active, text.into()),
        Action::Program(p) => (p == l.program_index, text.into()),
        Action::BounceChoice(choice) => (bounce_on && choice == bounce, text.into()),
        _ => (false, text.into()),
    };
    let r = rect(r).reduced(1.0, 1.0);
    key_fill(s, if down { Rect::new(r.x, r.y + 1.0, r.w, r.h) } else { r }, sk, lit, 2.5);
    if hot {
        s.fill_round(r, 2.5, Color(0xffff_ffff).with_alpha(0.06), Color(0xffff_ffff).with_alpha(0.06), true);
    }
    text_c(s, r.x + r.w * 0.5, r.y + r.h * 0.5, &label, if lit { c(0xff00_0000) } else { sk.legend }, 10.0, true);
}

/// The selected key of a bank, and how many keys.
pub fn bank_selected(l: &Looks, bank: Bank) -> Option<usize> {
    let v = l.values;
    match bank {
        Bank::Quality => Some(if v[idx::ECO] > 0.5 { 0 } else if v[idx::QUALITY].round() == 0.0 { 1 } else { 2 }),
        Bank::Oversampling => {
            let os = v[idx::OVERSAMPLING].round() as i32;
            if os == 0 {
                // legacy: the factor the engine picks for the quality and the rate (`stagesFor`)
                let nominal = if v[idx::ECO] > 0.5 { 0 } else if v[idx::QUALITY].round() as i32 == 2 { 2 } else { 1 };
                let shift = (f64::from(l.sample_rate.max(1.0)) / 48_000.0).log2().round().max(0.0) as i32;
                usize::try_from((nominal - shift).max(0) - 1).ok()
            } else {
                usize::try_from(os - 2).ok()
            }
        }
        Bank::Mains => Some(v[idx::MAINS].round() as usize),
        Bank::VuSource => Some(l.vu_source),
    }
}

/// The key of a bank at x.
pub fn bank_key(r: R, bank: Bank, x: f32) -> usize {
    let n = bank.names().len();
    (((x - r.0) / r.2 * n as f32).floor().max(0.0) as usize).min(n - 1)
}

fn key_bank(s: &mut Scene, r: R, l: &Looks, bank: Bank) {
    let sk = &l.skin;
    let names = bank.names();
    let selected = bank_selected(l, bank);
    let w = r.2 / names.len() as f32;
    let size = if bank == Bank::VuSource { 9.5 } else { 11.0 };
    for (i, name) in names.iter().enumerate() {
        let key = Rect::new(r.0 + w * i as f32, r.1, w, r.3).reduced(2.0, 2.0);
        let lit = selected == Some(i);
        key_fill(s, key, sk, lit, 2.5);
        let ink = if lit { c(0xff00_0000) } else { sk.legend };
        if bank == Bank::Oversampling && i == 2 {
            derp_face(s, key.reduced(if key.w < 45.0 { 3.0 } else { 8.0 }, if key.w < 45.0 { 3.0 } else { 8.0 }), ink);
        } else {
            text_c(s, key.x + key.w * 0.5, key.y + key.h * 0.5, name, ink, size, true);
        }
    }
}

/// The \( ᐖ)/ face (`drawDerpFace`): arms, brackets as curves, a play triangle with a stem, two dots.
fn derp_face(s: &mut Scene, r: Rect, ink: Color) {
    let k = (r.w / 334.0).min(r.h / 182.0);
    let (ox, oy) = (r.x + r.w * 0.5 - 167.0 * k, r.y + r.h * 0.5 - 105.0 * k);
    let p = |x: f32, y: f32| Point::new(ox + x * k, oy + y * k);
    let w = (12.0 * k).max(1.2);
    let quad = |a: (f32, f32), b: (f32, f32), e: (f32, f32)| -> Vec<Point> {
        (0..=12)
            .map(|i| {
                let t = i as f32 / 12.0;
                let u = 1.0 - t;
                p(u * u * a.0 + 2.0 * u * t * b.0 + t * t * e.0, u * u * a.1 + 2.0 * u * t * b.1 + t * t * e.1)
            })
            .collect()
    };
    s.line(p(12.0, 62.0), p(44.0, 190.0), ink, w);
    s.stroke_poly(quad((94.0, 62.0), (58.0, 126.0), (94.0, 190.0)), ink, w, false);
    s.stroke_poly(vec![p(147.0, 65.0), p(228.0, 112.0), p(147.0, 165.0)], ink, w, true);
    s.line(p(147.0, 65.0), p(147.0, 42.0), ink, w);
    s.stroke_poly(quad((250.0, 62.0), (286.0, 126.0), (250.0, 190.0)), ink, w, false);
    s.line(p(290.0, 190.0), p(322.0, 62.0), ink, w);
    for (x, y) in [(147.0, 25.0), (207.0, 56.0)] {
        let d = p(x, y);
        let rr = 6.0 * k + w * 0.15;
        s.fill_ellipse_solid(Rect::new(d.x - rr, d.y - rr, rr * 2.0, rr * 2.0), ink);
    }
}

fn vu(s: &mut Scene, r: R, l: &Looks) {
    let sk = &l.skin;
    let m = l.meters;
    if !sk.digital {
        meters::draw_vu_meter(s, rect(r), m.needle, VuFace::Black);
        return;
    }
    let outer = rect(r);
    s.fill_round(outer, 3.0, c(0xff05_0a12), c(0xff05_0a12), true);
    let inner = Rect::new(outer.x + 6.0, outer.y + 4.0, outer.w - 12.0, outer.h - 8.0);
    let out_src = l.vu_source == 3;
    let (floor, ceiling) = if out_src { (-60.0, 0.0) } else { (-30.0, 3.0) };
    let pos = |db: f32| ((db - floor) / (ceiling - floor)).clamp(0.0, 1.0);
    for ch in 0..m.channels.max(1) {
        let row = Rect::new(inner.x, inner.y + ch as f32 * 6.0, inner.w, 4.0);
        let (level, held) = (pos(m.bars[ch].db), pos(m.bars[ch].held_db));
        let count = 24;
        let step = row.w / count as f32;
        for i in 0..count {
            let f = (i + 1) as f32 / count as f32;
            let colour = if f > 0.95 { c(0xffe5_382a) } else if f > 0.8 { c(0xfff0_b323) } else { sk.accent };
            let colour = if level >= f { colour } else { colour.with_alpha(0.13) };
            s.fill_rect(Rect::new(row.x + i as f32 * step, row.y, step - 1.0, row.h), colour, colour, true);
        }
        s.fill_rect(Rect::new(row.x + held * (row.w - 1.0), row.y, 1.0, row.h), sk.legend, sk.legend, true);
    }
    let y = inner.bottom() - 7.0;
    text_l(s, inner.x, y, &m.readout(), if m.display_db >= 0.0 { c(0xffe5_382a) } else { sk.accent }, 11.0, true);
    text_r(s, inner.right(), y, if out_src { "dBFS" } else { "VU" }, sk.legend_dim, 9.0, true);
}

fn ladder(s: &mut Scene, r: R, sk: &Skin, kind: Ladder, shown: f32) {
    let segs = if kind == Ladder::Warmth { 10 } else { 12 };
    let outer = rect(r);
    s.fill_round(outer, 2.0, c(0xff08_090a), c(0xff08_090a), true);
    let inner = outer.reduced(2.0, 2.0);
    let step = inner.w / segs as f32;
    for i in 0..segs {
        let lit = (i as f32) < shown * segs as f32 - 0.001;
        let colour = if kind == Ladder::Warmth {
            c(if i >= 8 { 0xffe5_382a } else if i >= 4 { 0xfff0_b323 } else { 0xff3c_cf5a })
        } else {
            c(0xfff0_b323).mix(c(0xffe5_382a), i as f32 / segs as f32)
        };
        let cell = Rect::new(inner.x + i as f32 * step, inner.y, step, inner.h).reduced(1.5, 1.5);
        let fill = if lit { colour } else { colour.darker(0.85).with_alpha(0.55) };
        s.fill_round(cell, 1.5, fill, fill, true);
        if lit && !sk.digital {
            s.fill_round(cell.expanded(1.5), 2.0, colour.with_alpha(0.25), colour.with_alpha(0.25), true);
        }
    }
}

// ---- the power supply (PowerPanel) -------------------------------------------------------------------------------

fn power(s: &mut Scene, r: R, l: &Looks) {
    let sk = &l.skin;
    let m = l.meters;
    let Some(t) = m.power.as_deref() else { return };
    let (ink, mid) = (sk.legend, sk.legend_dim);
    let lit = if sk.digital { sk.accent } else { c(0xffff_c65c) };
    let area = rect(r);

    // flow diagram
    let flow = Rect::new(area.x + 4.0, area.y + 6.0, area.w - 8.0, 54.0);
    let names = ["MAINS", "TRANSFORMER", "RECTIFIERS", "RESERVOIRS", "MULTIPLIERS", "REGULATORS"];
    let subs = ["", "22-0-22 V", "2 x 2 diodes", "2 x 1000 uF", "+75 / +150 V", "+/-15 / +45 V"];
    let n = 6;
    let bw = (flow.w - (n - 1) as f32 * 14.0) / n as f32;
    let boxes: Vec<Rect> = (0..n).map(|i| Rect::new(flow.x + i as f32 * (bw + 14.0), flow.y + 6.0, bw, flow.h - 12.0)).collect();
    let load = (t.load_p15 + t.load_tube).clamp(0.0, 0.5);
    for i in 0..n - 1 {
        let (a, b, y) = (boxes[i].right(), boxes[i + 1].x, boxes[i].y + boxes[i].h * 0.5);
        s.line(Point::new(a, y), Point::new(b, y), mid.with_alpha(0.6), 1.5);
        for k in 0..3 {
            let ph = (m.flow_phase + k as f32 / 3.0 + i as f32 * 0.17).rem_euclid(1.0);
            let dot = lit.with_alpha(0.55 + 0.4 * (load * 4.0).clamp(0.0, 1.0));
            s.fill_ellipse_solid(Rect::new(a + (b - a) * ph - 2.0, y - 2.0, 4.0, 4.0), dot);
        }
    }
    for (i, b) in boxes.iter().enumerate() {
        s.fill_round(*b, 3.0, sk.panel_bottom.with_alpha(0.85), sk.panel_bottom.with_alpha(0.85), true);
        s.stroke_round(*b, 3.0, mid.with_alpha(0.7), 1.0);
        text_c(s, b.x + b.w * 0.5, b.y + b.h * 0.25, names[i], ink, 8.5, true);
        text_c(s, b.x + b.w * 0.5, b.y + b.h * 0.75, subs[i], mid, 8.0, false);
    }
    {
        // mains phase marker on the first box
        let b = boxes[0];
        let bx = Rect::new(b.x + 6.0, b.bottom() - 13.0, b.w - 12.0, 10.0);
        let sine: Vec<Point> = (0..=bx.w as usize).map(|x| Point::new(bx.x + x as f32, bx.y + bx.h * 0.5 - (x as f32 / bx.w * std::f32::consts::TAU).sin() * bx.h * 0.45)).collect();
        s.stroke_poly(sine, mid.with_alpha(0.7), 1.0, false);
        let ph = t.mains_phase.rem_euclid(std::f32::consts::TAU);
        let (mx, my) = (bx.x + ph / std::f32::consts::TAU * bx.w, bx.y + bx.h * 0.5 - ph.sin() * bx.h * 0.45);
        s.fill_ellipse_solid(Rect::new(mx - 2.5, my - 2.5, 5.0, 5.0), lit);
    }

    // rails
    let lower = Rect::new(area.x + 4.0, area.y + 68.0, area.w - 8.0, area.h - 70.0);
    let bars = Rect::new(lower.x, lower.y, lower.w * 0.52, lower.h);
    let rails = [("+25 raw", t.p25, 26.0), ("+15 reg", t.p15, 15.0), ("-15 reg", -t.n15, 15.0), ("+45 reg", t.p45, 45.8), ("+75", t.p75, 74.0), ("plate", t.tube_p, 165.0)];
    let row_h = bars.h / 6.0;
    for (i, (name, value, nominal)) in rails.iter().enumerate() {
        let row = Rect::new(bars.x, bars.y + i as f32 * row_h + 2.0, bars.w, row_h - 4.0);
        let cy = row.y + row.h * 0.5;
        text_l(s, row.x, cy, name, mid, 9.5, true);
        let track = Rect::new(row.x + 52.0, row.y, row.w - 52.0 - 86.0, row.h);
        s.fill_round(track, 2.0, c(0xff06_080a), c(0xff06_080a), true);
        let frac = (value / (nominal * 1.25)).clamp(0.0, 1.0);
        let fill = Rect::new(track.x, track.y, track.w * frac, track.h).reduced(1.0, 1.0);
        s.fill_round(fill, 1.5, lit.with_alpha(0.85), lit.with_alpha(0.85), true);
        let nom_x = track.x + track.w / 1.25;
        s.line(Point::new(nom_x, track.y), Point::new(nom_x, track.bottom()), ink.with_alpha(0.7), 1.0);
        let sag = value - m.rail_avg[i];
        let right = row.right();
        text_r(s, right, row.y + row.h * 0.28, &format!("{value:.*} V", if i < 3 { 2 } else { 1 }), ink, 9.5, false);
        text_r(s, right, row.y + row.h * 0.78, &format!("{}{:.0} mV", if sag >= 0.0 { "+" } else { "" }, sag * 1000.0), if sag.abs() > if i == 5 { 0.6 } else { 0.15 } { c(0xffff_8a5c) } else { mid }, 8.5, false);
    }

    // scope: ripple history of the raw rail and the plate supply
    let scope_all = Rect::new(lower.x + lower.w * 0.52 + 6.0, lower.y + 2.0, lower.w * 0.48 - 12.0, lower.h - 4.0);
    let scope = Rect::new(scope_all.x, scope_all.y, scope_all.w, scope_all.h - 34.0);
    let heaters = Rect::new(scope_all.x, scope.bottom(), scope_all.w, 34.0);
    s.fill_round(scope, 3.0, c(0xff05_0708), c(0xff05_0708), true);
    for i in 1..4 {
        let y = scope.y + scope.h * i as f32 / 4.0;
        s.line(Point::new(scope.x, y), Point::new(scope.right(), y), mid.with_alpha(0.25), 1.0);
    }
    for i in 1..8 {
        let x = scope.x + scope.w * i as f32 / 8.0;
        s.line(Point::new(x, scope.y), Point::new(x, scope.bottom()), mid.with_alpha(0.25), 1.0);
    }
    let trace = |s: &mut Scene, data: &[f32; SCOPE], span: f32, colour: Color| {
        let mean = data.iter().sum::<f32>() / SCOPE as f32;
        let w = t.scope_write.max(0) as usize;
        let pts: Vec<Point> = (0..SCOPE)
            .map(|i| {
                let v = data[(w + i) % SCOPE] - mean;
                Point::new(scope.x + scope.w * i as f32 / (SCOPE - 1) as f32, scope.y + scope.h * 0.5 - (v / span).clamp(-0.5, 0.5) * scope.h * 0.9)
            })
            .collect();
        s.stroke_poly(pts, colour.with_alpha(0.9), 1.3, false);
    };
    trace(s, &t.scope25, 4.0, lit);
    trace(s, &t.scope150, 3.0, c(0xff6f_d4ff));
    text_l(s, scope.x + 4.0, scope.y + 9.0, "rail ripple, 32 ms", mid, 8.5, false);
    text_l(s, scope.x + 4.0, scope.y + 20.0, "+25 V", lit, 8.5, false);
    text_l(s, scope.x + 50.0, scope.y + 20.0, "plate", c(0xff6f_d4ff), 8.5, false);

    // heaters
    let heat = (t.heater_current / 0.135).clamp(0.0, 1.2);
    for k in 0..2 {
        let tb = Rect::new(heaters.x + k as f32 * heaters.w * 0.5 + 4.0, heaters.y + 2.0, heaters.w * 0.5 - 8.0, heaters.h - 4.0);
        let glow = heat * (0.78 + 0.22 * (m.flow_phase * std::f32::consts::TAU * 0.5 + k as f32).sin());
        let cx = tb.x + 16.0;
        s.fill_round(Rect::new(cx - 9.0, tb.y + 1.0, 18.0, tb.h - 2.0), 8.0, c(0xff11_161a), c(0xff11_161a), true);
        let lamp = c(0xffff_9d3c).with_alpha(0.20 + 0.65 * glow.min(1.0));
        s.fill_ellipse_solid(Rect::new(cx - 5.0, tb.y + tb.h * 0.5 - 6.0, 10.0, 12.0), lamp);
        if !sk.digital {
            s.fill_ellipse_solid(Rect::new(cx - 16.0, tb.y + tb.h * 0.5 - 14.0, 32.0, 28.0), c(0xffff_9d3c).with_alpha((0.12 * glow).min(1.0)));
        }
        text_l(s, tb.x + 34.0, tb.y + 5.5, if k == 0 { "V1  12AX7" } else { "V3  12AT7" }, mid, 8.5, false);
        text_l(s, tb.x + 34.0, tb.bottom() - 5.5, &format!("{:.0} mA heater", t.heater_current * 1000.0), ink.with_alpha(0.8), 8.5, false);
    }
    if l.values[idx::PSU_MODEL] < 0.5 {
        s.fill_round(area.reduced(2.0, 2.0), 4.0, c(0xd000_0000), c(0xd000_0000), true);
        text_c(s, area.x + area.w * 0.5, area.y + area.h * 0.5, "Supply model off: the rails stay at their settled values (less CPU)", mid, 12.0, true);
    }
}

// ---- the digital displays ----------------------------------------------------------------------------------------

fn display_frame(s: &mut Scene, r: Rect, sk: &Skin) {
    s.fill_round(r, 4.0, c(0xff05_0a12), c(0xff05_0a12), true);
    s.stroke_round(r.reduced(0.5, 0.5), 4.0, sk.line.with_alpha(0.7), 1.0);
}

fn handle_dot(s: &mut Scene, x: f32, y: f32, colour: Color, big: f32) {
    s.fill_ellipse_solid(Rect::new(x - big, y - big, big * 2.0, big * 2.0), colour.with_alpha(0.28));
    s.fill_ellipse_solid(Rect::new(x - big * 0.55, y - big * 0.55, big * 1.1, big * 1.1), colour);
    s.fill_ellipse_solid(Rect::new(x - big * 0.22, y - big * 0.22, big * 0.44, big * 0.44), c(0xff05_0a12));
}

fn eq(s: &mut Scene, r: R, l: &Looks) {
    let sk = &l.skin;
    let outer = rect(r);
    display_frame(s, outer, sk);
    let plot = EqPlot::of(r);
    let a = rect(plot.0);
    s.clip_round(outer.reduced(1.0, 1.0), 4.0);
    for f in [50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0] {
        let x = plot.x(f).floor();
        let major = f == 100.0 || f == 1000.0 || f == 10000.0;
        s.line(Point::new(x, a.y), Point::new(x, a.bottom()), sk.line.with_alpha(if major { 0.45 } else { 0.2 }), 1.0);
        if major {
            text_c(s, x, a.bottom() - 6.0, if f >= 1000.0 { if f == 1000.0 { "1k" } else { "10k" } } else { "100" }, sk.legend_dim, 8.0, false);
        }
    }
    for db in [-12.0, -6.0, 0.0, 6.0, 12.0] {
        let y = plot.y(db).floor();
        s.line(Point::new(a.x, y), Point::new(a.right(), y), sk.line.with_alpha(if db == 0.0 { 0.55 } else { 0.2 }), 1.0);
        if db != 0.0 {
            text_l(s, a.x + 1.0, y, &format!("{}{}", if db > 0.0 { "+" } else { "" }, db as i32), sk.legend_dim, 8.0, false);
        }
    }
    // spectrum, filled
    let w = a.w as usize;
    let mut spec = vec![Point::new(a.x, a.bottom())];
    for px in 0..=w {
        let x = a.x + px as f32;
        let m = l.spectrum.db(plot.f(x - 0.5), plot.f(x + 0.5));
        let t = ((m + 100.0) / 90.0).clamp(0.0, 1.0);
        spec.push(Point::new(x, a.bottom() - t * a.h * 0.9));
    }
    spec.push(Point::new(a.right(), a.bottom()));
    s.fill_poly(spec.clone(), sk.accent.with_alpha(0.42), sk.accent.with_alpha(0.03));
    s.stroke_poly(spec[1..spec.len() - 1].to_vec(), sk.accent.with_alpha(0.65), 1.0, false);
    // EQ curve
    let eq_on = l.values[EQ + l.ch] > 0.5;
    let bands = Bands::of(l.values, l.ch);
    let curve: Vec<Point> = (0..=w).step_by(2).map(|px| Point::new(a.x + px as f32, plot.y(if eq_on { bands.response_db(plot.f(a.x + px as f32)) } else { 0.0 }))).collect();
    s.stroke_poly(curve.clone(), Color(0xffff_ffff).with_alpha(if eq_on { 0.18 } else { 0.06 }), 5.0, false);
    s.stroke_poly(curve, Color(0xffff_ffff).with_alpha(if eq_on { 1.0 } else { 0.45 }), 1.8, false);
    for i in 0..4 {
        let (f, g) = bands.handle(i);
        let (x, y) = (plot.x(f), plot.y(g));
        let colour = c(BAND_COLOURS[i]);
        handle_dot(s, x, y, colour, 11.0);
        let dy = if i == 1 || i == 3 { 14.0 } else { -14.0 };
        text_c(s, x, y + dy, BAND_NAMES[i], colour, 8.5, true);
    }
    s.pop_clip();
}

fn comp(s: &mut Scene, r: R, l: &Looks) {
    let sk = &l.skin;
    let outer = rect(r);
    display_frame(s, outer, sk);
    let plot = CompPlot::of(r);
    let a = rect(plot.0);
    let curve_of = CompCurve::of(l.values, l.ch);
    let mut db = COMP_LO;
    while db <= COMP_HI + 0.01 {
        let (x, y) = (plot.x(db).floor(), plot.y(db).floor());
        let grid = sk.line.with_alpha(if db == 0.0 { 0.5 } else { 0.2 });
        s.line(Point::new(x, a.y), Point::new(x, a.bottom()), grid, 1.0);
        s.line(Point::new(a.x, y), Point::new(a.right(), y), grid, 1.0);
        text_c(s, x, a.bottom() + 6.0, &format!("{}", db as i32), sk.legend_dim, 8.0, false);
        text_r(s, outer.x + 20.0, y, &format!("{}", db as i32), sk.legend_dim, 8.0, false);
        db += 10.0;
    }
    text_r(s, a.right(), a.bottom() - 7.0, "IN dBm", sk.legend_dim, 8.0, false);
    text_l(s, a.x + 2.0, a.y + 6.0, "OUT", sk.legend_dim, 8.0, false);
    s.clip_round(a.expanded(1.0), 0.0);
    // unity line, dashed
    let mut d = COMP_LO;
    while d < COMP_HI {
        let e = (d + 2.0).min(COMP_HI);
        s.line(Point::new(plot.x(d), plot.y(d)), Point::new(plot.x(e), plot.y(e)), sk.line.with_alpha(0.6), 1.0);
        d += 4.0;
    }
    let pts: Vec<Point> = (0..=120).map(|i| COMP_LO + i as f64 * 0.5).map(|input| Point::new(plot.x(input), plot.y(curve_of.out_db(input)))).collect();
    let mut fill = pts.clone();
    fill.push(Point::new(plot.x(COMP_HI), a.bottom()));
    fill.push(Point::new(plot.x(COMP_LO), a.bottom()));
    s.fill_poly(fill, sk.accent.with_alpha(0.22), sk.accent.with_alpha(0.0));
    s.stroke_poly(pts.clone(), sk.accent.with_alpha(0.3), 5.0, false);
    s.stroke_poly(pts, c(0xffff_ffff), 1.8, false);
    // live gain reduction
    let gr = l.meters.comp_gr;
    s.fill_rect(Rect::new(a.right() - 5.0, a.y, 4.0, a.h * (gr / 24.0).clamp(0.0, 1.0)), c(0xffff_9a2a), c(0xffff_9a2a), true);
    let handles = [(if curve_of.limiter { "CEILING" } else { "THRESH" }, 0xff5a_b4ff), ("RATIO", 0xffff_c04a), ("OUT", 0xff5a_e07a)];
    for (i, (label, colour)) in handles.iter().enumerate().take(curve_of.handles()) {
        let (x, y) = curve_of.handle(&plot, i);
        handle_dot(s, x, y, c(*colour), 10.0);
        text_c(s, x, y + if i == 1 { -13.0 } else { 12.0 }, label, c(*colour), 8.0, true);
    }
    s.pop_clip();
    text_r(s, outer.right() - 8.0, outer.y + 9.0, &format!("GR {gr:.1} dB"), sk.accent, 9.5, true);
}
