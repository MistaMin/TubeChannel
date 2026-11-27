//! The C++ chain (Source/Plugin/Chain.h) behind the C ABI of cpp/bridge.h, and the prepared state
//! the JUCE processor swapped between (`PreparedState`): a playback chain, an optional offline bounce
//! chain, and the delay that pads each to one constant latency.
//!
//! A chain takes up to a few hundred milliseconds to build, so a structural change (quality,
//! oversampling, phase, tolerances, seed, offline bounce) is built by a [`Builder`] thread and
//! handed to the audio thread through two lock-free queues: finished states in, retired states out.
//! The audio thread never allocates, frees or waits for one.

use std::ptr::NonNull;
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::Arc;
use std::thread::JoinHandle;
use std::time::Duration;

use openplugin::core::spsc;

use crate::params::{idx, COUNT};

#[repr(C)]
struct TcChain {
    _opaque: [u8; 0],
}
#[repr(C)]
struct TcTelemetry {
    _opaque: [u8; 0],
}

#[repr(C)]
#[derive(Clone, Copy)]
struct TcChainConfig {
    sample_rate: f64,
    max_block: i32,
    channels: i32,
    quality: i32,
    oversampling_stages: i32,
    linear_phase: i32,
    tolerances: i32,
    seed: i32,
    mains_hz: f64,
    mains_scale: f64,
}

pub const SCOPE: usize = 256;

/// One channel's telemetry, copied for the editor (`TcMeters`).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Meters {
    pub out_peak: f32,
    pub gr_db: f32,
    pub tube_drive: f32,
    pub clip: f32,
    pub clip_pre: f32,
    pub in_peak: f32,
    pub out_rms: f32,
    pub p15: f32,
    pub n15: f32,
    pub p25: f32,
    pub p45: f32,
    pub p75: f32,
    pub p150: f32,
    pub tube_p: f32,
    pub mains_phase: f32,
    pub heater_current: f32,
    pub load_p15: f32,
    pub load_tube: f32,
    pub vu_tap: [f32; 4],
    pub scope25: [f32; SCOPE],
    pub scope150: [f32; SCOPE],
    pub scope_load: [f32; SCOPE],
    pub scope_write: i32,
}

extern "C" {
    fn tc_chain_create(config: *const TcChainConfig) -> *mut TcChain;
    fn tc_chain_destroy(chain: *mut TcChain);
    fn tc_chain_latency(chain: *const TcChain) -> i32;
    fn tc_chain_factor(chain: *const TcChain) -> i32;
    fn tc_chain_channels(chain: *const TcChain) -> i32;
    fn tc_chain_compact_detector(chain: *const TcChain) -> i32;
    fn tc_linear_reference_latency(sample_rate: f64) -> i32;
    fn tc_max_latency(sample_rate: f64) -> i32;
    fn tc_oversampler_latency(sample_rate: f64, quality: i32, stages: i32, linear: i32) -> i32;
    fn tc_chain_process(chain: *mut TcChain, io: *const *mut f32, frames: i32, sc: *const *const f32, nsc: i32, values: *const f32, a: *mut TcTelemetry, b: *mut TcTelemetry);
    fn tc_param_count() -> i32;
    fn tc_telemetry_create() -> *mut TcTelemetry;
    fn tc_telemetry_destroy(t: *mut TcTelemetry);
    fn tc_telemetry_read(t: *const TcTelemetry, out: *mut Meters);
}

/// The largest latency any setting can report at `sample_rate`.
pub fn max_latency(sample_rate: f64) -> usize {
    unsafe { tc_max_latency(sample_rate) }.max(0) as usize
}

/// Both channels' telemetry, written by the audio thread and read by the editor (atomics inside).
pub struct Telemetry([NonNull<TcTelemetry>; 2]);

// Every field is a std::atomic: any thread may read while the audio thread writes.
unsafe impl Send for Telemetry {}
unsafe impl Sync for Telemetry {}

impl Telemetry {
    pub fn new() -> Option<Self> {
        let a = NonNull::new(unsafe { tc_telemetry_create() })?;
        let Some(b) = NonNull::new(unsafe { tc_telemetry_create() }) else {
            unsafe { tc_telemetry_destroy(a.as_ptr()) };
            return None;
        };
        Some(Self([a, b]))
    }

