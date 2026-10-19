// SPDX-License-Identifier: GPL-2.0-or-later
//! Detached text measurement packets and the narrow GDI backend.
use crate::windows_text_measure as arithmetic;
use alloc::vec::Vec;
use xy_vsfilter_compat::text_measure::{self, Backend, Style};
#[repr(C)]
struct Size {
    x: i32,
    y: i32,
}
#[link(name = "gdi32")]
extern "system" {
    fn SelectObject(dc: usize, object: usize) -> usize;
    fn GetTextExtentPoint32W(dc: usize, text: *const u16, count: i32, size: *mut Size) -> i32;
}
#[link(name = "kernel32")]
extern "system" {
    fn GetVersion() -> u32;
}
struct Windows {
    dc: usize,
    font: usize,
}
impl Backend for Windows {
    type Selection = usize;
    fn metrics(&mut self, s: u64, a: i32, d: i32) -> [i32; 2] {
        arithmetic::metrics(s, a, d)
    }
    fn select(&mut self) -> usize {
        // SAFETY: export's exclusive live DC and retained font contracts.
        unsafe { SelectObject(self.dc, self.font) }
    }
    fn nonzero(&mut self, b: u64) -> bool {
        arithmetic::nonzero(b)
    }
    fn legacy_windows(&mut self) -> bool {
        // SAFETY: GetVersion has no pointer or ownership arguments.
        (unsafe { GetVersion() } as i32) < 0
    }
    fn extent(&mut self, units: &[u16]) -> Option<i32> {
        let mut size = Size { x: 0, y: 0 };
        // SAFETY: bounded initialized UTF16 within the owned terminated input,
        // count fits i32 by admission, exclusive DC and initialized output SIZE.
        let ok = unsafe {
            GetTextExtentPoint32W(self.dc, units.as_ptr(), units.len() as i32, &mut size)
        };
        if ok == 0 {
            None
        } else {
            Some(size.x)
        }
    }
    fn spacing(&mut self, b: u64) -> i32 {
        arithmetic::spacing(b)
    }
    fn width(&mut self, s: u64, w: i32) -> i32 {
        arithmetic::width(s, w)
    }
    fn restore(&mut self, old: usize) {
        // SAFETY: the selection came from this exclusively borrowed DC. Native
        // still attempts restoration when selection returned null/failure.
        unsafe { SelectObject(self.dc, old) };
    }
}
/// # Safety
/// input spans length initialized bytes; output spans 12 writable bytes, with
/// arbitrary alignment/overlap. dc is exclusively borrowed for the complete
/// call, with its creator thread alive, and the packet's HFONT owner outlives
/// return. Use the renderer operation guard. No foreign pointer is retained.
/// Header: scale X/Y/spacing u64 bits, initial width/ascent/descent i32, unit
/// count u32, HFONT u64; then raw u16 units. GDI failure returns partial output
/// with status zero, as the native void method does; no failed extent is read.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_text_measure_v1(
    dc: usize,
    input: *const u8,
    length: usize,
    output: *mut u8,
) -> u32 {
    if length > isize::MAX as usize {
        return 5;
    }
    if input.is_null() || output.is_null() || length < 48 {
        return 3;
    }
    // SAFETY: the initialized caller span is decoded before any output writes.
    let bytes = unsafe { core::slice::from_raw_parts(input, length) };
    let u32at = |at| u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap());
    let u64at = |at| u64::from_le_bytes(bytes[at..at + 8].try_into().unwrap());
    let count = u32at(36) as usize;
    if count > i32::MAX as usize
        || count.checked_mul(2).and_then(|n| n.checked_add(48)) != Some(length)
    {
        return 3;
    }
    let font = u64at(40);
    if font > usize::MAX as u64 {
        return 3;
    }
    let style = Style {
        scale_x: u64at(0),
        scale_y: u64at(8),
        spacing: u64at(16),
    };
    let initial = u32at(24) as i32;
    let metrics = [u32at(28) as i32, u32at(32) as i32];
    let mut units = Vec::new();
    if units.try_reserve_exact(count + 1).is_err() {
        return 5;
    }
    units.extend(
        bytes[48..]
            .chunks_exact(2)
            .map(|p| u16::from_le_bytes([p[0], p[1]])),
    );
    units.push(0);
    let info = text_measure::measure(
        &units,
        style,
        initial,
        metrics,
        &mut Windows {
            dc,
            font: font as usize,
        },
    );
    let mut result = [0; 12];
    for (chunk, value) in result
        .chunks_exact_mut(4)
        .zip([info.width, info.ascent, info.descent])
    {
        chunk.copy_from_slice(&value.to_le_bytes());
    }
    // SAFETY: detached initialized output and caller's writable 12-byte span.
    unsafe {
        core::ptr::copy_nonoverlapping(result.as_ptr(), output, result.len());
    }
    0
}

#[cfg(test)]
mod tests {
    use super::*;
    #[link(name = "gdi32")]
    extern "system" {
        fn CreateCompatibleDC(dc: usize) -> usize;
        fn DeleteDC(dc: usize) -> i32;
        fn GetCurrentObject(dc: usize, kind: u32) -> usize;
    }
    #[test]
    fn packet_checks_and_unaligned_overlapping_output_with_raw_nul_tail() {
        let mut packet = [0u8; 54];
        packet[..8].copy_from_slice(&100.0f64.to_bits().to_le_bytes());
        packet[8..16].copy_from_slice(&100.0f64.to_bits().to_le_bytes());
        packet[24..28].copy_from_slice(&5i32.to_le_bytes());
        packet[28..32].copy_from_slice(&7i32.to_le_bytes());
        packet[32..36].copy_from_slice(&2i32.to_le_bytes());
        packet[36..40].copy_from_slice(&3u32.to_le_bytes());
        packet[48..].copy_from_slice(&[65, 0, 0, 0, 66, 0]);
        let mut untouched = [0xa5; 12];
        // SAFETY: exact initialized input/output spans, including intentional
        // aliasing. Null/short metadata is rejected before any GDI call. The
        // real DC and its stock font live on this thread through final release.
        unsafe {
            assert_eq!(
                xy_vsfilter_text_measure_v1(0, core::ptr::null(), 54, untouched.as_mut_ptr()),
                3
            );
            assert_eq!(
                xy_vsfilter_text_measure_v1(0, packet.as_ptr(), 47, untouched.as_mut_ptr()),
                3
            );
            assert_eq!(
                xy_vsfilter_text_measure_v1(0, packet.as_ptr(), 48, untouched.as_mut_ptr()),
                3
            );
            assert_eq!(untouched, [0xa5; 12]);
            let dc = CreateCompatibleDC(0);
            assert_ne!(dc, 0);
            let font = GetCurrentObject(dc, 6);
            assert_ne!(font, 0);
            packet[40..48].copy_from_slice(&(font as u64).to_le_bytes());
            let units = [65u16, 0, 66];
            let mut extent = Size { x: 0, y: 0 };
            assert_ne!(GetTextExtentPoint32W(dc, units.as_ptr(), 1, &mut extent), 0);
            assert_eq!(
                xy_vsfilter_text_measure_v1(
                    dc,
                    packet.as_ptr(),
                    packet.len(),
                    packet.as_mut_ptr().add(1)
                ),
                0
            );
            let result: Vec<i32> = packet[1..13]
                .chunks_exact(4)
                .map(|p| i32::from_le_bytes(p.try_into().unwrap()))
                .collect();
            assert_eq!(result, [(extent.x + 9) >> 3, 7, 2]);
            assert_ne!(DeleteDC(dc), 0);
        }
    }
}
