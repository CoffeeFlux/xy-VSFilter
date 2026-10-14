// SPDX-License-Identifier: GPL-3.0-or-later
use core::{ptr, slice};
use xy_vsfilter_compat::vobsub_segment::{select, Segment};

/// # Safety
/// points spans point_count pairs of aligned i32 coordinates; directions spans
/// direction_count aligned i32 values. Only individually requested entries need
/// initialization. output is writable for one Segment, disjoint from inputs.
/// Inputs remain unchanged for this call. Zero leaves output untouched; native
/// container appends, diagnostics and exceptions occur after the call returns.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_vobsub_segment(
    points: *const i32,
    point_count: usize,
    directions: *const i32,
    direction_count: usize,
    start: i32,
    output: *mut Segment,
) -> u32 {
    if point_count > i32::MAX as usize
        || direction_count < point_count
        || point_count > isize::MAX as usize / 8
        || (points as usize | directions as usize) & 3 != 0
    {
        return 0;
    }
    let result = select(
        point_count,
        start,
        |i| {
            // SAFETY: safe selection bounds each reached direction index.
            unsafe { *directions.add(i) }
        },
        |i| {
            // SAFETY: only a requested initialized coordinate pair is read; unused
            // native array capacity and unvisited entries are never borrowed.
            unsafe { [*points.add(i * 2), *points.add(i * 2 + 1)] }
        },
    );
    let Some(result) = result else {
        return 0;
    };
    // SAFETY: caller supplies writable, disjoint output storage.
    unsafe {
        output.write(result);
    }
    1
}

/// # Safety
/// Each table contains count initialized aligned u32 entries. output is writable
/// for two usize values and disjoint from the tables. Native table construction,
/// allocation, diagnostics and output appends stay outside this call; zero
/// leaves output untouched and requests the original selection/diagnostic path.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_vobsub_split(
    lengths: *const u32,
    starts: *const u32,
    ends: *const u32,
    count: usize,
    output: *mut usize,
) -> u32 {
    if count == 0 || count > isize::MAX as usize / 4 {
        return 0;
    }
    // SAFETY: all three initialized native table spans follow the contract.
    let (lengths, starts, ends) = unsafe {
        (
            slice::from_raw_parts(lengths, count),
            slice::from_raw_parts(starts, count),
            slice::from_raw_parts(ends, count),
        )
    };
    let Some(result) = xy_vsfilter_compat::vobsub_segment::split(lengths, starts, ends) else {
        return 0;
    };
    // SAFETY: disjoint writable output after the read-only loans' final use.
    unsafe {
        ptr::copy_nonoverlapping(result.as_ptr(), output, 2);
    }
    1
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::mem::MaybeUninit;
    #[test]
    fn early_selection_does_not_read_unused_native_entries() {
        let mut points = [MaybeUninit::<i32>::uninit(); 12];
        for (i, v) in [-2, 1, -1, 3, 4, -5].into_iter().enumerate() {
            points[i].write(v);
        }
        let mut directions = [MaybeUninit::<i32>::uninit(); 6];
        directions[1].write(1);
        directions[2].write(1);
        let mut result = MaybeUninit::<Segment>::uninit();
        // SAFETY: all reached entries are initialized; output is writable and
        // disjoint. Logical unused array entries deliberately remain uninitialized.
        unsafe {
            assert_eq!(
                xy_vsfilter_vobsub_segment(
                    points.as_ptr().cast(),
                    6,
                    directions.as_ptr().cast(),
                    6,
                    0,
                    result.as_mut_ptr()
                ),
                1
            );
            let result = result.assume_init();
            assert_eq!((result.begin, result.end, result.last), (1, 2, 1));
            assert_eq!(
                (result.start_x, result.start_y, result.end_x, result.end_y),
                (-2, 2, 1, -1)
            );
        }
    }
    #[test]
    fn missing_split_maximum_does_not_publish_partial_output() {
        let lengths = [1u32, 0, 1];
        let starts = [0u32, 1, 1];
        let ends = [1u32, 1, 2];
        let mut output = [88usize, 99];
        // SAFETY: initialized tables and disjoint writable output.
        unsafe {
            assert_eq!(
                xy_vsfilter_vobsub_split(
                    lengths.as_ptr(),
                    starts.as_ptr(),
                    ends.as_ptr(),
                    3,
                    output.as_mut_ptr()
                ),
                0
            );
        }
        assert_eq!(output, [88, 99]);
    }
}
