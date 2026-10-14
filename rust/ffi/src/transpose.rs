// SPDX-License-Identifier: GPL-3.0-or-later
use alloc::vec::Vec;
use xy_vsfilter_compat::transpose::Layout;

/// # Safety
/// For an accepted positive layout, each active source row contains width
/// initialized readable bytes and each destination row has destination_width
/// writable bytes. The allocations cover the calculated spans; pitch gaps may
/// be uninitialized. Native code must not access either allocation concurrently.
/// A return value of zero leaves destination bytes unchanged for native fallback.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_byte_transpose(
    destination: *mut u8,
    destination_width: i32,
    destination_stride: i32,
    source: *const u8,
    width: i32,
    height: i32,
    source_stride: i32,
) -> u32 {
    let Some(layout) = Layout::new(
        width,
        height,
        source_stride,
        destination_width,
        destination_stride,
    ) else {
        return 0;
    };
    // SAFETY: byte layout spans and initialized active rows follow the contract.
    unsafe { transpose_rows(destination, source, layout, |_| true) }
}

// Callers prove element alignment, byte-sized span bounds, initialized source
// rows and writable destination rows. This helper excludes overlapping spans.
unsafe fn transpose_rows<T: Copy + Default>(
    destination: *mut T,
    source: *const T,
    layout: Layout,
    accept: impl Fn(T) -> bool,
) -> u32 {
    let Some(source_end) =
        (source as usize).checked_add(layout.source_span * core::mem::size_of::<T>())
    else {
        return 0;
    };
    let Some(destination_end) =
        (destination as usize).checked_add(layout.destination_span * core::mem::size_of::<T>())
    else {
        return 0;
    };
    if (source as usize) < destination_end && (destination as usize) < source_end {
        return 0;
    }
    let mut input = Vec::new();
    if input
        .try_reserve_exact(layout.width * layout.height)
        .is_err()
    {
        return 0;
    }
    for y in 0..layout.height {
        // SAFETY: one initialized active row only; no borrow includes padding.
        let row = unsafe {
            core::slice::from_raw_parts(source.add(y * layout.source_stride), layout.width)
        };
        if row.iter().any(|value| !accept(*value)) {
            return 0;
        }
        input.extend_from_slice(row);
    }
    let Some(output) = layout.transpose(&input) else {
        return 0;
    };
    for x in 0..layout.width {
        // SAFETY: output is fully initialized Rust storage and disjoint from
        // the caller's writable destination. No destination reference is made.
        unsafe {
            core::ptr::copy_nonoverlapping(
                output.as_ptr().add(x * layout.destination_width),
                destination.add(x * layout.destination_stride),
                layout.destination_width,
            );
        }
    }
    1
}

/// # Safety
/// The same row contract as byte transposition applies, in aligned initialized
/// u32 elements representing native float bits; strides are still byte counts.
/// Non-normal/nonzero values decline before output so native floating-point
/// loads/stores retain their architecture-specific behavior.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_word_transpose(
    destination: *mut u32,
    destination_width: i32,
    destination_stride: i32,
    source: *const u32,
    width: i32,
    height: i32,
    source_stride: i32,
) -> u32 {
    if (source as usize | destination as usize) & 3 != 0
        || source_stride & 3 != 0
        || destination_stride & 3 != 0
        || height.checked_mul(source_stride).is_none()
        || width.checked_mul(destination_stride).is_none()
    {
        return 0;
    }
    let Some(layout) = Layout::new(
        width,
        height,
        source_stride / 4,
        destination_width,
        destination_stride / 4,
    ) else {
        return 0;
    };
    // SAFETY: alignment is checked above and signed byte products fit. Active
    // rows follow the caller's initialized word contract; gaps are not borrowed.
    unsafe {
        transpose_rows(destination, source, layout, |bits| {
            let exponent = bits & 0x7f80_0000;
            (exponent != 0 && exponent != 0x7f80_0000) || bits & 0x7fff_ffff == 0
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::mem::MaybeUninit;
    #[test]
    fn word_copy_preserves_negative_zero_and_declines_special_values_before_writes() {
        let mut source = [MaybeUninit::<u32>::uninit(); 6];
        for (index, value) in [
            (0, 0x8000_0000),
            (1, 0x3f80_0000),
            (3, 0x0080_0000),
            (4, 0x7f7f_ffff),
        ] {
            source[index].write(value);
        }
        let mut destination = [MaybeUninit::<u32>::uninit(); 6];
        // SAFETY: active words are initialized and both output rows writable;
        // the uninitialized third word in each native row is padding.
        unsafe {
            assert_eq!(
                xy_vsfilter_word_transpose(
                    destination.as_mut_ptr().cast(),
                    2,
                    12,
                    source.as_ptr().cast(),
                    2,
                    2,
                    12
                ),
                1
            );
            assert_eq!(destination[0].assume_init(), 0x8000_0000);
            assert_eq!(destination[1].assume_init(), 0x0080_0000);
            assert_eq!(destination[3].assume_init(), 0x3f80_0000);
            assert_eq!(destination[4].assume_init(), 0x7f7f_ffff);
            source[4].write(0x7f80_0001);
            assert_eq!(
                xy_vsfilter_word_transpose(
                    destination.as_mut_ptr().cast(),
                    2,
                    12,
                    source.as_ptr().cast(),
                    2,
                    2,
                    12
                ),
                0
            );
            assert_eq!(destination[0].assume_init(), 0x8000_0000);
        }
    }
    #[test]
    fn uninitialized_padding_and_destination_are_never_borrowed_as_bytes() {
        let mut source = [MaybeUninit::<u8>::uninit(); 12];
        for (index, value) in [(0, 1), (1, 2), (4, 3), (5, 4), (8, 5), (9, 6)] {
            source[index].write(value);
        }
        let mut destination = [MaybeUninit::<u8>::uninit(); 14];
        // SAFETY: the six active source bytes are initialized, and the two
        // destination rows each have five writable bytes. Pitch gaps are unused.
        unsafe {
            assert_eq!(
                xy_vsfilter_byte_transpose(
                    destination.as_mut_ptr().cast(),
                    5,
                    7,
                    source.as_ptr().cast(),
                    2,
                    3,
                    4
                ),
                1
            );
            for (index, value) in [
                (0, 1),
                (1, 3),
                (2, 5),
                (3, 0),
                (4, 0),
                (7, 2),
                (8, 4),
                (9, 6),
                (10, 0),
                (11, 0),
            ] {
                assert_eq!(destination[index].assume_init(), value);
            }
        }
    }
    #[test]
    fn overlapping_allocations_decline_without_writes() {
        let mut bytes = [1, 2, 3, 4];
        // SAFETY: allocations are valid; overlap is detected before any borrow.
        unsafe {
            assert_eq!(
                xy_vsfilter_byte_transpose(bytes.as_mut_ptr(), 2, 2, bytes.as_ptr(), 2, 2, 2),
                0
            );
        }
        assert_eq!(bytes, [1, 2, 3, 4]);
    }
}
