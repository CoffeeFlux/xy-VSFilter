// SPDX-License-Identifier: GPL-3.0-or-later
/// Private, value-only ABI: low byte is an optional ASCII prefix; the next byte
/// is the number of points to format (0, 1 or 3). No point/type storage is read.
#[no_mangle]
pub extern "C" fn xy_vsfilter_vobsub_path(kind: u8, previous: u8, remaining: usize) -> u32 {
    xy_vsfilter_compat::vobsub_path::command(kind, previous, remaining)
}

/// # Safety
/// output points to 72 writable, aligned u16 slots, which may be uninitialized.
/// Only the returned prefix is written. Invalid counts return zero untouched.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_vobsub_coordinates(
    x0: i32,
    y0: i32,
    x1: i32,
    y1: i32,
    x2: i32,
    y2: i32,
    count: u32,
    output: *mut u16,
) -> u32 {
    let Some((text, length)) =
        xy_vsfilter_compat::vobsub_path::coordinates(&[x0, y0, x1, y1, x2, y2], count as usize)
    else {
        return 0;
    };
    // SAFETY: the safe formatter bounds the initialized prefix to 72 units;
    // caller supplies writable output, disjoint from this local array.
    unsafe { core::ptr::copy_nonoverlapping(text.as_ptr(), output, length) };
    length as u32
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::mem::MaybeUninit;
    #[test]
    fn formatting_initializes_only_the_returned_prefix() {
        let mut output = [MaybeUninit::<u16>::uninit(); 72];
        // SAFETY: aligned writable output; only the reported units are read.
        unsafe {
            let len =
                xy_vsfilter_vobsub_coordinates(0, -1, 0, 0, 0, 0, 1, output.as_mut_ptr().cast());
            assert_eq!(len, 5);
            for (slot, expected) in output[..len as usize].iter().zip("0 -1 ".encode_utf16()) {
                assert_eq!(slot.assume_init(), expected);
            }
            assert_eq!(
                xy_vsfilter_vobsub_coordinates(0, 0, 0, 0, 0, 0, 2, core::ptr::null_mut()),
                0
            );
        }
    }
}
