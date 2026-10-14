// SPDX-License-Identifier: GPL-3.0-or-later
/// # Safety
/// For positive width, bytes lends width initialized, exclusively accessible
/// bytes for this call. Later native rows may overlap after this borrow ends.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_be_filter_row(bytes: *mut u8, width: i32, policy: u32) {
    if width <= 0 {
        return;
    }
    // SAFETY: the native row adapter lends exactly this initialized writable row.
    xy_vsfilter_compat::be_filter::row(
        unsafe { core::slice::from_raw_parts_mut(bytes, width as usize) },
        policy,
    );
}

#[no_mangle]
pub extern "C" fn xy_vsfilter_weighted_pixel(
    neighbors: u32,
    center: u32,
    side: u32,
    middle: u32,
    policy: u32,
) -> u8 {
    xy_vsfilter_compat::be_filter::weighted_pixel(neighbors, center, side, middle, policy)
}

#[no_mangle]
pub extern "C" fn xy_vsfilter_weighted_group(
    next: u64,
    state: u32,
    side: u32,
    middle: u32,
    policy: u32,
) -> u64 {
    xy_vsfilter_compat::be_filter::weighted_group(next, state, side, middle, policy)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn row_loans_end_before_the_next_overlapping_pass() {
        let mut bytes = [8, 16, 24, 32];
        // SAFETY: each bounded borrow is exclusive and ends before the next.
        unsafe {
            xy_vsfilter_be_filter_row(bytes.as_mut_ptr(), 3, 0);
            xy_vsfilter_be_filter_row(bytes.as_mut_ptr().add(1), 3, 0);
            xy_vsfilter_be_filter_row(core::ptr::null_mut(), 0, 0);
        }
        assert_eq!(bytes, [8, 12, 20, 20]);
    }
}
