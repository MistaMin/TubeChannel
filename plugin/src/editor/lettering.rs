//! Lettering. On macOS the text is set by Core Text in the system font, regular and bold,
//! as a small picture per string placed on whole device pixels, so it is sharp on a 1x
//! display. Elsewhere it is the toolkit's own face.

use openplugin::dri::goodlookindri::color::Color;
use openplugin::dri::goodlookindri::scene::Scene;

/// Where the toolkit puts a line of text: `y` is its middle, the baseline this share of the size below.
const MIDDLE: f32 = 0.35;

/// Width of `text` at `size` units.
pub fn width(text: &str, size: f32, bold: bool) -> f32 {
    imp::width(text, size, bold)
}

/// Draw `text` from `left`, its middle at `y`, in panel units.
pub fn draw(scene: &mut Scene, left: f32, y: f32, text: &str, colour: Color, size: f32, bold: bool) {
    if !text.is_empty() {
        imp::draw(scene, left, y + size * MIDDLE, text, colour, size, bold);
    }
}

#[cfg(not(target_os = "macos"))]
mod imp {
    use super::*;
    use openplugin::dri::goodlookindri::scene::Point;
    use goodlookindri_shell::text as font;

    pub fn width(text: &str, size: f32, _bold: bool) -> f32 {
        font::width(text, size)
    }

    pub fn draw(scene: &mut Scene, left: f32, baseline: f32, text: &str, colour: Color, size: f32, _bold: bool) {
        scene.text(Point::new(left, baseline - size * MIDDLE), text, colour, size, false);
    }
}

#[cfg(target_os = "macos")]
#[allow(non_upper_case_globals, clippy::missing_safety_doc)]
mod imp {
    use super::*;
    use std::collections::HashMap;
    use std::ffi::c_void;
    use std::sync::{Arc, Mutex};

    use openplugin::dri::goodlookindri::hardware;
    use openplugin::dri::goodlookindri::scene::{Image, Rect};

    type Ref = *const c_void;

    #[link(name = "CoreFoundation", kind = "framework")]
    extern "C" {
        static kCFTypeDictionaryKeyCallBacks: c_void;
        static kCFTypeDictionaryValueCallBacks: c_void;
        fn CFStringCreateWithBytes(alloc: Ref, bytes: *const u8, len: isize, encoding: u32, external: u8) -> Ref;
        fn CFDictionaryCreate(alloc: Ref, keys: *const Ref, values: *const Ref, n: isize, kcb: *const c_void, vcb: *const c_void) -> Ref;
        fn CFAttributedStringCreate(alloc: Ref, string: Ref, attributes: Ref) -> Ref;
        fn CFRelease(obj: Ref);
    }
    #[link(name = "CoreGraphics", kind = "framework")]
    extern "C" {
        fn CGColorSpaceCreateDeviceRGB() -> Ref;
        fn CGColorSpaceRelease(cs: Ref);
        fn CGColorCreate(space: Ref, components: *const f64) -> Ref;
        fn CGColorRelease(c: Ref);
        fn CGBitmapContextCreate(data: *mut c_void, w: usize, h: usize, bpc: usize, bpr: usize, space: Ref, info: u32) -> Ref;
        fn CGContextRelease(ctx: Ref);
        fn CGContextSetTextPosition(ctx: Ref, x: f64, y: f64);
        fn CGContextSetShouldSmoothFonts(ctx: Ref, on: u8);
        fn CGContextSetShouldAntialias(ctx: Ref, on: u8);
        fn CGContextSetAllowsFontSubpixelPositioning(ctx: Ref, on: u8);
        fn CGContextSetShouldSubpixelPositionFonts(ctx: Ref, on: u8);
    }
    #[link(name = "CoreText", kind = "framework")]
    extern "C" {
        static kCTFontAttributeName: Ref;
        static kCTForegroundColorAttributeName: Ref;
        fn CTFontCreateUIFontForLanguage(ui_type: u32, size: f64, language: Ref) -> Ref;
        fn CTLineCreateWithAttributedString(s: Ref) -> Ref;
        fn CTLineGetTypographicBounds(line: Ref, ascent: *mut f64, descent: *mut f64, leading: *mut f64) -> f64;
        fn CTLineDraw(line: Ref, ctx: Ref);
    }

    /// kCTFontUIFontSystem and kCTFontUIFontEmphasizedSystem.
    const SYSTEM: u32 = 2;
    const SYSTEM_BOLD: u32 = 3;
    /// Clear pixels around the letters in a picture.
    const PAD: usize = 2;

    #[derive(Clone, PartialEq, Eq, Hash)]
    struct Key {
        text: String,
        /// The size in device pixels, as bits.
        px: u32,
        colour: u32,
        bold: bool,
    }

    struct Set {
        image: Arc<Image>,
        /// Pixels from the picture's top to the baseline.
        baseline: f32,
    }

    #[derive(Default)]
    struct Cache {
        /// Core Text fonts by (bold, size bits); the address as a number so the cache is `Send`.
        fonts: HashMap<(bool, u32), usize>,
        widths: HashMap<(String, u32, bool), f32>,
        sets: HashMap<Key, Arc<Set>>,
    }

