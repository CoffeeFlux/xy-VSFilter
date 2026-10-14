// SPDX-License-Identifier: GPL-3.0-or-later
use core::{mem::MaybeUninit, slice};
use xy_vsfilter_compat::vobsub_pixels;

/// Private value-only ABI: offset in low 32 bits, length in high 32 bits.
/// All ones declines; zero is the native early-return path, with no palette read.
#[no_mangle]
pub extern "C" fn xy_vsfilter_vobsub_clip(
    left: i32,
    top: i32,
    right: i32,
    bottom: i32,
    x: i32,
    y: i32,
    length: i32,
) -> u64 {
    match vobsub_pixels::clip([left, top, right, bottom], x, y, length) {
        Some(span) => u64::from(span.offset) | (u64::from(span.length) << 32),
        None => u64::MAX,
    }
}

/// # Safety
/// For a nonzero supported length, destination covers pixels * 4 writable bytes,
/// exclusively available for this call. Their old contents may be uninitialized.
/// Native palette/color reads must finish before this call, including aliases of
/// the output. No native callback or concurrent access occurs during the loan.
/// Zero means unsupported length and leaves destination untouched.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_vobsub_fill(
    destination: *mut u8,
    pixels: usize,
    color: u32,
) -> u32 {
    let Some(bytes) = pixels.checked_mul(4).filter(|&n| n <= isize::MAX as usize) else {
        return 0;
    };
    if bytes != 0 {
        // SAFETY: the caller grants exactly the writable span. MaybeUninit
        // permits first writes to native output; no old contents are read.
        let output =
            unsafe { slice::from_raw_parts_mut(destination.cast::<MaybeUninit<u8>>(), bytes) };
        vobsub_pixels::fill(output, color.to_le_bytes());
    }
    1
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn output_can_be_uninitialized_and_surrounding_bytes_are_not_borrowed() {
        let mut output = [MaybeUninit::<u8>::uninit(); 20];
        // SAFETY: bytes 3..15 are writable and exclusive; no initialized output
        // or pixel alignment is required. Unwritten surrounding bytes stay unread.
        unsafe {
            assert_eq!(
                xy_vsfilter_vobsub_fill(output.as_mut_ptr().add(3).cast(), 3, 0x12345678),
                1
            );
            for (i, byte) in output[3..15].iter().enumerate() {
                assert_eq!(byte.assume_init(), [0x78, 0x56, 0x34, 0x12][i % 4]);
            }
            assert_eq!(xy_vsfilter_vobsub_fill(core::ptr::null_mut(), 0, 0), 1);
            assert_eq!(
                xy_vsfilter_vobsub_fill(core::ptr::null_mut(), usize::MAX, 0),
                0
            );
        }
    }
}
