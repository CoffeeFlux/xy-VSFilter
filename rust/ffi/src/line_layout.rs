// SPDX-License-Identifier: GPL-2.0-or-later
use xy_vsfilter_compat::line_layout;

/// # Safety
/// space is initialized/aligned for two i32 values and exclusive during call;
/// line is initialized/aligned for four i32 values, immutable and disjoint.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_line_grow(space: *mut i32, line: *const i32) -> u32 {
    // SAFETY: copy the disjoint native arrays before any output write.
    let (current, line) = unsafe { (*space.cast::<[i32; 2]>(), *line.cast::<[i32; 4]>()) };
    if let Some(result) = line_layout::grow(current, line) {
        // SAFETY: exclusive initialized output has room for both values.
        unsafe { space.cast::<[i32; 2]>().write(result) };
        1
    } else {
        0
    }
}
/// # Safety
/// size/space span two initialized aligned i32 values each; margin spans four.
/// Inputs are immutable throughout call. output is aligned, exclusive, writable
/// for four i32 values and disjoint from every input.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_line_place(
    size: *const i32,
    margin: *const i32,
    space: *const i32,
    alignment: i32,
    output: *mut i32,
) -> u32 {
    // SAFETY: native copies remain live and immutable, no callback or reference
    // to output overlaps these input reads.
    let result = unsafe {
        line_layout::place(
            *size.cast::<[i32; 2]>(),
            *margin.cast::<[i32; 4]>(),
            *space.cast::<[i32; 2]>(),
            alignment,
        )
    };
    if let Some(rect) = result {
        // SAFETY: disjoint output is exclusively writable for the whole rectangle.
        unsafe { output.cast::<[i32; 4]>().write(rect) };
        1
    } else {
        0
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn all_outputs_are_transactional() {
        let mut space = [5i32, 6];
        let line = [i32::MAX, 1, 2, 3];
        let size = [10i32, 20];
        let margin = [i32::MAX, 0, 0, 0];
        let mut rect = [91i32; 4];
        // SAFETY: initialized disjoint stack arrays and exclusive outputs.
        unsafe {
            assert_eq!(xy_vsfilter_line_grow(space.as_mut_ptr(), line.as_ptr()), 0);
            assert_eq!(space, [5, 6]);
            assert_eq!(
                xy_vsfilter_line_place(
                    size.as_ptr(),
                    margin.as_ptr(),
                    space.as_ptr(),
                    7,
                    rect.as_mut_ptr()
                ),
                0
            );
            assert_eq!(rect, [91; 4]);
            assert_eq!(
                xy_vsfilter_line_place(
                    size.as_ptr(),
                    margin.as_ptr(),
                    space.as_ptr(),
                    9,
                    rect.as_mut_ptr()
                ),
                1
            );
            assert_eq!(rect, [5, 0, 10, 6]);
        }
    }
}