    /// A copy of channel `ch` (0 or 1).
    pub fn read(&self, ch: usize) -> Box<Meters> {
        let mut m = Box::new(std::mem::MaybeUninit::<Meters>::uninit());
        unsafe {
            tc_telemetry_read(self.0[ch.min(1)].as_ptr(), m.as_mut_ptr());
            Box::from_raw(Box::into_raw(m).cast())
        }
    }
}

impl Drop for Telemetry {
    fn drop(&mut self) {
        for t in self.0 {
            unsafe { tc_telemetry_destroy(t.as_ptr()) }
        }
    }
}

/// One built chain.
pub struct Chain(NonNull<TcChain>);

// The chain has no thread affinity and no globals written after load.
unsafe impl Send for Chain {}

impl Chain {
    pub fn latency(&self) -> usize {
        unsafe { tc_chain_latency(self.0.as_ptr()) }.max(0) as usize
    }
    pub fn factor(&self) -> u32 {
        unsafe { tc_chain_factor(self.0.as_ptr()) }.max(1) as u32
    }
    pub fn channels(&self) -> usize {
        unsafe { tc_chain_channels(self.0.as_ptr()) }.max(1) as usize
    }
    pub fn compact_detector(&self) -> bool {
        unsafe { tc_chain_compact_detector(self.0.as_ptr()) == 1 }
    }

    /// In place over the chain's channels of `io`; `sc` is the sidechain (up to two channels).
    fn process(&mut self, io: &mut [&mut [f32]], frames: usize, sc: &[&[f32]], values: &[f32; COUNT], telemetry: &Telemetry) {
        // The C++ chain reads every channel it was built for: fewer buffers would hand it a null pointer.
        if io.len() < self.channels() || io.iter().any(|c| c.len() < frames) {
            return;
        }
        let channels = self.channels();
        let mut io_ptrs = [std::ptr::null_mut::<f32>(); 2];
        for (ptr, channel) in io_ptrs.iter_mut().zip(io.iter_mut()).take(channels) {
            *ptr = channel.as_mut_ptr();
        }
        let mut sc_ptrs = [std::ptr::null::<f32>(); 2];
        let mut nsc = 0;
        for (ptr, channel) in sc_ptrs.iter_mut().zip(sc) {
            if channel.len() < frames {
                break;
            }
            *ptr = channel.as_ptr();
            nsc += 1;
        }
        unsafe {
            tc_chain_process(self.0.as_ptr(), io_ptrs.as_ptr(), frames as i32, if nsc > 0 { sc_ptrs.as_ptr() } else { std::ptr::null() }, nsc, values.as_ptr(), telemetry.0[0].as_ptr(), telemetry.0[1].as_ptr());
        }
    }
}

impl Drop for Chain {
    fn drop(&mut self) {
        unsafe { tc_chain_destroy(self.0.as_ptr()) }
    }
}

/// What one chain is built for, from the parameters (`TubeChannelProcessor::Key` without the
/// sample rate, block size and channels, which only `prepare` changes).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Key {
    pub quality: i32,
    pub stages: i32,
    pub linear: bool,
    pub tolerances: bool,
    pub seed: i32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct StateKey {
    pub playback: Key,
    pub bounce: Key,
    pub bounce_enabled: bool,
}

fn choice(v: f32) -> i32 {
    v.round() as i32
}

impl StateKey {
    /// `TubeChannelProcessor::requestedStateKey`.
    pub fn of(v: &[f32; COUNT]) -> Self {
        let playback = Key {
            quality: if v[idx::ECO] > 0.5 { 3 } else { choice(v[idx::QUALITY]).clamp(0, 2) },
            stages: choice(v[idx::OVERSAMPLING]).clamp(0, 4) - 1,
            linear: v[idx::LINEAR_PHASE] > 0.5,
            tolerances: v[idx::TOLERANCES] > 0.5,
            seed: choice(v[idx::SEED]).clamp(0, 99),
        };
        let bounce_enabled = v[idx::OFFLINE_BOUNCE_ENABLED] > 0.5;
        let mut bounce = playback;
        if bounce_enabled {
            let c = choice(v[idx::OFFLINE_BOUNCE]).clamp(0, 8);
            bounce.quality = [3, 0, 1][(c / 3) as usize]; // Eco, Circuit, HQ
            bounce.stages = c % 3 + 1; // 2x, 4x, 8x
        }
        Self { playback, bounce, bounce_enabled }
    }

