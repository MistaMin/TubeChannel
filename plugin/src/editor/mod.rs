//! The editor: the analogue front panel, the digital skin and each skin's System page of the JUCE
//! release (Source/Plugin/PluginEditor.cpp, Source/UI), drawn with GoodLookin DRI.
//!
//! Every control is bound to a host parameter, so automation and session restore act on the
//! parameter and the editor only reflects it. The editor is laid out in the release's 1200 x 284
//! units and scaled as a whole into the window (half to twice, same shape). The release's popup
//! menus (factory programs, offline bounce) are lists drawn over the page.

mod graphs;
mod lettering;
mod paint;
mod panel;
mod place;

use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;

use openplugin::core::editor::{AccessAction, AccessNode, AccessRange, ParamMenu, ParamMenuAction, SizeRange, TextEdit, Tooltip};
use openplugin::core::{Editor, EditorHost, EditorState, Frame};
use openplugin::dri::goodlookindri::meters::vu_position_for_db;
use openplugin::dri::goodlookindri::scene::Scene;
use openplugin::dri::goodlookindri::{draw, hardware};
use openplugin::dri::{Area, Control, ControlSet, DamageRect, DamageTracker, DoubleClick, Key, Modifiers, SceneCanvas};

use graphs::{Bands, CompCurve, CompPlot, EqPlot, Spectrum};
pub use panel::Page;
use panel::{Action, Bank, Kind, Overlay, Widget, H, W};
use paint::{Looks, Meters, Skin};

use crate::params::{idx, B, COUNT, PARAMS, PER_CHANNEL};
use crate::shared::Shared;

/// A graph handle being dragged, and the parameters it has opened a gesture on.
struct GraphDrag {
    widget: usize,
    handle: usize,
    open: Vec<usize>,
}

/// A press on a knob that flips a parameter when it is let go without moving.
struct KnobClick {
    param: usize,
    at: (f32, f32),
    moved: bool,
}

pub struct TubeEditor {
    host: EditorHost,
    shared: Option<Arc<Shared>>,
    /// Settings while no engine is bound: skin, System page, meter source.
    local: (u8, bool, u8),
    values: [f32; COUNT],
    page: Page,
    channel_b: bool,
    dual: bool,
    overlay: Overlay,
    size: (u32, u32),
    widgets: Vec<Widget>,
    controls: ControlSet,
    canvas: SceneCanvas,
    damage: DamageTracker,
    signatures: Vec<u64>,
    meters: Meters,
    spectrum: Spectrum,
    status: (String, String),
    hot: Option<(usize, usize)>,
    down: Option<usize>,
    drag: Option<GraphDrag>,
    knob_click: Option<KnobClick>,
    modifiers: Modifiers,
}

#[derive(Clone, Copy)]
struct Parts {
    page: Page,
    ch: usize,
    channel_b: bool,
    hot: Option<(usize, usize)>,
    down: Option<usize>,
    vu_source: usize,
    program: usize,
    ab_is_b: bool,
    offline_active: bool,
    sample_rate: f32,
}

impl Parts {
    fn looks<'a>(self, values: &'a [f32; COUNT], meters: &'a Meters, spectrum: &'a Spectrum, status: &'a (String, String)) -> Looks<'a> {
        Looks {
            values,
            page: self.page,
            skin: Skin::of(self.page.digital()),
            ch: self.ch,
            channel_b: self.channel_b,
            hot: self.hot,
            down: self.down,
            meters,
            spectrum,
            spectrum_frame: spectrum.analyser.frames_analysed(),
            vu_source: self.vu_source,
            program: crate::programs::PROGRAMS.get(self.program).map_or("Init", |p| p.name),
            program_index: self.program,
            ab_is_b: self.ab_is_b,
            offline_active: self.offline_active,
            status,
            sample_rate: self.sample_rate,
        }
    }
}

pub fn make_editor(host: EditorHost) -> Option<Box<dyn Editor>> {
    Some(Box::new(TubeEditor::new(host)))
}

