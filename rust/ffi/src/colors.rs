// SPDX-License-Identifier: GPL-2.0-or-later
//! Stateless shared-color boundary. No registry admission or native owner.
use alloc::vec::Vec;
use core::ptr;

fn valid_length(length: u32) -> bool {
    length <= i32::MAX as u32 && length as usize <= isize::MAX as usize / 2
}
unsafe fn copy(input: *const u16, length: u32) -> Vec<u16> {
    if length == 0 {
        return Vec::new();
    }
    // SAFETY: caller preflights the representation bound and nonnull pointer;
    // the export contract guarantees an initialized aligned input span.
    unsafe { core::slice::from_raw_parts(input, length as usize) }.to_vec()
}
/// # Safety
/// Each nonempty input is an initialized aligned span of raw UTF16 units;
/// output spans capacity writable aligned units (may be uninitialized).
/// Inputs may alias each other and output: both are copied before CRT calls
/// and the final write. No native reference or pointer is retained.
/// Mode0 resolves hash-trimmed HTML color to two little-endian u16 words;
/// mode1 formats an SSA color tag with the supplied raw ctag, requiring
/// tag_length+12 output units. Invalid metadata returns1 without writes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_html_color_v1(
    mode: u32,
    input: *const u16,
    length: u32,
    tag: *const u16,
    tag_length: u32,
    output: *mut u16,
    capacity: u32,
) -> u32 {
    let needed = match mode {
        0 if tag_length == 0 => 2,
        1 => match tag_length.checked_add(12) {
            Some(n) => n,
            None => return 1,
        },
        _ => return 1,
    };
    if !valid_length(length)
        || !valid_length(tag_length)
        || !valid_length(needed)
        || capacity < needed
        || output.is_null()
        || length != 0 && input.is_null()
        || tag_length != 0 && tag.is_null()
    {
        return 1;
    }
    // SAFETY: preflighted metadata; input spans satisfy the export contract.
    // These owned copies end both caller-buffer loans before any external call.
    let (input, tag) = unsafe { (copy(input, length), copy(tag, tag_length)) };
    let color = xy_vsfilter_compat::colors::html(&input, mode == 0, |text| {
        crate::windows_crt::number(text, 16).0
    });
    let result = if mode == 0 {
        alloc::vec![color as u16, (color >> 16) as u16]
    } else {
        xy_vsfilter_compat::colors::tag(color, &tag)
    };
    debug_assert_eq!(result.len(), needed as usize);
    // SAFETY: output has preflighted capacity for the initialized owned result.
    // This allocation is disjoint from caller storage; output is never read.
    unsafe {
        ptr::copy_nonoverlapping(result.as_ptr(), output, result.len());
    }
    0
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn copied_inputs_allow_overlap_and_invalid_shapes_leave_output_untouched() {
        let mut input: Vec<u16> = "##red".encode_utf16().collect();
        let mut out = [0xa5u16; 20];
        // SAFETY: initialized inputs, bounded writable outputs; oversized/invalid
        // metadata cases are rejected before dereferencing the null input.
        unsafe {
            for (mode, len, tlen, capacity) in [
                (2, 0, 0, 20),
                (0, 0, 1, 20),
                (1, 0, u32::MAX, 20),
                (0, u32::MAX, 0, 20),
                (1, 0, 0, 11),
            ] {
                assert_eq!(
                    xy_vsfilter_html_color_v1(
                        mode,
                        ptr::null(),
                        len,
                        ptr::null(),
                        tlen,
                        out.as_mut_ptr(),
                        capacity
                    ),
                    1
                );
                assert_eq!(out, [0xa5; 20]);
            }
            assert_eq!(
                xy_vsfilter_html_color_v1(
                    0,
                    input.as_ptr(),
                    5,
                    ptr::null(),
                    0,
                    input.as_mut_ptr(),
                    5
                ),
                0
            );
            assert_eq!(&input[..2], &[255, 0]);
            let black: Vec<u16> = "black".encode_utf16().collect();
            out[0] = 0;
            out[1] = 0xd800;
            assert_eq!(
                xy_vsfilter_html_color_v1(
                    1,
                    black.as_ptr(),
                    5,
                    out.as_ptr(),
                    2,
                    out.as_mut_ptr(),
                    20
                ),
                0
            );
            assert_eq!(&out[..6], &[123, 92, 0, 0xd800, 38, 72]);
            assert_eq!(&out[6..14], &[48, 48, 48, 48, 48, 48, 38, 125]);
            assert_eq!(
                xy_vsfilter_html_color_v1(0, ptr::null(), 0, ptr::null(), 0, out.as_mut_ptr(), 20),
                0
            );
            assert_eq!(&out[..2], &[65535, 255]);
        }
    }
}
