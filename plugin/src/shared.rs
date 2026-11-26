//! What the engine shares with the editor: the chain's telemetry, the final output's meters, the
//! analyser ring and the active engine, plus the editor's skin, which is saved with the session.

use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU8, Ordering::Relaxed};
use std::sync::Mutex;

use openplugin::dri::goodlookindri::spectrum::SampleRing;

use crate::engine::Telemetry;

/// An `f32` in an atomic.
#[derive(Default)]
pub struct AtomicF32(AtomicU32);

impl AtomicF32 {
    pub fn get(&self) -> f32 {
        f32::from_bits(self.0.load(Relaxed))
    }
    pub fn set(&self, v: f32) {
        self.0.store(v.to_bits(), Relaxed);
    }
    /// Keep the larger of the stored value and `v` (both zero or more).
    pub fn raise(&self, v: f32) {
        // Non-negative floats order like their bits.
        self.0.fetch_max(v.max(0.0).to_bits(), Relaxed);
    }
    /// The value since the last take, which becomes zero.
    pub fn take(&self) -> f32 {
        f32::from_bits(self.0.swap(0, Relaxed))
    }
}

pub struct Shared {
    /// The chain's per-channel telemetry (channel A, channel B).
    pub telemetry: Option<Telemetry>,
    /// Final output peaks since the editor last took them, after trims, fades, Autogain and Delta.
    pub output_peak: [AtomicF32; 2],
    pub output_clip: [AtomicBool; 2],
    /// The output for the editor's analyser (wait-free; the FFT runs in the editor).
    pub spectrum: SampleRing,
    pub sample_rate: AtomicF32,
    /// The engine that ran the last block.
    pub channels: AtomicU32,
    pub factor: AtomicU32,
    pub quality: AtomicU32,
    pub latency: AtomicU32,
    pub prototype_compressor: AtomicBool,
    pub offline_bounce: AtomicBool,
    /// A structural change is waiting for its chain.
    pub building: AtomicBool,
    /// Editor skin: 0 analogue, 1 digital. Saved with the session.
    pub skin: AtomicU8,
    /// The System page is open; the meter's source (0 PRE, 1 COMP, 2 EQ, 3 OUT). Saved with the session.
    pub system_page: AtomicBool,
    pub vu_source: AtomicU8,
    /// The factory program last chosen in the editor.
    pub program: AtomicU8,
    /// A/B comparison: B is shown, and the other set of values (the editor's thread only, never audio).
    pub ab_is_b: AtomicBool,
    other: Mutex<Option<Vec<f32>>>,
}

impl Default for Shared {
    fn default() -> Self {
        Self {
            telemetry: Telemetry::new(),
            output_peak: Default::default(),
            output_clip: Default::default(),
            spectrum: SampleRing::default(),
            sample_rate: AtomicF32(AtomicU32::new(48_000f32.to_bits())),
            channels: AtomicU32::new(2),
            factor: AtomicU32::new(1),
            quality: AtomicU32::new(3),
            latency: AtomicU32::new(0),
            prototype_compressor: AtomicBool::new(true),
            offline_bounce: AtomicBool::new(false),
            building: AtomicBool::new(false),
            skin: AtomicU8::new(0),
            system_page: AtomicBool::new(false),
            vu_source: AtomicU8::new(3),
            program: AtomicU8::new(0),
            ab_is_b: AtomicBool::new(false),
            other: Mutex::new(None),
        }
    }
}

/// What the editor shows, saved with the session (the release kept `skin`, `vuSrc` and `sysPage`
/// in its state).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct View {
    pub skin: u8,
    pub vu_source: u8,
    pub system_page: bool,
}

impl Default for View {
    fn default() -> Self {
        Self { skin: 0, vu_source: 3, system_page: false }
    }
}

impl Shared {
    pub fn view(&self) -> View {
        View { skin: self.skin(), vu_source: self.vu_source.load(Relaxed), system_page: self.system_page.load(Relaxed) }
    }
    pub fn set_view(&self, v: View) {
        self.set_skin(v.skin);
        self.vu_source.store(v.vu_source.min(3), Relaxed);
        self.system_page.store(v.system_page, Relaxed);
    }
    pub fn skin(&self) -> u8 {
        self.skin.load(Relaxed)
    }
    pub fn set_skin(&self, skin: u8) {
        self.skin.store(skin.min(1), Relaxed);
    }
    /// The other of A and B, taken.
    pub fn take_other(&self) -> Option<Vec<f32>> {
        self.other.lock().ok().and_then(|mut o| o.take())
    }
    pub fn set_other(&self, values: Vec<f32>) {
        if let Ok(mut o) = self.other.lock() {
            *o = Some(values);
        }
    }
}