fn range() -> SizeRange {
    SizeRange::between(((W * 0.5) as u32, (H * 0.5) as u32), ((W * 2.0) as u32, (H * 2.0) as u32)).with_aspect(W as u32, H as u32)
}

impl TubeEditor {
    pub fn new(host: EditorHost) -> Self {
        let mut editor = Self {
            host: host.clone(),
            shared: None,
            local: (0, false, 3),
            values: crate::params::defaults(),
            page: Page::Analogue,
            channel_b: false,
            dual: false,
            overlay: Overlay::None,
            size: (W as u32, H as u32),
            widgets: Vec::new(),
            controls: ControlSet::new(host),
            canvas: SceneCanvas::new(W, H),
            damage: DamageTracker::new(),
            signatures: Vec::new(),
            meters: Meters { channels: 2, rail_avg: [26.0, 15.0, 15.0, 45.8, 74.0, 165.0], ..Meters::default() },
            spectrum: Spectrum::default(),
            status: (String::new(), String::new()),
            hot: None,
            down: None,
            drag: None,
            knob_click: None,
            modifiers: Modifiers::NONE,
        };
        editor.rebuild();
        editor
    }

    /// Points per editor unit and the corner of the editor in the window, in points.
    fn fit(&self) -> (f32, f32, f32) {
        let (w, h) = (self.size.0 as f32, self.size.1 as f32);
        let k = (w / W).min(h / H);
        (k, ((w - W * k) * 0.5).floor().max(0.0), ((h - H * k) * 0.5).floor().max(0.0))
    }

    fn to_editor(&self, x: f32, y: f32) -> (f32, f32) {
        let (k, left, top) = self.fit();
        ((x - left) / k, (y - top) / k)
    }

    fn to_window(&self, r: panel::R) -> DamageRect {
        let (k, left, top) = self.fit();
        DamageRect::new(r.0 * k + left, r.1 * k + top, r.2 * k, r.3 * k)
    }

    fn skin(&self) -> u8 {
        self.shared.as_ref().map_or(self.local.0, |s| s.skin())
    }
    fn system(&self) -> bool {
        self.shared.as_ref().map_or(self.local.1, |s| s.system_page.load(Relaxed))
    }
    fn vu_source(&self) -> usize {
        usize::from(self.shared.as_ref().map_or(self.local.2, |s| s.vu_source.load(Relaxed))).min(3)
    }

    /// Show a skin (0 analogue, 1 digital) and its main or System page.
    pub fn show(&mut self, skin: u8, system: bool) {
        let vu = self.vu_source() as u8;
        self.set_view(skin, system, vu);
    }

    /// The widgets of the page shown, for tests and tooling: each one's rectangle in editor units
    /// and the parameters it sets.
    pub fn widget_params(&self) -> Vec<(panel::R, Vec<usize>)> {
        self.widgets.iter().map(|w| (w.rect, w.params())).collect()
    }

    fn set_view(&mut self, skin: u8, system: bool, vu: u8) {
        self.local = (skin, system, vu);
        if let Some(s) = &self.shared {
            s.set_skin(skin);
            s.system_page.store(system, Relaxed);
            s.vu_source.store(vu, Relaxed);
        }
        self.follow();
    }

    fn channels(&self) -> u32 {
        self.shared.as_ref().map_or(2, |s| s.channels.load(Relaxed))
    }

    fn wanted(&self) -> (Page, bool) {
        let dual = self.values[idx::DUAL_MONO] > 0.5 && self.channels() == 2;
        (Page::of(self.skin() == 1, self.system()), dual)
    }

