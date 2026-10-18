// SPDX-License-Identifier: GPL-2.0-or-later
//! Narrow Windows CRT string services. Inputs are copied and terminated here.
use alloc::vec::Vec;
use core::ptr;
extern "C" {
    fn wcstol(text: *const u16, end: *mut *mut u16, base: i32) -> i32;
    fn _wcsicmp(a: *const u16, b: *const u16) -> i32;
}
fn terminated(input: &[u16]) -> Vec<u16> {
    let mut out = input.to_vec();
    out.push(0);
    out
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
