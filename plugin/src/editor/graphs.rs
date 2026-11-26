//! The digital skin's two displays (Source/UI/Digital.h): a spectrum with an editable EQ curve, and
//! the compressor's transfer curve with threshold, ratio and make-up handles. They set the same
//! parameters as the knobs. Geometry is in editor units.

use std::f64::consts::PI;

use openplugin::dri::goodlookindri::spectrum::{Analyser, SampleRing};

use super::panel::*;
use crate::params::{eq_gain_db, hm_freq_hz, lm_freq_hz, ratio_value, threshold_dbm, COUNT};

pub type Edit = (usize, f32);

// ---- equaliser --------------------------------------------------------------------------------------------

/// RBJ biquad magnitude, for drawing the curve only (the audio is the circuit, not these filters).
fn biquad_db(b: [f64; 3], a: [f64; 3], w: f64) -> f64 {
    let (c1, s1, c2, s2) = (w.cos(), -w.sin(), (2.0 * w).cos(), -(2.0 * w).sin());
    let num = (b[0] + b[1] * c1 + b[2] * c2, b[1] * s1 + b[2] * s2);
    let den = (a[0] + a[1] * c1 + a[2] * c2, a[1] * s1 + a[2] * s2);
    10.0 * ((num.0 * num.0 + num.1 * num.1) / (den.0 * den.0 + den.1 * den.1)).max(1e-18).log10()
}

fn peak_db(f: f64, g: f64, q: f64, fq: f64) -> f64 {
    let fs = 48_000.0;
    let (a, w0) = (10f64.powf(g / 40.0), 2.0 * PI * f / fs);
    let (al, c) = (w0.sin() / (2.0 * q), w0.cos());
    biquad_db([1.0 + al * a, -2.0 * c, 1.0 - al * a], [1.0 + al / a, -2.0 * c, 1.0 - al / a], 2.0 * PI * fq / fs)
}

fn shelf_db(low: bool, f: f64, g: f64, fq: f64) -> f64 {
    let fs = 48_000.0;
    let (a, w0) = (10f64.powf(g / 40.0), 2.0 * PI * f / fs);
    let (c, sn) = (w0.cos(), w0.sin());
    let tsa = 2.0 * a.sqrt() * sn / 2.0 * 2f64.sqrt();
    let w = 2.0 * PI * fq / fs;
    if low {
        biquad_db(
            [a * ((a + 1.0) - (a - 1.0) * c + tsa), 2.0 * a * ((a - 1.0) - (a + 1.0) * c), a * ((a + 1.0) - (a - 1.0) * c - tsa)],
            [(a + 1.0) + (a - 1.0) * c + tsa, -2.0 * ((a - 1.0) + (a + 1.0) * c), (a + 1.0) + (a - 1.0) * c - tsa],
            w,
        )
    } else {
        biquad_db(
            [a * ((a + 1.0) + (a - 1.0) * c + tsa), -2.0 * a * ((a - 1.0) + (a + 1.0) * c), a * ((a + 1.0) + (a - 1.0) * c - tsa)],
            [(a + 1.0) - (a - 1.0) * c + tsa, 2.0 * ((a - 1.0) - (a + 1.0) * c), (a + 1.0) - (a - 1.0) * c - tsa],
            w,
        )
    }
}

pub struct Bands {
    pub low_f: f64,
    pub low_g: f64,
    pub high_f: f64,
    pub high_g: f64,
    pub lm_f: f64,
    pub lm_g: f64,
    pub lm_q: f64,
    pub hm_f: f64,
    pub hm_g: f64,
    pub hm_q: f64,
}

impl Bands {
    pub fn of(v: &[f32; COUNT], ch: usize) -> Self {
        let g = |i: usize| f64::from(v[i + ch]);
        let on = |i: usize| v[i + ch] > 0.5;
        Self {
            low_f: if on(LOSHIFT) { 40.0 } else { 120.0 },
            low_g: eq_gain_db(g(LOW)),
            high_f: if on(HISHIFT) { 6000.0 } else { 18000.0 },
            high_g: eq_gain_db(g(HIGH)),
            lm_f: lm_freq_hz(g(LMFREQ), on(LMX10)),
            lm_g: eq_gain_db(g(LMLEVEL)),
            lm_q: if on(LMNARROW) { 2.5 } else { 0.7 },
            hm_f: hm_freq_hz(g(HMFREQ), on(HMX10)),
            hm_g: eq_gain_db(g(HMLEVEL)),
            hm_q: if on(HMNARROW) { 2.5 } else { 0.7 },
        }
    }