    /// The page's widgets and their controls at the current size.
    fn rebuild(&mut self) {
        let (page, dual) = self.wanted();
        self.page = page;
        self.dual = dual;
        if !dual {
            self.channel_b = false;
        }
        self.widgets = panel::widgets(page, self.channel_b, dual, self.overlay);
        let (k, left, top) = self.fit();
        let scaled = |r: panel::R| Area::rect(r.0 * k + left, r.1 * k + top, r.2 * k, r.3 * k);
        let mut controls = ControlSet::new(self.host.clone()).with_double_click(DoubleClick::Reset);
        // Controls under an open list stay inert: the list takes the clicks.
        if self.overlay == Overlay::None {
            for w in &self.widgets {
                let control = match w.kind {
                    Kind::Knob { param, body, .. } => {
                        let (cx, cy, radius) = panel::knob_circle(w);
                        let _ = body;
                        Control::knob(Area::circle(cx * k + left, cy * k + top, radius * k), param, &PARAMS[param]).drag_range(220.0 * k)
                    }
                    Kind::Slider { param, title_w, value_w, .. } => {
                        let (x0, x1) = paint::slider_track(w.rect, title_w, value_w);
                        Control::fader_h(scaled((x0, w.rect.1, x1 - x0, w.rect.3)), param, &PARAMS[param])
                    }
                    Kind::Led { param, .. } => Control::toggle(scaled(w.rect), param, &PARAMS[param]),
                    _ => continue,
                };
                let name = PARAMS[control.param].name;
                controls.add(control.tooltip(name));
            }
        }
        controls.set_modifiers(self.modifiers);
        for (param, value) in self.values.iter().enumerate() {
            controls.param_changed(param, *value);
        }
        self.controls = controls;
        self.signatures = vec![0; self.widgets.len()];
        self.hot = None;
        self.down = None;
        self.drag = None;
        self.knob_click = None;
        self.damage.add_all();
    }

    /// Rebuild when the page or the channel tabs changed, but not while a control is held.
    fn follow(&mut self) {
        let (page, dual) = self.wanted();
        if (page != self.page || dual != self.dual) && self.controls.pressed().is_none() && self.drag.is_none() {
            self.rebuild();
        }
    }

    /// Take the values the controls hold after input.
    fn sync(&mut self) {
        for control in self.controls.iter() {
            self.values[control.param] = control.value();
        }
        self.follow();
    }

    /// Set a parameter in one gesture.
    fn set(&mut self, param: usize, value: f32) {
        let value = PARAMS[param].clamp(value);
        self.host.set(param, value);
        self.values[param] = value;
        self.controls.param_changed(param, value);
    }

    fn ch(&self) -> usize {
        if self.channel_b { B } else { 0 }
    }

    fn program(&self) -> usize {
        self.shared.as_ref().map_or(0, |s| usize::from(s.program.load(Relaxed)))
    }

