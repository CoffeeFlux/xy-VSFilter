// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned HFONT and CMyFont's Windows calls. Handles never designate Rust memory.
//! Registry admission serializes creation; complete calls serialize Drop.
//! Release builds can leave this font selected in the native DC at final release;
//! preserve Windows DeleteObject failure/deferred deletion, as CFont does.
use xy_vsfilter_compat::font_intern::{Error, Key};

#[repr(C)]
struct LogFont {
    height: i32,
    width: i32,
    escapement: i32,
    orientation: i32,
    weight: i32,
    italic: u8,
    underline: u8,
    strikeout: u8,
    charset: u8,
    out_precision: u8,
    clip_precision: u8,
    quality: u8,
    pitch: u8,
    face: [u16; 32],
}
#[repr(C)]
struct TextMetric {
    values: [i32; 11],
    characters: [u16; 4],
    flags: [u8; 5],
}
const _: () = assert!(core::mem::size_of::<LogFont>() == 92);
const _: () = assert!(core::mem::size_of::<TextMetric>() == 60);
#[link(name = "user32")]
extern "system" {
    fn GetDC(window: usize) -> usize;
    fn ReleaseDC(window: usize, dc: usize) -> i32;
}
#[link(name = "gdi32")]
extern "system" {
    fn GetDeviceCaps(dc: usize, index: i32) -> i32;
    fn CreateFontIndirectW(font: *const LogFont) -> usize;
    fn SelectObject(dc: usize, object: usize) -> usize;
    fn GetTextMetricsW(dc: usize, metric: *mut TextMetric) -> i32;
    fn DeleteObject(object: usize) -> i32;
}
#[link(name = "kernel32")]
extern "system" {
    fn MulDiv(number: i32, numerator: i32, denominator: i32) -> i32;
    fn SetLastError(error: u32);
}

fn native_int(value: f64) -> i32 {
    // SAFETY: the supported Windows x86/x64 renderer already requires SSE2.
    // CVTTSD2SI preserves native invalid/inexact status as well as INT_MIN for
    // nonrepresentable inputs; Rust's saturating cast does not have that contract.
    unsafe {
        #[cfg(target_arch = "x86")]
        use core::arch::x86::{_mm_cvttsd_si32, _mm_set_sd};
        #[cfg(target_arch = "x86_64")]
        use core::arch::x86_64::{_mm_cvttsd_si32, _mm_set_sd};
        _mm_cvttsd_si32(_mm_set_sd(value))
    }
}
fn height(size: f64) -> i32 {
    // Keep both source conversions and their position relative to Windows
    // calls. API-boundary observations can see floating status before return.
    core::hint::black_box(native_int(core::hint::black_box(size) + 0.5))
}
pub(crate) struct Font {
    handle: usize,
    ascent: i32,
    descent: i32,
}
impl Font {
    /// # Safety
    /// dc is a live renderer HDC exclusively borrowed for this whole call.
    /// All calls sharing that DC and all font selection/releases are serialized.
    pub(crate) unsafe fn new(key: &Key, dc: usize) -> Result<Self, Error> {
        let mut lf = LogFont {
            height: 0,
            width: 0,
            escapement: 0,
            orientation: 0,
            weight: 0,
            italic: 0,
            underline: 0,
            strikeout: 0,
            charset: key.char_set as u8,
            out_precision: 0,
            clip_precision: 0,
            quality: 0,
            pitch: 0,
            face: [0; 32],
        };
        for (to, &from) in lf.face[..31]
            .iter_mut()
            .zip(key.font_name.iter().take_while(|&&v| v != 0))
        {
            *to = from;
        }
        // SAFETY: screen DC is acquired/released as a matched Windows pair.
        // Integer-only MulDiv/GetDeviceCaps accept these native scalar values.
        // These calls are retained even though CMyFont overwrites lfHeight.
        unsafe {
            let screen = GetDC(0);
            // Cleanup MSVC evaluates the conversion before GetDeviceCaps on
            // x86, and after it on x64; the GDI call observer sees this status.
            #[cfg(target_arch = "x86")]
            let converted = height(key.font_size);
            let dpi = GetDeviceCaps(screen, 90);
            #[cfg(not(target_arch = "x86"))]
            let converted = height(key.font_size);
            lf.height = MulDiv(converted, dpi, 72).wrapping_neg();
            ReleaseDC(0, screen);
        }
        lf.weight = key.font_weight;
        lf.italic = if key.italic { 255 } else { 0 };
        lf.underline = if key.underline { 255 } else { 0 };
        lf.strikeout = if key.strikeout { 255 } else { 0 };
        lf.height = height(key.font_size);
        lf.out_precision = 4;
        lf.clip_precision = 0;
        lf.quality = 4;
        lf.pitch = 0;
        // SAFETY: lf has the SDK layout and fully initialized, terminated face.
        let mut handle = unsafe { CreateFontIndirectW(&lf) };
        if handle == 0 {
            // _tcscpy replaces only the Arial prefix and terminator, retaining
            // the old tail just as the initialized native LOGFONT does.
            lf.face[..6].copy_from_slice(&[65, 114, 105, 97, 108, 0]);
            // SAFETY: same initialized SDK structure after bounded replacement.
            handle = unsafe { CreateFontIndirectW(&lf) };
        }
        if handle != 0 {
            // Cleanup CFont::Attach calls afxMapHGDIOBJ/AfxGetModuleThreadState;
            // MFC's TlsGetValue clears last-error on success. Preserve that
            // observed side effect without retaining the native handle map.
            // SAFETY: SetLastError changes only the calling thread's scalar.
            unsafe {
                SetLastError(0);
            }
        }
        let mut font = Self {
            handle,
            ascent: 0,
            descent: 0,
        };
        let mut metric = TextMetric {
            values: [0; 11],
            characters: [0; 4],
            flags: [0; 5],
        };
        // SAFETY: caller exclusively lends live dc. Font is owned here; zero
        // from failed creation is passed through as the native fallback does.
        // Windows writes only the SDK metric buffer. Restore on every outcome.
        let result = unsafe {
            let previous = SelectObject(dc, handle);
            let success = GetTextMetricsW(dc, &mut metric);
            SelectObject(dc, previous);
            success
        };
        if result == 0 {
            return Err(Error::Construction);
        }
        font.ascent = metric.values[1].wrapping_add(4) >> 3;
        font.descent = metric.values[2].wrapping_add(4) >> 3;
        Ok(font)
    }
    pub(crate) fn snapshot(&self) -> [u8; 16] {
        let mut out = [0; 16];
        out[..8].copy_from_slice(&(self.handle as u64).to_le_bytes());
        out[8..12].copy_from_slice(&self.ascent.to_le_bytes());
        out[12..].copy_from_slice(&self.descent.to_le_bytes());
        out
    }
}
impl Drop for Font {
    fn drop(&mut self) {
        if self.handle != 0 {
            // SAFETY: this non-Clone owner received the handle from Windows;
            // one deletion is attempted, preserving any native DC selection.
            // Ignore failure exactly as CFont/CGdiObject destruction does.
            unsafe {
                // CFont::Detach traverses the same MFC TLS map before deletion.
                SetLastError(0);
                DeleteObject(self.handle);
            }
        }
    }
}