    unsafe fn font(cache: &mut Cache, bold: bool, size: f32) -> Ref {
        if cache.fonts.len() > 256 {
            for (_, f) in cache.fonts.drain() {
                CFRelease(f as Ref);
            }
        }
        *cache.fonts.entry((bold, size.to_bits())).or_insert_with(|| CTFontCreateUIFontForLanguage(if bold { SYSTEM_BOLD } else { SYSTEM }, f64::from(size), std::ptr::null()) as usize) as Ref
    }

    /// A Core Text line of `text`; the caller releases it.
    unsafe fn line(cache: &mut Cache, text: &str, size: f32, bold: bool, colour: Option<Color>) -> Ref {
        let string = CFStringCreateWithBytes(std::ptr::null(), text.as_ptr(), text.len() as isize, 0x0800_0100, 0);
        let mut keys = vec![kCTFontAttributeName];
        let mut values = vec![font(cache, bold, size)];
        let mut cg = std::ptr::null();
        if let Some(c) = colour {
            let space = CGColorSpaceCreateDeviceRGB();
            let parts = [f64::from(c.r()) / 255.0, f64::from(c.g()) / 255.0, f64::from(c.b()) / 255.0, f64::from(c.a()) / 255.0];
            cg = CGColorCreate(space, parts.as_ptr());
            CGColorSpaceRelease(space);
            keys.push(kCTForegroundColorAttributeName);
            values.push(cg);
        }
        let attributes = CFDictionaryCreate(std::ptr::null(), keys.as_ptr(), values.as_ptr(), keys.len() as isize, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        let attributed = CFAttributedStringCreate(std::ptr::null(), string, attributes);
        let line = CTLineCreateWithAttributedString(attributed);
        CFRelease(attributed);
        CFRelease(attributes);
        CFRelease(string);
        if !cg.is_null() {
            CGColorRelease(cg);
        }
        line
    }

    pub fn width(text: &str, size: f32, bold: bool) -> f32 {
        if text.is_empty() {
            return 0.0;
        }
        let mut cache = lock();
        let key = (text.to_string(), size.to_bits(), bold);
        if let Some(w) = cache.widths.get(&key) {
            return *w;
        }
        let w = unsafe {
            let l = line(&mut cache, text, size, bold, None);
            let w = CTLineGetTypographicBounds(l, std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut());
            CFRelease(l);
            w as f32
        };
        if cache.widths.len() > 4000 {
            cache.widths.clear();
        }
        cache.widths.insert(key, w);
        w
    }

    fn lock() -> std::sync::MutexGuard<'static, Cache> {
        static CACHE: std::sync::OnceLock<Mutex<Cache>> = std::sync::OnceLock::new();
        CACHE.get_or_init(|| Mutex::new(Cache::default())).lock().unwrap_or_else(|e| e.into_inner())
    }

    /// The picture of `text` at `px` device pixels.
    fn set(text: &str, px: f32, colour: Color, bold: bool) -> Option<Arc<Set>> {
        let mut cache = lock();
        let key = Key { text: text.to_string(), px: px.to_bits(), colour: colour.0, bold };
        if let Some(set) = cache.sets.get(&key) {
            return Some(set.clone());
        }
        let set = unsafe {
            let l = line(&mut cache, text, px, bold, Some(colour));
            let (mut ascent, mut descent) = (0.0f64, 0.0f64);
            let advance = CTLineGetTypographicBounds(l, &mut ascent, &mut descent, std::ptr::null_mut());
            let (ascent, descent) = (ascent.ceil() as usize, descent.ceil() as usize);
            let (w, h) = (advance.ceil() as usize + 2 * PAD, ascent + descent + 2 * PAD);
            let mut pixels = vec![0u32; w * h];
            let space = CGColorSpaceCreateDeviceRGB();
            // kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little: a premultiplied 0xAARRGGBB word.
            let ctx = CGBitmapContextCreate(pixels.as_mut_ptr().cast(), w, h, 8, w * 4, space, 2 | (2 << 12));
            CGColorSpaceRelease(space);
            if ctx.is_null() {
                CFRelease(l);
                return None;
            }
            CGContextSetShouldAntialias(ctx, 1);
            CGContextSetShouldSmoothFonts(ctx, 0);
            CGContextSetAllowsFontSubpixelPositioning(ctx, 1);
            CGContextSetShouldSubpixelPositionFonts(ctx, 1);
            // Core Graphics counts rows from the bottom.
            CGContextSetTextPosition(ctx, PAD as f64, (descent + PAD) as f64);
            CTLineDraw(l, ctx);
            CGContextRelease(ctx);
            CFRelease(l);
            Arc::new(Set { image: Arc::new(Image::new(w as u32, h as u32, pixels)), baseline: (ascent + PAD) as f32 })
        };
        if cache.sets.len() > 3000 {
            cache.sets.clear();
        }
        cache.sets.insert(key, set.clone());
        Some(set)
    }

    pub fn draw(scene: &mut Scene, left: f32, baseline: f32, text: &str, colour: Color, size: f32, bold: bool) {
        // Device pixels per panel unit: the picture is made at that size and put on whole pixels.
        let unit = hardware::device_scale().max(0.25);
        let Some(set) = set(text, size * unit, colour, bold) else { return };
        let x = (left * unit).round() - PAD as f32;
        let y = (baseline * unit).round() - set.baseline;
        scene.image(&set.image, Rect::new(x / unit, y / unit, set.image.w as f32 / unit, set.image.h as f32 / unit), 1.0);
    }
}