    fn looks(&self) -> Looks<'_> {
        let parts = self.parts();
        parts.looks(&self.values, &self.meters, &self.spectrum, &self.status)
    }

    /// What the looks take from the shared state, as values.
    fn parts(&self) -> Parts {
        let program = self.program();
        Parts {
            page: self.page,
            ch: self.ch(),
            channel_b: self.channel_b,
            hot: self.hot,
            down: self.down,
            vu_source: self.vu_source(),
            program,
            ab_is_b: self.shared.as_ref().is_some_and(|s| s.ab_is_b.load(Relaxed)),
            offline_active: self.shared.as_ref().is_some_and(|s| s.offline_bounce.load(Relaxed)),
            sample_rate: self.shared.as_ref().map_or(48_000.0, |s| s.sample_rate.get()),
        }
    }

    /// The widget under a point in window points, and the part of it (a bank's key, a graph's
    /// handle), when it is one the editor handles itself.
    fn custom_at(&self, x: f32, y: f32) -> Option<(usize, usize)> {
        let (px, py) = self.to_editor(x, y);
        let inside = |r: panel::R| px >= r.0 && px < r.0 + r.2 && py >= r.1 && py < r.1 + r.3;
        self.widgets.iter().enumerate().rev().find_map(|(i, w)| {
            if !inside(w.rect) {
                return None;
            }
            match w.kind {
                Kind::Button { .. } | Kind::Backdrop => Some((i, 0)),
                Kind::Bank(bank) if self.overlay == Overlay::None => Some((i, paint::bank_key(w.rect, bank, px))),
                Kind::Eq { .. } if self.overlay == Overlay::None => graphs::eq_hit(&EqPlot::of(w.rect), &Bands::of(&self.values, self.ch()), px, py).map(|h| (i, h)),
                Kind::Comp { .. } if self.overlay == Overlay::None => CompCurve::of(&self.values, self.ch()).hit(&CompPlot::of(w.rect), px, py).map(|h| (i, h)),
                _ => None,
            }
        })
    }

    fn open(&mut self, overlay: Overlay) {
        self.overlay = overlay;
        self.rebuild();
    }

    /// Set every parameter to `values`, each in its own gesture.
    fn set_all(&mut self, values: &[f32]) {
        for (p, v) in values.iter().enumerate().take(COUNT) {
            if self.values[p] != *v {
                self.set(p, *v);
            }
        }
    }

    fn press(&mut self, i: usize, part: usize, x: f32, y: f32, clicks: u32) {
        let skin = self.skin();
        let system = self.system();
        let vu = self.vu_source() as u8;
        match self.widgets[i].kind {
            Kind::Backdrop => self.open(Overlay::None),
            Kind::Button { action, .. } => match action {
                Action::Presets => self.open(if self.overlay == Overlay::Presets { Overlay::None } else { Overlay::Presets }),
                Action::Bounce => self.open(if self.overlay == Overlay::Bounce { Overlay::None } else { Overlay::Bounce }),
                Action::Program(p) => {
                    // A program sets channel A and leaves the setup and channel B alone (setCurrentProgram).
                    if let Some(values) = crate::programs::values(p) {
                        for (param, v) in values.iter().enumerate().take(PER_CHANNEL) {
                            if self.values[param] != *v {
                                self.set(param, *v);
                            }
                        }
                    }
                    if let Some(s) = &self.shared {
                        s.program.store(p as u8, Relaxed);
                    }
                    self.open(Overlay::None);
                }
                Action::BounceChoice(choice) => {
                    self.set(idx::OFFLINE_BOUNCE, choice as f32);
                    self.set(idx::OFFLINE_BOUNCE_ENABLED, 1.0);
                    self.open(Overlay::None);
                }
                Action::Ab => {
                    let Some(shared) = self.shared.clone() else { return };
                    let other = shared.take_other().unwrap_or_else(|| self.values.to_vec());
                    shared.set_other(self.values.to_vec());
                    self.set_all(&other);
                    shared.ab_is_b.fetch_xor(true, Relaxed);
                }
                Action::Copy => {
                    if let Some(s) = &self.shared {
                        s.set_other(self.values.to_vec());
                    }
                }
                Action::System => self.set_view(skin, !system, vu),
                Action::Skin => self.set_view(1 - skin.min(1), system, vu),
                Action::Channel(b) => {
                    self.channel_b = b;
                    self.rebuild();
                }
            },
            Kind::Bank(bank) => self.press_bank(bank, part),
            Kind::Eq { .. } => {
                if clicks >= 2 {
                    for (p, v) in graphs::eq_double_click(&self.values, self.ch(), part) {
                        self.set(p, v);
                    }
                    return;
                }
                self.drag = Some(GraphDrag { widget: i, handle: part, open: Vec::new() });
                self.drag_to(x, y);
            }
            Kind::Comp { .. } => {
                self.drag = Some(GraphDrag { widget: i, handle: part, open: Vec::new() });
                self.drag_to(x, y);
            }
            _ => {}
        }
    }

    fn press_bank(&mut self, bank: Bank, key: usize) {
        match bank {
            Bank::Quality => {
                // Freeze the legacy rate-dependent oversampling before the mode changes; from then
                // on the two controls are independent (KeyBank::mouseDown).
                if self.values[idx::OVERSAMPLING].round() == 0.0 {
                    let looks = self.looks();
                    let stages = paint::bank_selected(&looks, Bank::Oversampling).map_or(0, |s| s + 1);
                    self.set(idx::OVERSAMPLING, (stages + 1) as f32);
                }
                if key != 0 {
                    self.set(idx::QUALITY, if key == 1 { 0.0 } else { 1.0 });
                }
                self.set(idx::ECO, if key == 0 { 1.0 } else { 0.0 });
            }
            Bank::Oversampling => {
                let selected = paint::bank_selected(&self.looks(), Bank::Oversampling);
                self.set(idx::OVERSAMPLING, if selected == Some(key) { 1.0 } else { (key + 2) as f32 });
            }
            Bank::Mains => self.set(idx::MAINS, key as f32),
            Bank::VuSource => {
                let (skin, system) = (self.skin(), self.system());
                self.set_view(skin, system, key as u8);
                self.meters.bars = Default::default();
            }
        }
    }

    /// Move the held graph handle to a point in window points.
    fn drag_to(&mut self, x: f32, y: f32) {
        let Some(mut drag) = self.drag.take() else { return };
        let (px, py) = self.to_editor(x, y);
        let rect = self.widgets[drag.widget].rect;
        let ch = self.ch();
        let edits = match self.widgets[drag.widget].kind {
            Kind::Eq { .. } => graphs::eq_drag(&EqPlot::of(rect), &self.values, ch, drag.handle, px, py),
            _ => vec![CompCurve::of(&self.values, ch).drag(&CompPlot::of(rect), ch, drag.handle, px, py)],
        };
        for (param, value) in edits {
            let value = PARAMS[param].clamp(value);
            if PARAMS[param].unit == openplugin::prelude::Unit::Toggle {
                // A switch the drag crosses is a whole edit of its own (ParamEditor::setBool).
                if self.values[param] != value {
                    self.set(param, value);
                }
                continue;
            }
            if !drag.open.contains(&param) {
                self.host.begin(param);
                drag.open.push(param);
            }
            self.host.perform(param, value);
            self.values[param] = value;
            self.controls.param_changed(param, value);
        }
        self.drag = Some(drag);
    }

    fn end_drag(&mut self) {
        if let Some(drag) = self.drag.take() {
            for param in drag.open {
                self.host.end(param);
            }
        }
    }

    fn collect_damage(&mut self) {
        let looks = self.looks();
        let changed: Vec<(usize, u64)> = self
            .widgets
            .iter()
            .enumerate()
            .filter_map(|(i, w)| {
                let signature = paint::signature(w, i, &looks);
                (signature != self.signatures[i]).then_some((i, signature))
            })
            .collect();
        for (i, signature) in changed {
            self.signatures[i] = signature;
            self.damage.add(DamageRect::from_tuple(paint::reach(&self.widgets[i])));
        }
        let _ = self.controls.take_changed();
    }

    /// The meters, `dt` seconds after the last tick (Panels.h timers).
    fn read_meters(&mut self, shared: &Shared, dt: f32) {
        let Some(telemetry) = &shared.telemetry else { return };
        let t = [telemetry.read(0), telemetry.read(1)];
        let channels = shared.channels.load(Relaxed).clamp(1, 2) as usize;
        let dual = self.dual;
        let selected = usize::from(dual && self.channel_b);
        let source = self.vu_source();
        let align = [-14.0f32, -16.0, -18.0, -20.0, -22.0][(self.values[idx::ALIGNMENT].round() as usize).min(4)];
        let page = self.page;
        let m = &mut self.meters;
        m.channels = channels;
        let ticks = (dt * 30.0).clamp(0.0, 30.0);
        let follow = |shown: &mut f32, target: f32| {
            let k: f32 = if target > *shown { 0.5 } else { 0.12 };
            *shown += (target - *shown) * (1.0 - (1.0 - k).powf(ticks));
        };
        let pick = |f: &dyn Fn(&crate::engine::Meters) -> f32| if dual || channels == 1 { f(&t[selected]) } else { f(&t[0]).max(f(&t[1])) };

        // the meter: calibrated RMS VU, or OUT as stereo sample peaks after all output gain (digital)
        let output_ref = 10f32.powf(align / 20.0) / std::f32::consts::SQRT_2;
        let relative: Vec<f32> = (0..2).map(|ch| if source == 3 { t[ch].out_rms / output_ref } else { t[ch].vu_tap[source] / 0.347 }).collect();
        if page == Page::Digital {
            for (ch, (bar, rel)) in m.bars.iter_mut().zip(&relative).enumerate().take(channels) {
                let peak = shared.output_peak[ch].take();
                bar.update(if source == 3 { peak } else { *rel }, dt);
            }
        }
        let rms = if dual { relative[selected] } else { relative[0].max(if channels > 1 { relative[1] } else { 0.0 }) };
        let vu_db = if rms > 1e-6 { 20.0 * rms.log10() } else { -60.0 };
        m.needle += (vu_position_for_db(vu_db) - m.needle) * (1.0 - (-dt / 0.08).exp());
        m.display_db = if dual || channels == 1 { m.bars[selected].held_db } else { m.bars[0].held_db.max(m.bars[1].held_db) };

        follow(&mut m.gain_reduction, (pick(&|t| t.gr_db.max(0.0)) / 24.0).clamp(0.0, 1.0));
        let drive = pick(&|t| t.tube_drive);
        let db = if drive > 1e-4 { 20.0 * (drive / 12.0).log10() } else { -80.0 };
        follow(&mut m.warmth, ((db + 40.0) / 40.0).clamp(0.0, 1.0));
        m.comp_gr += (t[selected].gr_db.max(0.0) - m.comp_gr) * (1.0 - 0.7f32.powf(ticks));

        let clipped = if page == Page::Digital {
            let (l, r) = (shared.output_clip[0].swap(false, Relaxed), shared.output_clip[1].swap(false, Relaxed));
            l || r
        } else {
            t[0].clip > 0.5 || (channels > 1 && t[1].clip > 0.5)
        };
        m.clip = if clipped { 1.0 } else { m.clip * 0.85f32.powf(dt * 20.0) };

        if page.system() {
            let p = &t[0];
            let load = (p.load_p15 + p.load_tube).clamp(0.0, 0.5);
            m.flow_phase = (m.flow_phase + (0.004 + load * 0.06) * ticks).rem_euclid(1.0);
            let rails = [p.p25, p.p15, -p.n15, p.p45, p.p75, p.tube_p];
            let k = if m.rails_read { 1.0 - 0.98f32.powf(ticks) } else { 1.0 };
            for (avg, v) in m.rail_avg.iter_mut().zip(rails) {
                *avg += (v - *avg) * k;
            }
            m.rails_read = true;
        }
        let [a, _] = t;
        m.power = Some(a);
    }

    /// The System page's engine line (TubeChannelEditor::timerCallback).
    fn read_status(&mut self, shared: &Shared) {
        let factor = shared.factor.load(Relaxed);
        let rate = shared.sample_rate.get();
        let quality = shared.quality.load(Relaxed);
        let mode = if quality == 3 { "Eco" } else if shared.prototype_compressor.load(Relaxed) { "Circuit" } else { "HQ" };
        let mut a = format!("{mode}, {factor}x, {:.1} kHz", f64::from(factor) * f64::from(rate) / 1000.0);
        if shared.offline_bounce.load(Relaxed) {
            a = format!("Offline bounce: {a}");
        }
        if shared.building.load(Relaxed) {
            a = "preparing audio settings...".into();
        }
        let phase = if self.values[idx::LINEAR_PHASE] > 0.5 { "linear phase" } else { "minimum phase" };
        self.status = (a, format!("{phase}, {} samples latency", shared.latency.load(Relaxed)));
    }
}

