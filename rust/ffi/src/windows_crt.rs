// SPDX-License-Identifier: GPL-2.0-or-later
//! Narrow Windows CRT string services. Inputs are copied and terminated here.
use alloc::vec::Vec;
use core::ptr;
extern "C" {
    fn pow(base: f64, exponent: f64) -> f64;
    fn wcstol(text: *const u16, end: *mut *mut u16, base: i32) -> i32;
    fn wcstod(text: *const u16, end: *mut *mut u16) -> f64;
    fn _wcsicmp(a: *const u16, b: *const u16) -> i32;
    fn _wcslwr_s(text: *mut u16, capacity: usize) -> i32;
    fn iswspace(unit: u16) -> i32;
}
pub(crate) fn power(base: f64, exponent: f64) -> f64 {
    // SAFETY: CRT pow accepts all double bit patterns; errno is deliberately
    // neither cleared nor translated. No pointers or resources cross this call.
    unsafe { pow(base, exponent) }
}
fn terminated(input: &[u16]) -> Vec<u16> {
    let mut out = input.to_vec();
    out.push(0);
    out
}
pub(crate) fn space(unit: u16) -> bool {
    // SAFETY: Windows wint_t is unsigned16bits, as CString's CRT traits use.
    unsafe { iswspace(unit) != 0 }
}
pub(crate) fn lower(input: &mut [u16]) {
    let mut buffer = terminated(input);
    // SAFETY: owned writable terminated buffer; capacity includes the final
    // NUL. Like CString MakeLower, ignore the return and retain original length.
    // Embedded NUL terminates conversion but leaves the remaining units intact.
    unsafe {
        _wcslwr_s(buffer.as_mut_ptr(), buffer.len());
    }
    input.copy_from_slice(&buffer[..input.len()]);
}
pub(crate) fn number(input: &[u16], base: i32) -> (i32, u32) {
    let input = terminated(input);
    let mut end = ptr::null_mut();
    // SAFETY: terminated owned UTF-16 input, valid end-pointer slot; callers use
    // base10 or16. Windows long is32bits on both SDK-probed MSVC architectures.
    let value = unsafe { wcstol(input.as_ptr(), &mut end, base) };
    (value, u32::from(end.cast_const() > input.as_ptr()))
}
pub(crate) fn compare(a: &[u16], b: &[u16]) -> i32 {
    let a = terminated(a);
    let b = terminated(b);
    // SAFETY: two live terminated UTF-16 buffers; the exact CRT function used by
    // CStringW::CompareNoCase, retaining native locale and first-NUL semantics.
    unsafe { _wcsicmp(a.as_ptr(), b.as_ptr()) }
}

#[link(
    name = "legacy_stdio_definitions",
    kind = "static",
    modifiers = "-bundle"
)]
extern "C" {
    fn swscanf(text: *const u16, format: *const u16, ...) -> i32;
}
pub(crate) fn float(input: &[u16]) -> f64 {
    let input = terminated(input);
    // SAFETY: terminated owned code units and optional null end pointer, matching
    // the legacy renderer call. No borrowed native storage survives here.
    unsafe { wcstod(input.as_ptr(), ptr::null_mut()) }
}
pub(crate) fn scan_effect(input: &[u16], count: usize) -> (i32, [i32; 4]) {
    let input = terminated(input);
    let format: &[u16] = if count == 3 {
        &[37, 100, 59, 37, 100, 59, 37, 100, 0]
    } else {
        &[37, 100, 59, 37, 100, 59, 37, 100, 59, 37, 100, 0]
    };
    let mut a = 0i32;
    let mut b = 0i32;
    let mut c = 0i32;
    let mut d = 0i32;
    // SAFETY: fixed formats contain only three/four %d conversions; all varargs
    // are live distinct writable i32 slots. Extra fourth argument is ignored for
    // Banner, exactly as C varargs require. The CRT owns scanf semantics/errno.
    let result = unsafe {
        swscanf(
            input.as_ptr(),
            format.as_ptr(),
            &mut a as *mut i32,
            &mut b as *mut i32,
            &mut c as *mut i32,
            &mut d as *mut i32,
        )
    };
    (result, [a, b, c, d])
}