    pub fn response_db(&self, f: f64) -> f64 {
        shelf_db(true, self.low_f, self.low_g, f) + shelf_db(false, self.high_f, self.high_g, f) + peak_db(self.lm_f, self.lm_g, self.lm_q, f) + peak_db(self.hm_f, self.hm_g, self.hm_q, f)
    }

    /// (Hz, dB) of LOW, LO-MID, HI-MID, HIGH.
    pub fn handle(&self, i: usize) -> (f64, f64) {
        match i {
            0 => (self.low_f, self.low_g),
            1 => (self.lm_f, self.lm_g),
            2 => (self.hm_f, self.hm_g),
            _ => (self.high_f, self.high_g),
        }
    }
}

pub const BAND_NAMES: [&str; 4] = ["LOW", "LO-MID", "HI-MID", "HIGH"];
pub const BAND_COLOURS: [u32; 4] = [0xffff_6a5a, 0xff5a_e07a, 0xff5a_b4ff, 0xffff_c04a];

/// The EQ display's plot area and its scales.
#[derive(Clone, Copy)]
pub struct EqPlot(pub R);

impl EqPlot {
    pub fn of(widget: R) -> Self {
        Self((widget.0 + 8.0, widget.1 + 8.0, widget.2 - 16.0, widget.3 - 16.0))
    }
    pub fn x(&self, f: f64) -> f32 {
        self.0 .0 + ((f / 20.0).ln() / 1000f64.ln()) as f32 * self.0 .2
    }
    pub fn f(&self, x: f32) -> f64 {
        20.0 * 1000f64.powf(f64::from((x - self.0 .0) / self.0 .2))
    }
    pub fn y(&self, db: f64) -> f32 {
        self.0 .1 + self.0 .3 * 0.5 - (db / 12.0) as f32 * self.0 .3 * 0.36
    }
    pub fn db(&self, y: f32) -> f64 {
        f64::from((self.0 .1 + self.0 .3 * 0.5 - y) / (self.0 .3 * 0.36)) * 12.0
    }
}

/// The handle within 20 units of (x, y), if any.
pub fn eq_hit(plot: &EqPlot, bands: &Bands, x: f32, y: f32) -> Option<usize> {
    (0..4)
        .map(|i| {
            let (f, g) = bands.handle(i);
            (i, (plot.x(f) - x).hypot(plot.y(g) - y))
        })
        .filter(|(_, d)| *d < 20.0)
        .min_by(|a, b| a.1.total_cmp(&b.1))
        .map(|(i, _)| i)
}

/// The edits a drag of `handle` to (x, y) makes (`EqDisplay::mouseDrag`).
pub fn eq_drag(plot: &EqPlot, v: &[f32; COUNT], ch: usize, handle: usize, x: f32, y: f32) -> Vec<Edit> {
    let f = plot.f(x);
    let level = ((plot.db(y).clamp(-12.0, 12.0)) / 24.0 + 0.5) as f32;
    let flag = |b: bool| if b { 1.0 } else { 0.0 };
    match handle {
        0 => vec![(LOSHIFT + ch, flag(f < 70.0)), (LOW + ch, level)],
        3 => vec![(HISHIFT + ch, flag(f < 10500.0)), (HIGH + ch, level)],
        1 | 2 => {
            let (x10_i, freq_i, level_i, bounds) = if handle == 1 { (LMX10, LMFREQ, LMLEVEL, [(16.0, 214.0), (178.0, 2350.0)]) } else { (HMX10, HMFREQ, HMLEVEL, [(162.0, 2100.0), (1800.0, 23000.0)]) };
            let mut x10 = v[x10_i + ch] > 0.5;
            if f > bounds[0].1 {
                x10 = true;
            } else if f < bounds[1].0 {
                x10 = false;
            }
            let (lo, hi) = bounds[usize::from(x10)];
            let k = ((f.clamp(lo, hi) / lo).ln() / (hi / lo).ln()) as f32;
            vec![(x10_i + ch, flag(x10)), (freq_i + ch, k), (level_i + ch, level)]
        }
        _ => Vec::new(),
    }
}