    /// Packed into 29 bits, for the builder's atomic.
    pub fn code(self) -> u32 {
        let k = |k: Key| (k.quality as u32 & 3) | (((k.stages + 1) as u32 & 7) << 2) | (u32::from(k.linear) << 5) | (u32::from(k.tolerances) << 6) | ((k.seed as u32 & 127) << 7);
        k(self.playback) | (k(self.bounce) << 14) | (u32::from(self.bounce_enabled) << 28)
    }

    pub fn decode(code: u32) -> Self {
        let k = |c: u32| Key { quality: (c & 3) as i32, stages: ((c >> 2) & 7) as i32 - 1, linear: c & 32 != 0, tolerances: c & 64 != 0, seed: ((c >> 7) & 127) as i32 };
        Self { playback: k(code & 0x3fff), bounce: k((code >> 14) & 0x3fff), bounce_enabled: code & (1 << 28) != 0 }
    }
}

/// What `prepare` fixed: the session's rate, block size and channels, and the mains the supplies start at.
#[derive(Clone, Copy, Debug)]
pub struct Setup {
    pub sample_rate: f64,
    pub max_block: usize,
    pub channels: usize,
    pub mains_hz: f64,
    pub mains_scale: f64,
}

impl Setup {
    pub fn new(sample_rate: f64, max_block: usize, channels: usize, v: &[f32; COUNT]) -> Self {
        Self { sample_rate, max_block, channels: channels.clamp(1, 2), mains_hz: if v[idx::MAINS] > 0.5 { 50.0 } else { 60.0 }, mains_scale: f64::from(v[idx::MAINS_VOLTS]) }
    }

    fn chain(&self, k: Key) -> Option<Chain> {
        let c = TcChainConfig {
            sample_rate: self.sample_rate,
            max_block: self.max_block.clamp(1, i32::MAX as usize) as i32,
            channels: self.channels as i32,
            quality: k.quality,
            oversampling_stages: k.stages,
            linear_phase: i32::from(k.linear),
            tolerances: i32::from(k.tolerances),
            seed: k.seed,
            mains_hz: self.mains_hz,
            mains_scale: self.mains_scale,
        };
        NonNull::new(unsafe { tc_chain_create(&c) }).map(Chain)
    }
}

/// Every setting's latency at one sample rate, so a parameter change reports the latency of the
/// state it asks for at once (the conformance kit and hosts expect it with the change), before the
/// builder has made that state. Built in `prepare`; looking up allocates nothing.
pub struct Latencies {
    /// [quality][stages + 1][linear]
    table: [[[usize; 2]; 5]; 4],
    linear_reference: usize,
}

impl Latencies {
    pub fn new(sample_rate: f64) -> Self {
        let mut table = [[[0; 2]; 5]; 4];
        for (q, row) in table.iter_mut().enumerate() {
            for (s, pair) in row.iter_mut().enumerate() {
                for (l, v) in pair.iter_mut().enumerate() {
                    *v = unsafe { tc_oversampler_latency(sample_rate, q as i32, s as i32 - 1, l as i32) }.max(0) as usize;
                }
            }
        }
        Self { table, linear_reference: unsafe { tc_linear_reference_latency(sample_rate) }.max(0) as usize }
    }

    fn chain(&self, k: Key) -> usize {
        self.table[k.quality.clamp(0, 3) as usize][(k.stages + 1).clamp(0, 4) as usize][usize::from(k.linear)]
    }

    /// The latency [`State::build`] gives a state for `key`.
    pub fn of(&self, key: StateKey) -> usize {
        let mut latency = self.chain(key.playback);
        if key.bounce_enabled {
            latency = latency.max(self.chain(key.bounce));
            if key.playback.linear {
                latency = latency.max(self.linear_reference);
            }
        }
        latency
    }
}

