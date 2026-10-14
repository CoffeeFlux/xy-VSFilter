// SPDX-License-Identifier: GPL-2.0-or-later
/// # Safety
/// input is initialized for length UTF-16 units, separator for separator_length
/// units (its length before NUL). Both are immutable during the call and may
/// overlap. No borrowed references escape into native Trim/Add operations.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssf_split_find(
    input: *const u16,
    length: i32,
    separator: *const u16,
    separator_length: usize,
    start: i32,
) -> i32 {
    if start < 0 || length < start || length as usize > isize::MAX as usize / 2 {
        return -1;
    }
    if separator_length == 0 {
        return start;
    }
    if separator_length > (length - start) as usize {
        return -1;
    }
    // SAFETY: lengths are positive, bounded and cover initialized immutable
    // code units. Shared slices may alias each other.
    let input = unsafe { core::slice::from_raw_parts(input, length as usize) };
    let separator = unsafe { core::slice::from_raw_parts(separator, separator_length) };
    xy_vsfilter_compat::ssf_split::find(input, separator, start as usize)
        .and_then(|index| i32::try_from(index).ok())
        .unwrap_or(-1)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn shared_separator_alias_respects_nul_at_each_start_offset() {
        let input = [65, 66, 0, 65, 66, 0, 65, 66];
        // SAFETY: both spans borrow initialized overlapping input immutably.
        unsafe {
            let separator = input.as_ptr().add(3);
            assert_eq!(
                xy_vsfilter_ssf_split_find(input.as_ptr(), 8, separator, 2, 0),
                0
            );
            assert_eq!(
                xy_vsfilter_ssf_split_find(input.as_ptr(), 8, separator, 2, 1),
                -1
            );
            assert_eq!(
                xy_vsfilter_ssf_split_find(input.as_ptr(), 8, separator, 2, 3),
                3
            );
            assert_eq!(
                xy_vsfilter_ssf_split_find(input.as_ptr(), 8, separator, 2, 6),
                6
            );
            assert_eq!(
                xy_vsfilter_ssf_split_find(core::ptr::null(), 0, core::ptr::null(), 0, 0),
                0
            );
        }
    }
}