/// A double-click on a handle: a mid band flips narrow and wide; every band goes to 0 dB.
pub fn eq_double_click(v: &[f32; COUNT], ch: usize, handle: usize) -> Vec<Edit> {
    let mut out = Vec::new();
    match handle {
        1 => out.push((LMNARROW + ch, if v[LMNARROW + ch] > 0.5 { 0.0 } else { 1.0 })),
        2 => out.push((HMNARROW + ch, if v[HMNARROW + ch] > 0.5 { 0.0 } else { 1.0 })),
        _ => {}
    }
    out.push(([LOW, LMLEVEL, HMLEVEL, HIGH][handle.min(3)] + ch, 0.5));
    out
}

/// The output spectrum, analysed in the editor from the engine's sample ring.
pub struct Spectrum {
    pub analyser: Analyser,
}

impl Default for Spectrum {
    fn default() -> Self {
        let mut analyser = Analyser::default();
        analyser.configure(2048, 48_000.0);
        Self { analyser }
    }
}

impl Spectrum {
    pub fn update(&mut self, ring: &SampleRing, sample_rate: f64) {
        self.analyser.set_sample_rate(sample_rate.max(8000.0));
        self.analyser.analyse(ring, 256);
    }

    /// dB of the loudest bin between `f0` and `f1`.
    pub fn db(&self, f0: f64, f1: f64) -> f32 {
        let bins = self.analyser.display_db();
        if bins.is_empty() {
            return -120.0;
        }
        let hz = self.analyser.sample_rate() / self.analyser.fft_size() as f64;
        let b0 = ((f0 / hz) as usize).clamp(1, bins.len() - 1);
        let b1 = ((f1 / hz).ceil() as usize).clamp(b0, bins.len() - 1);
        bins[b0..=b1].iter().copied().fold(-120.0, f32::max)
    }
}

// ---- compressor ---------------------------------------------------------------------------------------------

pub const COMP_LO: f64 = -40.0;
pub const COMP_HI: f64 = 20.0;

#[derive(Clone, Copy)]
pub struct CompPlot(pub R);

impl CompPlot {
    pub fn of(widget: R) -> Self {
        Self((widget.0 + 22.0, widget.1 + 14.0, widget.2 - 44.0, widget.3 - 32.0))
    }
    pub fn x(&self, db: f64) -> f32 {
        self.0 .0 + ((db - COMP_LO) / (COMP_HI - COMP_LO)) as f32 * self.0 .2
    }
    pub fn y(&self, db: f64) -> f32 {
        self.0 .1 + self.0 .3 - ((db - COMP_LO) / (COMP_HI - COMP_LO)) as f32 * self.0 .3
    }
    fn in_of(&self, x: f32) -> f64 {
        COMP_LO + f64::from((x - self.0 .0) / self.0 .2) * (COMP_HI - COMP_LO)
    }
    fn out_of(&self, y: f32) -> f64 {
        COMP_LO + f64::from((self.0 .1 + self.0 .3 - y) / self.0 .3) * (COMP_HI - COMP_LO)
    }
}

fn audio_taper(k: f64) -> f64 {
    (10f64.powf(2.0 * k) - 1.0) / 99.0
}

pub struct CompCurve {
    pub thr: f64,
    pub ratio: f64,
    pub makeup: f64,
    pub limiter: bool,
}

impl CompCurve {
    pub fn of(v: &[f32; COUNT], ch: usize) -> Self {
        let out = f64::from(v[COMPOUT + ch]);
        Self {
            thr: threshold_dbm(v[THRESHOLD + ch].into()),
            ratio: ratio_value(v[RATIO + ch].into()),
            makeup: if out < 0.01 { -60.0 } else { 20.0 * (audio_taper(out) / audio_taper(0.88)).log10() },
            limiter: v[RATIO_LIMIT + ch] > 0.5,
        }
    }

