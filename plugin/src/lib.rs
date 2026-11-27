//! Tube Channel on OpenPluginSDK.
//!
//! The signal path is the C++ chain of the JUCE release (../Source: Channel, Circuit, DSP, Hybrid and
//! Plugin/Chain.h; no JUCE in it), called through cpp/bridge.cpp. This crate is the plugin layer:
//! parameters, identity, state, buses, the off-thread rebuild of the chain, and two additions of the
//! port, Autogain and Delta.
//!
//! Signal path: chain (input trim, circuit, output trim) -> latency padding -> mode-switch fade ->
//! Autogain (the input's BS.1770 loudness, when on) -> Delta (the result minus the input delayed by
//! the reported latency, when on). Delta therefore hears what the channel changes, after any level
//! matching.

mod editor;
pub mod engine;
pub mod params;
pub mod programs;
pub mod shared;
pub mod state_import;

use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;

use openplugin::core::foreign_state::{plist, ForeignStateKind};
use openplugin::core::{decode_params, encode_params, AaxIds, AuIds, HostNotifier, Vst3Ids};
use openplugin::dsp::{Delta, LoudnessMatch};
use openplugin::prelude::*;

pub use editor::{make_editor, TubeEditor};
use engine::{Builder, Latencies, Setup, State, StateKey};
pub use params::{idx, COUNT, PARAMS};
pub use shared::{Shared, View};

pub static INFO: PluginInfo = PluginInfo::new("com.opengrid.tubechannel", "Tube Channel", "OpenGrid", "2.0.0", Category::Dynamics, &PARAMS)
    .with_url("https://github.com/MistaMin/TubeChannel")
    .with_editor(true);

static LAYOUTS: [IoConfig; 4] = [
    IoConfig { input: SpeakerLayout::Stereo, output: SpeakerLayout::Stereo, sidechain: Some(SpeakerLayout::Stereo) },
    IoConfig { input: SpeakerLayout::Stereo, output: SpeakerLayout::Stereo, sidechain: None },
    IoConfig { input: SpeakerLayout::Mono, output: SpeakerLayout::Mono, sidechain: Some(SpeakerLayout::Mono) },
    IoConfig { input: SpeakerLayout::Mono, output: SpeakerLayout::Mono, sidechain: None },
];

/// The parameters a new chain is built for (`TubeChannelProcessor`'s listener list).
const STRUCTURAL: [usize; 8] = [idx::QUALITY, idx::ECO, idx::OVERSAMPLING, idx::OFFLINE_BOUNCE, idx::OFFLINE_BOUNCE_ENABLED, idx::LINEAR_PHASE, idx::TOLERANCES, idx::SEED];

/// How long the output can stay above -90 dBFS after the input stops: the coupling capacitors,
/// transformers and the compressor's recovery settling after a loud signal. Measured with
/// examples/tail.rs: 0.41 s at the defaults, 1.76 s for the instrument input at full gain (the
/// circuit noise alone sits at -129 dBFS). The JUCE release reported no tail.
const TAIL_SECONDS: f32 = 2.5;

/// Length of the fade-in after a chain swap or a new state (the JUCE build's 20 ms).
const FADE_SECONDS: f32 = 0.02;

pub struct TubeChannel {
    /// Each parameter's plain value, in table order.
    values: [f32; COUNT],
    state: Option<State>,
    builder: Option<Builder>,
    setup: Option<Setup>,
    /// The key the parameters ask for.
    wanted: StateKey,
    offline: bool,
    /// Counts offline starts and resets: a bounce chain renders each offline pass from a fresh build.
    epoch: u64,
    rendered_epoch: u64,
    bounce_has_run: bool,
    fade_remaining: usize,
    fade_length: usize,
    /// The live state has processed audio since it was built (a reset has something to clear).
    used: bool,
    autogain: LoudnessMatch,
    delta: Option<Delta>,
    dry: [Vec<f32>; 2],
    max_block: usize,
    sample_rate: f32,
    /// The latency of the state the parameters ask for (reported at once, as the JUCE build did;
    /// the state itself follows within a few milliseconds, under the swap's fade).
    latency: usize,
    latencies: Option<Latencies>,
    notifier: Option<HostNotifier>,
    shared: Arc<Shared>,
}