/// A delay of whole samples (`TubeChannelProcessor::Padding`).
#[derive(Default)]
pub struct Padding {
    samples: [Vec<f32>; 2],
    position: usize,
}

impl Padding {
    fn new(delay: usize) -> Self {
        Self { samples: [vec![0.0; delay], vec![0.0; delay]], position: 0 }
    }

    fn delay(&self) -> usize {
        self.samples[0].len()
    }

    fn process(&mut self, io: &mut [&mut [f32]], frames: usize) {
        let delay = self.delay();
        if delay == 0 {
            return;
        }
        for i in 0..frames {
            for (ch, channel) in io.iter_mut().take(2).enumerate() {
                std::mem::swap(&mut channel[i], &mut self.samples[ch][self.position]);
            }
            self.position += 1;
            if self.position == delay {
                self.position = 0;
            }
        }
    }
}

/// A playback chain, the offline bounce chain when it is enabled, and the padding that gives both
/// the same latency (`TubeChannelProcessor::PreparedState`).
pub struct State {
    pub key: StateKey,
    playback: Chain,
    bounce: Option<Chain>,
    playback_padding: Padding,
    bounce_padding: Padding,
    pub latency: usize,
}

impl State {
    /// Allocates and takes up to a few hundred milliseconds: never on the audio thread while playing.
    pub fn build(key: StateKey, setup: &Setup) -> Option<Self> {
        let playback = setup.chain(key.playback)?;
        let mut latency = playback.latency();
        let bounce = if key.bounce_enabled {
            let bounce = setup.chain(key.bounce)?;
            latency = latency.max(bounce.latency());
            if key.playback.linear {
                // A constant budget across all nine bounce choices avoids PDC changes at render entry.
                latency = latency.max(unsafe { tc_linear_reference_latency(setup.sample_rate) }.max(0) as usize);
            }
            Some(bounce)
        } else {
            None
        };
        let playback_padding = Padding::new(latency - playback.latency());
        let bounce_padding = Padding::new(bounce.as_ref().map_or(0, |b| latency - b.latency()));
        Some(Self { key, playback, bounce, playback_padding, bounce_padding, latency })
    }

    /// Takes over the chains of `previous` that this state was built the same for, so a bounce-only
    /// edit does not reset the working engine or its delay history. No allocation: pointers swap.
    pub fn preserve(&mut self, previous: &mut State, bounce: bool) {
        if self.key.playback == previous.key.playback {
            std::mem::swap(&mut self.playback, &mut previous.playback);
            if self.latency == previous.latency {
                std::mem::swap(&mut self.playback_padding, &mut previous.playback_padding);
            }
        }
        if bounce && self.key.bounce_enabled && previous.key.bounce_enabled && self.key.bounce == previous.key.bounce {
            if let (Some(a), Some(b)) = (self.bounce.as_mut(), previous.bounce.as_mut()) {
                std::mem::swap(a, b);
                if self.latency == previous.latency {
                    std::mem::swap(&mut self.bounce_padding, &mut previous.bounce_padding);
                }
            }
        }
    }

    /// The chain a render in this mode runs.
    pub fn engine(&self, offline: bool) -> &Chain {
        match (&self.bounce, offline) {
            (Some(b), true) => b,
            _ => &self.playback,
        }
    }

    /// Processes and pads one block; the result is delayed by exactly [`State::latency`].
    pub fn process(&mut self, io: &mut [&mut [f32]], frames: usize, sc: &[&[f32]], values: &[f32; COUNT], telemetry: &Telemetry, offline: bool) {
        let (chain, padding) = match (&mut self.bounce, offline) {
            (Some(b), true) => (b, &mut self.bounce_padding),
            _ => (&mut self.playback, &mut self.playback_padding),
        };
        let channels = chain.channels().min(io.len());
        chain.process(io, frames, sc, values, telemetry);
        padding.process(&mut io[..channels], frames);
    }
}

