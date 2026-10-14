// SPDX-License-Identifier: GPL-3.0-or-later
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
}