impl Default for TubeChannel {
    fn default() -> Self {
        assert!(engine::table_matches(), "cpp/bridge.cpp and src/params.rs disagree on the parameter table");
        let values = params::defaults();
        Self {
            wanted: StateKey::of(&values),
            values,
            state: None,
            builder: None,
            setup: None,
            offline: false,
            epoch: 0,
            rendered_epoch: 0,
            bounce_has_run: false,
            fade_remaining: 0,
            fade_length: 960,
            used: false,
            autogain: LoudnessMatch::new(48_000.0),
            delta: None,
            dry: [Vec::new(), Vec::new()],
            max_block: 0,
            sample_rate: 48_000.0,
            latency: 0,
            latencies: None,
            notifier: None,
            shared: Arc::new(Shared::default()),
        }
    }
}

impl TubeChannel {
    /// Each parameter's plain value, in table order.
    pub fn values(&self) -> &[f32; COUNT] {
        &self.values
    }

    pub fn shared(&self) -> &Arc<Shared> {
        &self.shared
    }

    /// Replaces every value at once (a state load) and asks for the chain they need.
    fn set_values(&mut self, values: [f32; COUNT]) {
        for (i, v) in values.into_iter().enumerate() {
            self.values[i] = PARAMS[i].clamp(v);
        }
        self.request();
    }

    fn request(&mut self) {
        self.wanted = StateKey::of(&self.values);
        if let Some(latency) = self.latencies.as_ref().map(|l| l.of(self.wanted)) {
            if latency != self.latency {
                self.latency = latency;
                if let Some(d) = self.delta.as_mut() {
                    d.set_latency(latency);
                }
                if let Some(n) = &self.notifier {
                    n.latency_changed();
                }
            }
        }
        if let Some(builder) = &self.builder {
            builder.request(self.wanted);
        }
        let building = self.state.as_ref().is_some_and(|s| s.key != self.wanted);
        self.shared.building.store(building, Relaxed);
    }

    /// Takes over a new state; the old one is retired (to the builder thread) or dropped (offline).
    fn install(&mut self, mut fresh: State, keep_bounce: bool) {
        if let Some(mut old) = self.state.take() {
            fresh.preserve(&mut old, keep_bounce);
            match &mut self.builder {
                Some(builder) => builder.retire(old),
                None => drop(old), // rt-lint: allow(RT-1) only without a builder: prepare, or a test with no prepare
            }
        }
        self.state = Some(fresh);
        self.used = false;
    }

    fn publish(&self, offline: bool) {
        let Some(state) = &self.state else { return };
        let engine = state.engine(offline);
        let s = &self.shared;
        s.channels.store(engine.channels() as u32, Relaxed);
        s.factor.store(engine.factor(), Relaxed);
        s.prototype_compressor.store(engine.compact_detector(), Relaxed);
        s.quality.store(if offline && state.key.bounce_enabled { state.key.bounce.quality } else { state.key.playback.quality } as u32, Relaxed);
        s.offline_bounce.store(offline && state.key.bounce_enabled, Relaxed);
        s.latency.store(state.latency as u32, Relaxed);
        s.building.store(state.key != self.wanted, Relaxed);
    }