/// Builds states off the audio thread. The audio thread asks with [`Builder::request`] (an atomic
/// store) and collects with [`Builder::take`]; retired states go back to this thread to be freed.
///
/// The builder also keeps one spare state built for the current settings, so [`Builder::take_spare`]
/// gives `reset` a freshly built circuit (the chain has no reset of its own) without allocating.
pub struct Builder {
    requested: Arc<AtomicU32>,
    stop: Arc<AtomicBool>,
    ready: spsc::Consumer<State>,
    spares: spsc::Consumer<State>,
    /// Spares taken out of `spares`, so the thread knows when its queue is empty.
    spares_popped: Arc<AtomicU32>,
    /// The key code of the spare the audio side holds (`NONE` for none).
    spare_held: Arc<AtomicU32>,
    held: Option<State>,
    retired: spsc::Producer<State>,
    /// Retired states the return queue had no room for yet.
    stalled: [Option<State>; 2],
    thread: Option<JoinHandle<()>>,
}

const NONE: u32 = u32::MAX;

impl Builder {
    /// Starts the thread. `built` is the key of the state the audio thread already has, `spare` a
    /// second state built for it, if there is one.
    pub fn start(setup: Setup, built: StateKey, spare: Option<State>) -> Self {
        let requested = Arc::new(AtomicU32::new(built.code()));
        let stop = Arc::new(AtomicBool::new(false));
        let spares_popped = Arc::new(AtomicU32::new(0));
        let spare_held = Arc::new(AtomicU32::new(spare.as_ref().map_or(NONE, |s| s.key.code())));
        let (mut ready_tx, ready) = spsc::channel::<State>(2);
        let (mut spare_tx, spares) = spsc::channel::<State>(1);
        let (retired, mut retired_rx) = spsc::channel::<State>(8);
        let (want, quit, popped, held) = (requested.clone(), stop.clone(), spares_popped.clone(), spare_held.clone());
        let thread = std::thread::Builder::new()
            .name("tube-channel-builder".into())
            .spawn(move || {
                let mut last = built.code();
                let mut waiting: Option<State> = None;
                let mut pushed = 0u32;
                while !quit.load(Ordering::Acquire) {
                    while retired_rx.pop().is_some() {}
                    if let Some(state) = waiting.take() {
                        if let Err(state) = ready_tx.push(state) {
                            waiting = Some(state);
                        }
                    }
                    let code = want.load(Ordering::Acquire);
                    if waiting.is_none() && code != last {
                        // A failed build keeps the running state and is retried on the next pass.
                        if let Some(state) = State::build(StateKey::decode(code), &setup) {
                            last = code;
                            // Another edit while building: build that instead of handing over a stale state.
                            if want.load(Ordering::Acquire) == code {
                                waiting = Some(state);
                                continue;
                            }
                        }
                    }
                    // The live state first, then the spare: none queued, and the held one is not for these settings.
                    if waiting.is_none() && pushed == popped.load(Ordering::Acquire) && held.load(Ordering::Acquire) != code {
                        if let Some(state) = State::build(StateKey::decode(code), &setup) {
                            if spare_tx.push(state).is_ok() {
                                pushed = pushed.wrapping_add(1);
                            }
                            continue;
                        }
                    }
                    std::thread::sleep(Duration::from_millis(5));
                }
            })
            .expect("could not start the Tube Channel builder thread");
        Self { requested, stop, ready, spares, spares_popped, spare_held, held: spare, retired, stalled: [None, None], thread: Some(thread) }
    }

    /// Audio thread: asks for a state built for `key`.
    pub fn request(&self, key: StateKey) {
        self.requested.store(key.code(), Ordering::Release);
    }

    /// Audio thread: a finished state, if one is waiting. Does not lock, allocate or free.
    pub fn take(&mut self) -> Option<State> {
        if !self.flush() {
            return None; // hand the old ones back before taking another
        }
        self.ready.pop()
    }

    /// Audio thread, once per block: keeps the spare in step with the settings. Does not lock,
    /// allocate or free.
    pub fn refresh_spare(&mut self, wanted: StateKey) {
        if self.held.as_ref().is_some_and(|s| s.key != wanted) && self.can_retire() {
            if let Some(stale) = self.held.take() {
                self.retire(stale);
            }
            self.spare_held.store(NONE, Ordering::Release);
        }
        if self.held.is_none() && self.can_retire() {
            if let Some(spare) = self.spares.pop() {
                self.spares_popped.fetch_add(1, Ordering::AcqRel);
                if spare.key == wanted {
                    self.spare_held.store(spare.key.code(), Ordering::Release);
                    self.held = Some(spare);
                } else {
                    self.retire(spare);
                }
            }
        }
    }

