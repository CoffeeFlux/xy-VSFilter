// SPDX-License-Identifier: GPL-2.0-or-later
//! Stateless renderer HTML parser boundary. All platform services operate on
//! owned Rust input; no document-registry borrow or native gate is involved.
use core::ptr;
use xy_vsfilter_compat::html_tag::{self, Services};
struct Windows;
impl Services for Windows {
    fn space(&mut self, u: u16) -> bool {
        crate::windows_crt::space(u)
    }
    fn lower(&mut self, s: &mut [u16]) {
        crate::windows_crt::lower(s)
    }
    fn number(&mut self, s: &[u16], base: i32) -> i32 {
        crate::windows_crt::number(s, base).0
    }
}
/// # Safety
/// Input spans length initialized bytes; output spans capacity writable bytes
/// and may be uninitialized. Buffers may overlap: input is copied before any
/// CRT service or final write. No native pointers escape. Wire format is
/// html_tag::packet; invalid metadata/capacity leaves output untouched.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_html_tag_v1(
    input: *const u8,
    length: usize,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    if length > isize::MAX as usize || capacity > isize::MAX as usize {
        return 2;
    }
    if length < 408 || capacity < 212 || input.is_null() || output.is_null() {
        return 1;
    }
    // SAFETY: caller supplies the preflighted initialized input span. The
    // borrowed native slice is dropped after this owned copy, before CRT calls.
    let input = unsafe { core::slice::from_raw_parts(input, length) }.to_vec();
    let result = match html_tag::packet(&input, capacity, &mut Windows) {
        Ok(r) => r,
        Err(html_tag::Error::Shape) => return 1,
        Err(html_tag::Error::Overflow) => return 2,
    };
    // SAFETY: core preflights the maximum reply size before parsing. Result
    // is an initialized distinct allocation; output is written without reading.
    unsafe {
        ptr::copy_nonoverlapping(result.as_ptr(), output, result.len());
    }
    0
}
#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;
    #[test]
    fn malformed_packets_and_overlapping_output_are_safe_and_atomic() {
        let mut packet = vec![0u8; 408];
        packet[4..8].copy_from_slice(&192u32.to_le_bytes());
        packet[8..12].copy_from_slice(&192u32.to_le_bytes());
        let mut out = [0xa5; 212];
        // SAFETY: finite initialized packets and writable outputs. Invalid/null and
        // oversized metadata are refused without dereferencing their pointers.
        unsafe {
            assert_eq!(
                xy_vsfilter_html_tag_v1(ptr::null(), usize::MAX, out.as_mut_ptr(), 212),
                2
            );
            assert_eq!(
                xy_vsfilter_html_tag_v1(packet.as_ptr(), 408, out.as_mut_ptr(), 211),
                1
            );
            assert_eq!(out, [0xa5; 212]);
            packet[24 + 56] = 2;
            assert_eq!(
                xy_vsfilter_html_tag_v1(packet.as_ptr(), 408, out.as_mut_ptr(), 212),
                1
            );
            assert_eq!(out, [0xa5; 212]);
            packet[24 + 56] = 0;
            assert_eq!(
                xy_vsfilter_html_tag_v1(packet.as_ptr(), 408, packet.as_mut_ptr(), 408),
                0
            );
            assert_eq!(&packet[..16], &[0; 16]);
            assert_eq!(&packet[16..20], &192u32.to_le_bytes());
        }
    }
}