    /// One block of at most `max_block` frames.
    fn block(&mut self, io: &mut [&mut [f32]], sidechain: &[&[f32]], frames: usize) {
        let channels = io.len().min(2);
        let offline = self.offline;
        if offline {
            if let Some(fresh) = self.builder.as_mut().and_then(Builder::take) {
                self.install(fresh, true);
            }
            let rerender = self.wanted.bounce_enabled && self.bounce_has_run && self.epoch != self.rendered_epoch;
            if self.state.as_ref().is_none_or(|s| s.key != self.wanted) || rerender {
                // Some hosts go offline or restore parameters right before rendering, with no prepare
                // and no message loop in between. Synchronous work is allowed ONLY offline.
                if let Some(setup) = self.setup {
                    // rt-lint: allow(RT-1) offline render only, as the JUCE build did
                    if let Some(fresh) = State::build(self.wanted, &setup) {
                        self.install(fresh, false);
                    }
                }
            }
            self.rendered_epoch = self.epoch;
            self.bounce_has_run = self.wanted.bounce_enabled;
            self.fade_remaining = 0;
        }
        if let Some(builder) = self.builder.as_mut() {
            builder.refresh_spare(self.wanted);
        }
        let Some(state) = self.state.as_mut() else { return };

        for (dry, channel) in self.dry.iter_mut().zip(io.iter()) {
            dry[..frames].copy_from_slice(&channel[..frames]);
        }
        state.process(io, frames, sidechain, &self.values, self.shared.telemetry.as_ref().expect("telemetry"), offline);
        self.used = true;

        // A finished chain is swapped in after this block: fade this block out and the next ones in.
        if !offline {
            if let Some(fresh) = self.builder.as_mut().and_then(Builder::take) {
                let changed = self.state.as_ref().is_some_and(|s| s.key.playback != fresh.key.playback || s.latency != fresh.latency);
                self.install(fresh, true);
                if changed {
                    for channel in io.iter_mut().take(channels) {
                        for (i, x) in channel[..frames].iter_mut().enumerate() {
                            *x *= 1.0 - (i + 1) as f32 / frames as f32;
                        }
                    }
                    self.fade_remaining = self.fade_length;
                }
            } else if self.fade_remaining > 0 {
                let len = self.fade_remaining.min(frames);
                for channel in io.iter_mut().take(channels) {
                    for (i, x) in channel[..len].iter_mut().enumerate() {
                        *x *= 1.0 - (self.fade_remaining - i) as f32 / self.fade_length as f32;
                    }
                }
                self.fade_remaining -= len;
            }
        }
        self.publish(offline);

        let dry: [&[f32]; 2] = [&self.dry[0][..frames], &self.dry[1][..frames]];
        let auto_on = self.values[idx::AUTOGAIN] > 0.5;
        if auto_on != self.autogain.is_enabled() {
            self.autogain.set_enabled(auto_on);
        }
        {
            let (a, b) = io.split_at_mut(1);
            let mut pair: [&mut [f32]; 2] = [&mut a[0][..frames], if channels == 2 { &mut b[0][..frames] } else { &mut [] }];
            let wet = &mut pair[..channels];
            self.autogain.process(&dry[..channels], wet);
            if let Some(d) = self.delta.as_mut() {
                let delta_on = self.values[idx::DELTA] > 0.5;
                if delta_on != d.is_enabled() {
                    d.set_enabled(delta_on);
                }
                d.process(&dry[..channels], wet);
            }
        }

        match channels {
            2 => self.shared.spectrum.push(&[&io[0][..frames], &io[1][..frames]]),
            _ => self.shared.spectrum.push(&[&io[0][..frames]]),
        }
        for (ch, data) in io.iter().take(channels).enumerate() {
            let peak = data[..frames].iter().fold(0.0f32, |m, x| m.max(x.abs()));
            self.shared.output_peak[ch].raise(peak);
            if peak >= 1.0 {
                self.shared.output_clip[ch].store(true, Relaxed);
            }
        }
    }
}

