// SPDX-License-Identifier: GPL-2.0-or-later
use xy_vsfilter_compat::path_align::{self, Point};
/// # Safety
/// For positive count, points spans count initialized repr(C) Point values with
/// exclusive access during call. output is aligned, writable for four i32 values,
/// exclusively accessible and disjoint from all points. No native callback runs
/// during this call. Invalid count/pointer shape declines without borrowing.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_path_align(
    points: *mut Point,
    count: i32,
    output: *mut i32,
) -> u32 {
    if count <= 0
        || points.is_null()
        || output.is_null()
        || count as u64 > isize::MAX as u64 / core::mem::size_of::<Point>() as u64
    {
        return 0;
    }
    // SAFETY: native caller owns the initialized, aligned point buffer exclusively
    // and the checked length fits a single allocation/addressable slice.
    let points = unsafe { core::slice::from_raw_parts_mut(points, count as usize) };
    if let Some(geometry) = path_align::align(points) {
        // SAFETY: fresh disjoint four-value output, not a native geometry alias.
        unsafe { output.cast::<[i32; 4]>().write(geometry) };
        1
    } else {
        0
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn declined_alignment_preserves_output_and_initialized_points() {
        let mut points = [Point { x: i32::MIN, y: 0 }, Point { x: 0, y: 0 }];
        let before = points;
        let mut out = [91i32; 4];
        // SAFETY: disjoint initialized arrays; null/count-zero is rejected before borrowing.
        unsafe {
            assert_eq!(
                xy_vsfilter_path_align(points.as_mut_ptr(), 2, out.as_mut_ptr()),
                0
            );
            assert_eq!(points, before);
            assert_eq!(out, [91; 4]);
            assert_eq!(
                xy_vsfilter_path_align(core::ptr::null_mut(), 0, out.as_mut_ptr()),
                0
            );
            points = [Point { x: -1, y: -65 }, Point { x: 64, y: 7 }];
            assert_eq!(
                xy_vsfilter_path_align(points.as_mut_ptr(), 2, out.as_mut_ptr()),
                1
            );
            assert_eq!(out, [-8, -16, 17, 18]);
            assert_eq!(points[1], Point { x: 128, y: 135 });
        }
    }
}
