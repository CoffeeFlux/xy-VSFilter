// SPDX-License-Identifier: GPL-2.0-or-later
/// # Safety
/// For positive width, pixels lends width initialized exclusive bytes and
/// columns lends width initialized, aligned, exclusive u16 values. The ranges
/// are disjoint. Both borrows end before the next row, which may overlap pixels.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_bilinear_row(
    pixels: *mut u8,
    columns: *mut u16,
    width: i32,
    x_factor: u32,
    y_factor: u32,
) {
    if width <= 0 || width as usize > isize::MAX as usize / 2 {
        return;
    }
    // SAFETY: the native adapter supplies disjoint initialized row/history
    // ranges; history comes from a fresh, zeroed native allocation.
    let pixels = unsafe { core::slice::from_raw_parts_mut(pixels, width as usize) };
    let columns = unsafe { core::slice::from_raw_parts_mut(columns, width as usize) };
    xy_vsfilter_compat::bilinear::row(pixels, columns, x_factor, y_factor);
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn overlapping_rows_have_separate_short_borrows() {
        let mut pixels = [10, 20, 30, 40];
        let mut columns = [0; 3];
        // SAFETY: history is disjoint, initialized and aligned. Each row's
        // borrow ends before the next overlapping native row is lent.
        unsafe {
            xy_vsfilter_bilinear_row(pixels.as_mut_ptr(), columns.as_mut_ptr(), 3, 8, 8);
            xy_vsfilter_bilinear_row(pixels.as_mut_ptr().add(1), columns.as_mut_ptr(), 3, 8, 8);
            xy_vsfilter_bilinear_row(core::ptr::null_mut(), core::ptr::null_mut(), 0, 0, 0);
        }
        assert_eq!(pixels, [0, 0, 10, 20]);
    }
}