impl Plugin for TubeChannel {
    fn info(&self) -> &'static PluginInfo {
        &INFO
    }

    fn prepare(&mut self, sample_rate: f32, max_block: usize, channels: usize) {
        let max_block = max_block.max(1);
        self.sample_rate = sample_rate;
        self.max_block = max_block;
        self.shared.sample_rate.set(sample_rate);
        // The builder thread is stopped first, so nothing else touches the state while it is replaced.
        self.builder = None;
        let setup = Setup::new(f64::from(sample_rate), max_block, channels, &self.values);
        let latencies = Latencies::new(f64::from(sample_rate));
        self.setup = Some(setup);
        self.wanted = StateKey::of(&self.values);
        self.state = None;
        self.latency = latencies.of(self.wanted);
        self.latencies = Some(latencies);
        if let Some(fresh) = State::build(self.wanted, &setup) {
            self.state = Some(fresh);
        }
        self.used = false;
        let spare = State::build(self.wanted, &setup);
        self.builder = Some(Builder::start(setup, self.wanted, spare));
        self.dry = [vec![0.0; max_block], vec![0.0; max_block]];
        self.delta = Some(Delta::prepare(2, engine::max_latency(f64::from(sample_rate)), sample_rate, 20.0));
        if let Some(d) = self.delta.as_mut() {
            d.set_latency(self.latency);
        }
        self.autogain = LoudnessMatch::new(sample_rate);
        self.fade_length = ((sample_rate * FADE_SECONDS).round() as usize).max(1);
        self.fade_remaining = 0;
        self.bounce_has_run = false;
        self.rendered_epoch = self.epoch;
        self.publish(self.offline);
    }

    fn set_offline(&mut self, offline: bool) {
        if offline && !self.offline {
            self.epoch += 1;
        }
        self.offline = offline;
    }

    /// Clears the circuit by swapping in the builder's spare, a state built for these settings (the
    /// chain has no reset of its own). When no spare is ready yet the circuit keeps its state, as in
    /// the JUCE build. A reset also starts a new offline pass, so an offline bounce renders from a
    /// fresh chain.
    fn reset(&mut self) {
        self.epoch += 1;
        let wanted = self.wanted;
        if !self.used {
            return; // a circuit that has not run yet has nothing to clear
        }
        if let Some(builder) = self.builder.as_mut() {
            if builder.can_retire() {
                if let Some(fresh) = builder.take_spare(wanted) {
                    if let Some(old) = self.state.replace(fresh) {
                        builder.retire(old);
                    }
                    self.fade_remaining = 0;
                    self.used = false;
                }
            }
        }
        self.autogain.reset();
        if let Some(d) = self.delta.as_mut() {
            d.reset();
        }
    }

    fn set_param(&mut self, id: &str, value: f32) -> bool {
        params::index_of(id).is_some_and(|i| self.set_param_index(i, value))
    }

    fn set_param_index(&mut self, index: usize, value: f32) -> bool {
        let Some(info) = PARAMS.get(index) else { return false };
        let value = info.clamp(value);
        let changed = self.values[index] != value;
        self.values[index] = value;
        if changed && STRUCTURAL.contains(&index) {
            self.request();
        }
        true
    }

    fn param(&self, id: &str) -> Option<f32> {
        params::index_of(id).map(|i| self.values[i])
    }

    fn latency(&self) -> usize {
        self.latency
    }

    fn tail(&self) -> Tail {
        Tail::from_samples((TAIL_SECONDS * self.sample_rate) as usize)
    }

    fn max_latency(&self) -> Option<usize> {
        Some(engine::max_latency(f64::from(self.sample_rate)))
    }

    fn process(&mut self, io: &mut [&mut [f32]], frames: usize) {
        self.process_buses(io, None, frames);
    }

    fn process_buses(&mut self, io: &mut [&mut [f32]], sidechain: Option<&[&[f32]]>, frames: usize) {
        let channels = io.len().min(2);
        if channels == 0 || frames == 0 || self.max_block == 0 || io[..channels].iter().any(|c| c.len() < frames) {
            return;
        }
        let sidechain = sidechain.unwrap_or(&[]);
        let mut start = 0;
        while start < frames {
            let n = self.max_block.min(frames - start);
            let (a, b) = io.split_at_mut(1);
            let mut part: [&mut [f32]; 2] = [&mut a[0][start..start + n], if channels == 2 { &mut b[0][start..start + n] } else { &mut [] }];
            let mut keys: [&[f32]; 2] = [&[], &[]];
            let nsc = sidechain.len().min(2);
            for (k, s) in keys.iter_mut().zip(sidechain) {
                *k = s.get(start..start + n).unwrap_or(&[]);
            }
            self.block(&mut part[..channels], &keys[..nsc], n);
            start += n;
        }
    }

    fn set_host_notifier(&mut self, notifier: HostNotifier) {
        self.notifier = Some(notifier);
    }

    fn editor_state(&self) -> Option<EditorState> {
        Some(self.shared.clone())
    }

    fn preset_count(&self) -> usize {
        programs::PROGRAMS.len()
    }

    fn preset_name(&self, index: usize) -> &'static str {
        programs::PROGRAMS.get(index).map_or("", |p| p.name)
    }

    fn preset_values(&self, index: usize) -> Option<&'static [f32]> {
        programs::values(index)
    }

    fn meters(&self) -> MeterFrame {
        let mut m = MeterFrame::SILENT;
        if let Some(t) = &self.shared.telemetry {
            let a = t.read(0);
            let b = t.read(1);
            let db = |v: f32| 20.0 * v.max(1e-6).log10();
            m.input_db = [db(a.in_peak), db(b.in_peak)];
            m.output_db = [db(a.out_peak), db(b.out_peak)];
            m.reduction_db = a.gr_db.max(b.gr_db);
            m.clip = u8::from(a.clip > 0.5 || b.clip > 0.5);
        }
        m
    }

    fn supported_layouts(&self) -> &'static [IoConfig] {
        &LAYOUTS
    }

    fn note_ports(&self) -> NotePorts {
        NotePorts { input: false, output: false }
    }

    // ---- the identity of the JUCE release (1.2.x), so its sessions find this plugin ----
    fn vst3_ids(&self) -> Option<Vst3Ids> {
        Vst3Ids::from_hex("ABCDEF019182FAEB4F70477254624368", "ABCDEF011234ABCD4F70477254624368")
    }

    fn au_ids(&self) -> Option<AuIds> {
        Some(AuIds { subtype: *b"TbCh", manufacturer: *b"OpGr" })
    }

    fn lv2_uri(&self) -> Option<&'static str> {
        Some("urn:opengrid:tubechannel")
    }

    // JUCE's AAX codes: its manufacturer and plugin codes, and the plug-in type ids it derives from
    // the bus layout (`jcaa` plus 0x101 for mono, 0x202 for stereo).
    fn aax_ids(&self) -> Option<AaxIds> {
        Some(AaxIds { manufacturer: *b"OpGr", product: *b"TbCh", mono: *b"jcbb", stereo: *b"jccc" })
    }

    /// `TCH2`, the editor's view (skin, meter source, System page, a spare byte), then the SDK's
    /// parameter snapshot.
    fn save_state(&mut self) -> Option<Vec<u8>> {
        let s = &self.shared;
        let mut out = STATE_MAGIC.to_vec();
        out.extend_from_slice(&[s.skin(), s.vu_source.load(Relaxed), u8::from(s.system_page.load(Relaxed)), 0]);
        out.extend_from_slice(&encode_params(&INFO, &self.values));
        Some(out)
    }

    /// The JUCE release's state first (sessions and presets made before the port), then the port's
    /// own. A refused state changes nothing.
    fn load_state(&mut self, data: &[u8]) -> bool {
        let (values, view) = match state_import::import_original(data) {
            Some(Ok(state)) => state,
            Some(Err(_)) => return false,
            None => match decode_own(data) {
                Some(state) => state,
                None => return false,
            },
        };
        self.set_values(values);
        self.shared.set_view(view);
        true
    }

    fn foreign_state_kinds(&self) -> &'static [ForeignStateKind] {
        &[ForeignStateKind::Vst3Component, ForeignStateKind::AuClassInfo, ForeignStateKind::AaxChunk]
    }

    /// What the JUCE release saved through VST3, Audio Unit and AAX hosts: its own state inside each
    /// format's container. A refused state changes nothing.
    fn load_foreign_state(&mut self, kind: ForeignStateKind, data: &[u8]) -> bool {
        let class_info;
        let chunk = match kind {
            ForeignStateKind::Vst3Component => Some(data),
            ForeignStateKind::AuClassInfo => {
                class_info = plist::parse(data).ok();
                class_info.as_ref().and_then(|v| v.get("jucePluginState")).and_then(|v| v.as_data())
            }
            ForeignStateKind::AaxChunk => data.strip_prefix(b"juce"),
            _ => None,
        };
        let Some(Some(Ok((values, view)))) = chunk.map(state_import::import_original) else { return false };
        self.set_values(values);
        self.shared.set_view(view);
        true
    }
}