impl Editor for TubeEditor {
    fn size(&self) -> (u32, u32) {
        self.size
    }

    fn size_range(&self) -> SizeRange {
        range()
    }

    fn resize(&mut self, width: u32, height: u32) {
        let size = range().clamp(width, height);
        if size != self.size {
            self.size = size;
            self.rebuild();
        }
    }

    fn bind(&mut self, state: Option<EditorState>) {
        self.shared = state.and_then(|s| s.downcast::<Shared>().ok());
        self.rebuild();
    }

    fn param_changed(&mut self, index: usize, plain: f32) {
        if index < COUNT {
            self.values[index] = plain;
            self.controls.param_changed(index, plain);
            self.follow();
        }
    }

    fn tick(&mut self, dt: f32) {
        self.follow();
        let Some(shared) = self.shared.clone() else { return };
        self.read_meters(&shared, dt);
        if self.page.system() {
            self.read_status(&shared);
        }
        if self.page == Page::Digital {
            self.spectrum.update(&shared.spectrum, f64::from(shared.sample_rate.get()));
        }
    }

    fn damage(&mut self) -> Option<Vec<DamageRect>> {
        self.collect_damage();
        self.damage.pending().map(|rects| rects.iter().map(|r| self.to_window((r.x, r.y, r.w, r.h))).collect())
    }

