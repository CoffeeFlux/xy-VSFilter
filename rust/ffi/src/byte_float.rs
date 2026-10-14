// SPDX-License-Identifier: GPL-3.0-or-later
use alloc::vec::Vec;

/// # Safety
/// Active source rows contain width initialized bytes and destination rows
/// contain max(width,destination_width) writable aligned words. Both allocations
/// cover the calculated spans. Strides are in bytes; gaps may be uninitialized.
/// No concurrent access is allowed. Zero returns without destination writes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_byte_float(
    destination: *mut u32,
    destination_width: i32,
    destination_stride: i32,
    source: *const u8,
    width: i32,
    height: i32,
    source_stride: i32,
) -> u32 {
    if width <= 0
        || height <= 0
        || source_stride <= 0
        || destination_stride < 0
        || (destination as usize) & 3 != 0
        || destination_stride & 3 != 0
        || height.checked_mul(source_stride).is_none()
        || height.checked_mul(destination_stride).is_none()
    {
        return 0;
    }
    let (width, height, source_stride, destination_stride, written) = (
        width as usize,
        height as usize,
        source_stride as usize,
        destination_stride as usize,
        width.max(destination_width) as usize,
    );
    let Some(source_span) = (height - 1)
        .checked_mul(source_stride)
        .and_then(|n| n.checked_add(width))
    else {
        return 0;
    };
    let Some(destination_bytes) = written.checked_mul(4) else {
        return 0;
    };
    let Some(destination_span) = (height - 1)
        .checked_mul(destination_stride)
        .and_then(|n| n.checked_add(destination_bytes))
    else {
        return 0;
    };
    if source_span > isize::MAX as usize || destination_span > isize::MAX as usize {
        return 0;
    }
    let Some(source_end) = (source as usize).checked_add(source_span) else {
        return 0;
    };
    let Some(destination_end) = (destination as usize).checked_add(destination_span) else {
        return 0;
    };
    if (source as usize) < destination_end && (destination as usize) < source_end {
        return 0;
    }
    let mut output = Vec::new();
    if output.try_reserve_exact(written).is_err() {
        return 0;
    }
    output.resize(written, 0);
    for y in 0..height {
        // SAFETY: this is exactly one initialized source row, without gaps.
        let row = unsafe { core::slice::from_raw_parts(source.add(y * source_stride), width) };
        xy_vsfilter_compat::byte_float::row(row, &mut output);
        // SAFETY: initialized Rust output is disjoint from the caller's writable
        // output, which is not borrowed. Later destination rows may overlap.
        unsafe {
            core::ptr::copy_nonoverlapping(
                output.as_ptr(),
                destination.add(y * (destination_stride / 4)),
                written,
            );
        }
    }
    1
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::mem::MaybeUninit;
    #[test]
    fn active_rows_exclude_uninitialized_padding() {
        let mut source = [MaybeUninit::<u8>::uninit(); 8];
        for (index, value) in [(0, 0), (1, 1), (2, 255), (5, 128), (6, 127), (7, 2)] {
            source[index].write(value);
        }
        let mut destination = [MaybeUninit::<u32>::uninit(); 12];
        // SAFETY: two initialized three-byte rows and two writable five-word
        // output rows. Source and destination pitch gaps are left uninitialized.
        unsafe {
            assert_eq!(
                xy_vsfilter_byte_float(
                    destination.as_mut_ptr().cast(),
                    5,
                    24,
                    source.as_ptr().cast(),
                    3,
                    2,
                    5
                ),
                1
            );
            for (index, bits) in [
                (0, 0),
                (1, 0x3f80_0000),
                (2, 0x437f_0000),
                (3, 0),
                (4, 0),
                (6, 0x4300_0000),
                (7, 0x42fe_0000),
                (8, 0x4000_0000),
                (9, 0),
                (10, 0),
            ] {
                assert_eq!(destination[index].assume_init(), bits);
            }
        }
    }
}