const STATE_MAGIC: &[u8; 4] = b"TCH2";

fn decode_own(data: &[u8]) -> Option<([f32; COUNT], View)> {
    let body = data.strip_prefix(STATE_MAGIC)?;
    let (head, params) = body.split_at_checked(4)?;
    if head[0] > 1 || head[1] > 3 || head[2] > 1 || head[3] != 0 {
        return None;
    }
    let values = <[f32; COUNT]>::try_from(decode_params(&INFO, params)?).ok()?;
    Some((values, View { skin: head[0], vu_source: head[1], system_page: head[2] == 1 }))
}

openplugin::targets::export_clap!(TubeChannel, editor = make_editor);
openplugin::targets::export_lv2!(TubeChannel, editor = make_editor);
openplugin::targets::export_shell!(TubeChannel, editor = make_editor);

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn own_state_round_trips() {
        let mut a = TubeChannel::default();
        for p in PARAMS.iter() {
            assert!(a.set_param(p.id, p.max));
        }
        let view = View { skin: 1, vu_source: 1, system_page: true };
        a.shared.set_view(view);
        let blob = a.save_state().expect("a state");
        let mut b = TubeChannel::default();
        assert!(b.load_state(&blob));
        assert_eq!(a.values(), b.values());
        assert_eq!(b.shared.view(), view);
        assert_eq!(b.save_state().unwrap(), blob);
    }

    #[test]
    fn fewer_channels_than_prepared_do_not_crash() {
        let mut p = TubeChannel::default();
        p.prepare(48_000.0, 256, 2);
        let mut left = vec![0.25f32; 256];
        p.process(&mut [&mut left[..]], 256);
        assert!(left.iter().all(|x| x.is_finite()));
    }

    #[test]
    fn every_pin_is_usable() {
        let plugin = TubeChannel::default();
        let errors = openplugin::core::identity::check_pins(&plugin);
        assert!(errors.is_empty(), "{errors:?}");
        assert!(plugin.vst3_ids().is_some_and(|ids| ids.is_valid()));
        assert!(plugin.aax_ids().is_some_and(|ids| ids.is_valid()));
    }

    #[test]
    fn a_refused_state_changes_nothing() {
        let mut a = TubeChannel::default();
        a.set_param("gain", 0.4);
        let before = *a.values();
        assert!(!a.load_state(&[]));
        assert!(!a.load_state(b"not a state"));
        assert_eq!(a.values(), &before);
    }

    #[test]
    fn programs_set_channel_a_only() {
        let p = TubeChannel::default();
        let names: Vec<_> = (0..p.preset_count()).map(|i| p.preset_name(i)).collect();
        assert_eq!(names, ["Init", "Vocal, warm and close", "Bass guitar, DI", "Drum bus, glue", "Tube drive, plate low", "Limiter, infinity"]);
        for i in 0..p.preset_count() {
            for (v, info) in p.preset_values(i).unwrap().iter().zip(PARAMS.iter()) {
                assert_eq!(*v, info.clamp(*v), "{} in {}", info.id, p.preset_name(i));
            }
        }
        assert!(PARAMS[..params::PER_CHANNEL].iter().all(|p| !p.flags.contains(ParamFlags::PRESET_EXCLUDE)));
        assert!(PARAMS[params::PER_CHANNEL..].iter().all(|p| p.flags.contains(ParamFlags::PRESET_EXCLUDE)));
    }
}