    /// Audio thread: a freshly built state for `wanted`, if the spare is ready.
    pub fn take_spare(&mut self, wanted: StateKey) -> Option<State> {
        self.refresh_spare(wanted);
        let spare = self.held.take()?;
        self.spare_held.store(NONE, Ordering::Release);
        Some(spare)
    }

    /// Whether a state can be retired without freeing it here.
    pub fn can_retire(&mut self) -> bool {
        self.flush();
        self.stalled.iter().any(Option::is_none)
    }

    /// Audio thread: hands a state back to be freed on the builder thread. Call only after
    /// [`Builder::can_retire`] said yes, or the state is freed here.
    pub fn retire(&mut self, state: State) {
        if let Err(state) = self.retired.push(state) {
            if let Some(slot) = self.stalled.iter_mut().find(|s| s.is_none()) {
                *slot = Some(state);
            }
            // shortcut: with both stall slots full the state is freed on this thread; the builder
            // drains the queue every 5 ms, so that needs more than ten swaps in one block.
        }
    }

    /// Hands stalled states back; true when none is left waiting.
    fn flush(&mut self) -> bool {
        for i in 0..self.stalled.len() {
            if let Some(state) = self.stalled[i].take() {
                if let Err(state) = self.retired.push(state) {
                    self.stalled[i] = Some(state);
                }
            }
        }
        self.stalled.iter().all(Option::is_none)
    }
}

impl Drop for Builder {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Release);
        if let Some(thread) = self.thread.take() {
            let _ = thread.join();
        }
    }
}

/// Checks that cpp/bridge.cpp and src/params.rs agree on the table.
pub fn table_matches() -> bool {
    (unsafe { tc_param_count() }) == COUNT as i32
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::params::defaults;

    #[test]
    fn keys_pack_and_unpack() {
        let mut v = defaults();
        for (eco, quality, os, linear, seed, bounce, enabled) in [(1.0, 0.0, 1.0, 0.0, 1.0, 8.0, 1.0), (0.0, 2.0, 0.0, 1.0, 99.0, 0.0, 0.0), (0.0, 1.0, 4.0, 1.0, 37.0, 4.0, 1.0)] {
            v[idx::ECO] = eco;
            v[idx::QUALITY] = quality;
            v[idx::OVERSAMPLING] = os;
            v[idx::LINEAR_PHASE] = linear;
            v[idx::SEED] = seed;
            v[idx::OFFLINE_BOUNCE] = bounce;
            v[idx::OFFLINE_BOUNCE_ENABLED] = enabled;
            let k = StateKey::of(&v);
            assert_eq!(StateKey::decode(k.code()), k);
        }
        assert!(table_matches());
    }

    #[test]
    fn the_latency_table_predicts_every_built_state() {
        let latencies = Latencies::new(96_000.0);
        let mut v = defaults();
        let setup = Setup::new(96_000.0, 256, 2, &v);
        for eco in [0.0, 1.0] {
            for quality in [0.0, 1.0, 2.0] {
                for os in [0.0, 1.0, 2.0, 3.0, 4.0] {
                    for linear in [0.0, 1.0] {
                        for (bounce, enabled) in [(0.0, 0.0), (2.0, 1.0), (7.0, 1.0)] {
                            v[idx::ECO] = eco;
                            v[idx::QUALITY] = quality;
                            v[idx::OVERSAMPLING] = os;
                            v[idx::LINEAR_PHASE] = linear;
                            v[idx::OFFLINE_BOUNCE] = bounce;
                            v[idx::OFFLINE_BOUNCE_ENABLED] = enabled;
                            let key = StateKey::of(&v);
                            let state = State::build(key, &setup).expect("a state");
                            assert_eq!(state.latency, latencies.of(key), "{key:?}");
                        }
                    }
                }
            }
        }
    }
}
