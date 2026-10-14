// SPDX-License-Identifier: GPL-3.0-or-later
use core::{mem::MaybeUninit, slice};

/// # Safety
/// Supported dimensions cover width*height aligned initialized source words
/// and writable destination words. Destination may be uninitialized. Regions
/// must not be concurrently accessed; overlaps decline before any borrow/write.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_vobsub_scale(
    source: *const u32,
    destination: *mut u32,
    width: i32,
    height: i32,
) -> u32 {
    if width <= 0 || height <= 0 || (source as usize | destination as usize) & 3 != 0 {
        return 0;
    }
    let Some(bytes) = width.checked_mul(height).and_then(|n| n.checked_mul(4)) else {
        return 0;
    };
    let bytes = bytes as usize;
    let Some(source_end) = (source as usize).checked_add(bytes) else {
        return 0;
    };
    let Some(destination_end) = (destination as usize).checked_add(bytes) else {
        return 0;
    };
    if (source as usize) < destination_end && (destination as usize) < source_end {
        return 0;
    }
    // SAFETY: checked disjoint aligned spans. Output is write-only; no reference
    // to initialized contents is made until native code consumes the filled array.
    let (input, output) = unsafe {
        (
            slice::from_raw_parts(source, bytes / 4),
            slice::from_raw_parts_mut(destination.cast::<MaybeUninit<u32>>(), bytes / 4),
        )
    };
    u32::from(xy_vsfilter_compat::vobsub_scale::filter(
        input,
        output,
        width as usize,
        height as usize,
    ))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn scratch_loan_is_initialized_then_copied_back_before_release() {
        use crate::plane::{
            xy_vsfilter_plane_create, xy_vsfilter_plane_loan, xy_vsfilter_plane_release,
        };
        let owner = xy_vsfilter_plane_create(9 * 4);
        assert!(!owner.is_null());
        let mut pixels = [0u32; 9];
        pixels[5] = u32::MAX;
        pixels[7] = u32::MAX;
        let original = pixels;
        // SAFETY: one aligned scratch loan, disjoint from the initialized image.
        // The filter initializes every output word before copy-back reads it.
        // No access to the owner occurs until all scratch views have ended.
        unsafe {
            let scratch = xy_vsfilter_plane_loan(owner).cast::<u32>();
            assert_eq!(xy_vsfilter_vobsub_scale(pixels.as_ptr(), scratch, 3, 3), 1);
            assert_eq!(pixels, original);
            core::ptr::copy_nonoverlapping(scratch, pixels.as_mut_ptr(), pixels.len());
            assert_eq!(pixels[4], 0x3f3f4040);
            assert_eq!(xy_vsfilter_vobsub_scale(pixels.as_ptr(), scratch, 3, 3), 1);
            core::ptr::copy_nonoverlapping(scratch, pixels.as_mut_ptr(), pixels.len());
            xy_vsfilter_plane_release(owner);
        }
        assert_eq!(pixels[4], 0x5f5f6060);
    }
    #[test]
    fn fresh_native_output_is_written_without_reading_it() {
        let input = [0x12345678u32];
        let mut output = MaybeUninit::<u32>::uninit();
        // SAFETY: input is initialized and output is writable, aligned and disjoint.
        unsafe {
            assert_eq!(
                xy_vsfilter_vobsub_scale(input.as_ptr(), output.as_mut_ptr(), 1, 1),
                1
            );
            assert_eq!(output.assume_init(), input[0]);
        }
    }
    #[test]
    fn overlapping_spans_decline_before_creating_conflicting_borrows() {
        let mut values = [1u32, 2, 3];
        let pointer = values.as_mut_ptr();
        // SAFETY: the allocation is valid for both spans; the checked overlap
        // path returns before constructing either slice or changing any value.
        unsafe {
            assert_eq!(xy_vsfilter_vobsub_scale(pointer, pointer, 3, 1), 0);
        }
        assert_eq!(values, [1, 2, 3]);
    }
}