    fn render(&mut self, scale: f32) -> Frame<'_> {
        self.collect_damage();
        let (k, left, top) = self.fit();
        let device = if scale.is_finite() { scale.clamp(1.0, 4.0) } else { 2.0 };
        let unit = device * k;
        self.canvas.resize(self.size.0 as f32, self.size.1 as f32);
        let page = self.page;
        let parts = self.parts();
        let looks = parts.looks(&self.values, &self.meters, &self.spectrum, &self.status);
        let widgets = &self.widgets;
        let pending: Option<Vec<DamageRect>> = self.damage.pending().map(|r| r.to_vec());
        let touched = |w: &Widget| {
            let reach = DamageRect::from_tuple(paint::reach(w));
            pending.as_ref().is_none_or(|rects| rects.iter().any(|r| r.touches(&reach)))
        };
        let in_points: Option<Vec<DamageRect>> = pending.as_ref().map(|rects| {
            rects.iter().map(|r| DamageRect::new(r.x * k + left - 1.0, r.y * k + top - 1.0, r.w * k + 2.0, r.h * k + 2.0)).collect()
        });
        let bake_at = |unit: f32| {
            draw::set_knob_scale(unit);
            hardware::set_device_scale(unit);
        };
        let overlay = self.overlay as u64;
        let frame = self.canvas.frame_damaged(
            device,
            page as u64 | overlay << 8,
            |dl| {
                bake_at(unit);
                let mut scene = Scene::new();
                paint::layer(&mut scene, page);
                place::place(&mut scene, k, left, top);
                dl.append(scene);
            },
            |dl| {
                bake_at(unit);
                let mut scene = Scene::new();
                for (i, w) in widgets.iter().enumerate().filter(|(_, w)| touched(w)) {
                    paint::widget(&mut scene, w, i, &looks);
                }
                place::place(&mut scene, k, left, top);
                dl.append(scene);
            },
            in_points.as_deref(),
        );
        self.damage.clear();
        frame
    }

    fn modifiers_changed(&mut self, modifiers: Modifiers) {
        self.modifiers = modifiers;
        self.controls.set_modifiers(modifiers);
    }

    fn mouse_down(&mut self, x: f32, y: f32, clicks: u32) {
        if let Some((i, part)) = self.custom_at(x, y) {
            self.controls.set_focus(None);
            self.down = Some(i);
            self.press(i, part, x, y, clicks);
            return;
        }
        // A plain click on the RATIO knob pops it out to the limiter (Knob::bindClick).
        let (px, py) = self.to_editor(x, y);
        self.knob_click = self.widgets.iter().find_map(|w| match w.kind {
            Kind::Knob { click: Some(param), .. } => {
                let (cx, cy, r) = panel::knob_circle(w);
                ((px - cx).hypot(py - cy) <= r).then_some(KnobClick { param, at: (x, y), moved: false })
            }
            _ => None,
        });
        self.controls.mouse_down(x, y, clicks);
        self.sync();
    }

    fn mouse_move(&mut self, x: f32, y: f32) {
        if let Some(click) = self.knob_click.as_mut() {
            click.moved |= (x - click.at.0).hypot(y - click.at.1) > 3.0;
        }
        if self.drag.is_some() {
            self.drag_to(x, y);
            return;
        }
        self.hot = self.custom_at(x, y);
        self.controls.mouse_move(x, y);
        self.sync();
    }

    fn mouse_up(&mut self, x: f32, y: f32) {
        self.end_drag();
        self.down = None;
        self.controls.mouse_up(x, y);
        self.sync();
        if let Some(click) = self.knob_click.take() {
            if !click.moved {
                let v = if self.values[click.param] > 0.5 { 0.0 } else { 1.0 };
                self.set(click.param, v);
            }
        }
    }

    fn wheel(&mut self, x: f32, y: f32, dy: f32) {
        self.controls.wheel(x, y, dy);
        self.sync();
    }

    fn wants_keys(&self) -> bool {
        self.controls.wants_keys()
    }

    fn key_down(&mut self, key: Key, modifiers: Modifiers) -> bool {
        let taken = self.controls.key_down(key, modifiers);
        self.sync();
        taken
    }

    fn key_up(&mut self, key: Key, modifiers: Modifiers) -> bool {
        let taken = self.controls.key_up(key, modifiers);
        self.sync();
        taken
    }

    fn focus_changed(&mut self, focused: bool) {
        if !focused {
            self.controls.release_keys();
        }
    }

    fn accessibility_nodes(&self) -> Vec<AccessNode> {
        self.controls.accessibility_nodes()
    }

    fn accessibility_action(&mut self, node: usize, action: AccessAction) -> bool {
        let done = self.controls.accessibility_action(node, action);
        self.sync();
        done
    }

    fn accessibility_range(&self, node: usize) -> Option<AccessRange> {
        self.controls.accessibility_range(node)
    }

    fn accessibility_set_value(&mut self, node: usize, value: f64) -> bool {
        let done = self.controls.accessibility_set_value(node, value);
        self.sync();
        done
    }

    fn accessibility_focus(&mut self, node: usize) -> bool {
        self.controls.accessibility_focus(node)
    }

    fn right_mouse_down(&mut self, x: f32, y: f32) -> bool {
        self.controls.context_click(x, y)
    }

    fn take_param_menu(&mut self) -> Option<ParamMenu> {
        self.controls.take_param_menu()
    }

    fn param_menu_action(&mut self, param: u32, action: ParamMenuAction) -> Option<String> {
        let result = self.controls.param_menu_action(param, action);
        self.sync();
        result
    }

    fn take_text_edit(&mut self) -> Option<TextEdit> {
        self.controls.take_text_edit()
    }

    fn text_edit_commit(&mut self, text: &str) -> bool {
        let done = self.controls.text_edit_commit(text);
        self.sync();
        done
    }

    fn text_edit_cancel(&mut self) {
        self.controls.text_edit_cancel();
    }

    fn tooltips(&self) -> Vec<Tooltip> {
        let mut tips = self.controls.tooltips();
        let bounce = self.values[idx::OFFLINE_BOUNCE].round() as usize;
        for w in &self.widgets {
            let text = match w.kind {
                Kind::Button { action: Action::Bounce, .. } if self.values[idx::OFFLINE_BOUNCE_ENABLED] > 0.5 => {
                    format!("Offline bounce: {}. Realtime exports use playback settings.", crate::params::OFFLINE_BOUNCE.get(bounce).unwrap_or(&""))
                }
                Kind::Button { action: Action::Bounce, .. } => "This saved project uses playback settings for offline bounce. Choose a combination to enable separate offline settings.".into(),
                Kind::Button { action: Action::Ab, .. } => "Switch between two complete settings (A and B)".into(),
                Kind::Button { action: Action::Copy, .. } => "Copy these settings to the other of A and B".into(),
                Kind::Eq { .. } => "Equaliser: drag a band's frequency and gain; double-click a band for 0 dB (a mid band also flips narrow and wide)".into(),
                Kind::Comp { .. } => "Compressor: drag threshold, ratio and output".into(),
                Kind::Knob { click: Some(_), .. } => "Click the knob to pop it out: brickwall limiter".into(),
                _ => continue,
            };
            tips.push(Tooltip { rect: self.to_window(w.rect), text });
        }
        tips
    }
}

#[cfg(test)]
mod tests;
