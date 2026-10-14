// SPDX-License-Identifier: GPL-3.0-or-later
use core::{mem::MaybeUninit, ptr, slice};
use xy_vsfilter_compat::vobsub_outline::{self, Step};

/// # Safety
/// source spans count four-byte pixels with initialized alpha, but RGB may be
/// uninitialized. output spans count writable bytes. Native code grants exclusive
/// output access and no concurrent input mutation; overlap declines before writes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_vobsub_outline_mask(
    source: *const u8,
    output: *mut u8,
    count: usize,
) -> u32 {
    let Some(bytes) = count.checked_mul(4).filter(|&n| n <= isize::MAX as usize) else {
        return 0;
    };
    let Some(source_end) = (source as usize).checked_add(bytes) else {
        return 0;
    };
    let Some(output_end) = (output as usize).checked_add(count) else {
        return 0;
    };
    if count == 0 {
        return 1;
    }
    if (source as usize) < output_end && (output as usize) < source_end {
        return 0;
    }
    // SAFETY: a write-only byte loan; source alpha is read individually.
    let destination = unsafe { slice::from_raw_parts_mut(output.cast::<MaybeUninit<u8>>(), count) };
    vobsub_outline::mask(destination, |i| unsafe { *source.add(i * 4 + 3) });
    1
}

fn count(width: i32, height: i32) -> Option<usize> {
    if width <= 0 || height <= 0 {
        return None;
    }
    width.checked_mul(height).map(|n| n as usize)
}

/// # Safety
/// For valid dimensions, grid is initialized for width*height bytes, and point
/// is writable for two i32 values, disjoint from grid. No concurrent mutation.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_vobsub_outline_find(
    grid: *const u8,
    width: i32,
    height: i32,
    point: *mut i32,
) -> u32 {
    let Some(count) = count(width, height) else {
        return 0;
    };
    // SAFETY: positive checked byte length and initialized native mask.
    let input = unsafe { slice::from_raw_parts(grid, count) };
    let Some(result) = vobsub_outline::find(input, width, height) else {
        return 0;
    };
    // SAFETY: writable point, after the input loan's final use.
    unsafe {
        ptr::copy_nonoverlapping(result.as_ptr(), point, 2);
    }
    1
}

/// # Safety
/// The initialized width*height byte grid is exclusively writable for this
/// call. output is writable for one Step, disjoint from grid. No native callback
/// or unwind occurs during the loan. Declining leaves both outputs untouched.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_vobsub_outline_walk(
    grid: *mut u8,
    width: i32,
    height: i32,
    x: i32,
    y: i32,
    direction: i32,
    output: *mut Step,
) -> u32 {
    let Some(count) = count(width, height) else {
        return 0;
    };
    // SAFETY: caller grants the checked, initialized mask span exclusively.
    let input = unsafe { slice::from_raw_parts_mut(grid, count) };
    let Some(result) = vobsub_outline::walk(input, width, height, x, y, direction) else {
        return 0;
    };
    // SAFETY: writable output, no overlapping input borrow.
    unsafe {
        output.write(result);
    }
    1
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn alpha_only_mask_initializes_storage_before_walking() {
        let mut source = [MaybeUninit::<u8>::uninit(); 36];
        for i in 0..9 {
            source[i * 4 + 3].write(if i == 5 { 255 } else { 0 });
        }
        let mut grid = [MaybeUninit::<u8>::uninit(); 9];
        let mut point = [0i32; 2];
        let mut output = MaybeUninit::<Step>::uninit();
        // SAFETY: alpha bytes are initialized; each output is writable/disjoint.
        // Mask creation initializes every grid byte before find/walk borrow it.
        unsafe {
            assert_eq!(
                xy_vsfilter_vobsub_outline_mask(
                    source.as_ptr().cast(),
                    grid.as_mut_ptr().cast(),
                    9
                ),
                1
            );
            assert_eq!(
                xy_vsfilter_vobsub_outline_find(grid.as_ptr().cast(), 3, 3, point.as_mut_ptr()),
                1
            );
            assert_eq!(point, [1, 1]);
            assert_eq!(
                xy_vsfilter_vobsub_outline_walk(
                    grid.as_mut_ptr().cast(),
                    3,
                    3,
                    1,
                    1,
                    0,
                    output.as_mut_ptr()
                ),
                1
            );
            assert_eq!(output.assume_init().direction, 1);
            assert_eq!(grid[4].assume_init(), 2);
        }
    }
}