    pub fn out_db(&self, input: f64) -> f64 {
        if self.limiter {
            return input.min(self.thr);
        }
        let (over, w) = (input - self.thr, 6.0);
        let y = if over <= -w * 0.5 {
            input
        } else if over >= w * 0.5 {
            self.thr + over / self.ratio
        } else {
            input + (1.0 / self.ratio - 1.0) * (over + w * 0.5) * (over + w * 0.5) / (2.0 * w)
        };
        y + self.makeup
    }

    fn ratio_x(&self) -> f64 {
        (self.thr + 18.0).min(COMP_HI - 2.0)
    }

    /// Handles present: THRESH (or CEILING), RATIO, OUT; a limiter has the ceiling only.
    pub fn handles(&self) -> usize {
        if self.limiter { 1 } else { 3 }
    }

    pub fn handle(&self, plot: &CompPlot, i: usize) -> (f32, f32) {
        let input = match i {
            0 => self.thr,
            1 => self.ratio_x(),
            _ => self.thr - 14.0,
        };
        (plot.x(input), plot.y(self.out_db(input)))
    }

    pub fn hit(&self, plot: &CompPlot, x: f32, y: f32) -> Option<usize> {
        (0..self.handles())
            .map(|i| {
                let (hx, hy) = self.handle(plot, i);
                (i, (hx - x).hypot(hy - y))
            })
            .filter(|(_, d)| *d < 18.0)
            .min_by(|a, b| a.1.total_cmp(&b.1))
            .map(|(i, _)| i)
    }

    /// The edit a drag of `handle` to (x, y) makes (`CompDisplay::mouseDrag`).
    pub fn drag(&self, plot: &CompPlot, ch: usize, handle: usize, x: f32, y: f32) -> Edit {
        let input = plot.in_of(x).clamp(COMP_LO, COMP_HI);
        let out = plot.out_of(y).clamp(COMP_LO, COMP_HI);
        match handle {
            0 => (THRESHOLD + ch, ((input.clamp(-20.0, 20.0) + 20.0) / 40.0) as f32),
            1 => {
                let (run, rise) = (self.ratio_x() - self.thr, out - self.makeup - self.thr);
                let r = if rise <= run / 40.0 { 40.0 } else { run / rise };
                let (mut lo, mut hi) = (0.0, 1.0);
                for _ in 0..30 {
                    let mid = 0.5 * (lo + hi);
                    if ratio_value(mid) < r { lo = mid } else { hi = mid }
                }
                (RATIO + ch, (0.5 * (lo + hi)) as f32)
            }
            _ => {
                let makeup = (out - (self.thr - 14.0)).clamp(-12.0, 4.5);
                let a = (audio_taper(0.88) * 10f64.powf(makeup / 20.0)).clamp(0.0, 1.0);
                (COMPOUT + ch, ((99.0 * a + 1.0).log10() / 2.0) as f32)
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::params::defaults;

    #[test]
    fn eq_handles_land_where_they_are_dragged() {
        let v = defaults();
        let plot = EqPlot::of((666.0, 66.0, 356.0, 150.0));
        for (handle, f) in [(1, 120.0), (1, 900.0), (2, 600.0), (2, 9000.0)] {
            let (x, y) = (plot.x(f), plot.y(6.0));
            let mut w = v;
            for (p, value) in eq_drag(&plot, &v, 0, handle, x, y) {
                w[p] = value;
            }
            let (hf, hg) = Bands::of(&w, 0).handle(handle);
            assert!((hf / f - 1.0).abs() < 0.01 && (hg - 6.0).abs() < 0.05, "handle {handle} at {f} Hz: {hf} Hz, {hg} dB");
        }
    }

    #[test]
    fn the_threshold_handle_follows_the_pointer() {
        let v = defaults();
        let plot = CompPlot::of((336.0, 66.0, 140.0, 150.0));
        let curve = CompCurve::of(&v, 0);
        let (p, value) = curve.drag(&plot, 0, 0, plot.x(5.0), 0.0);
        assert_eq!(p, THRESHOLD);
        assert!((threshold_dbm(value.into()) - 5.0).abs() < 0.01);
    }
}
