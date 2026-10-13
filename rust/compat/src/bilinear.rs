// SPDX-License-Identifier: GPL-2.0-or-later
//! In-place subpixel rows with the original per-column WORD history.
/// Returns false without mutation for a mismatched history or unsupported factor.
pub fn row(bytes: &mut [u8], columns: &mut [u16], x_factor: u32, y_factor: u32) -> bool {
    if columns.len() < bytes.len() || x_factor > 8 || y_factor > 8 {
        return false;
    }
    let mut last = 0;
    for (byte, column) in bytes.iter_mut().zip(columns) {
        let sample = u32::from(*byte);
        let horizontal_part = sample * x_factor;
        let horizontal = sample * 8 - horizontal_part + last;
        last = horizontal_part;
        let vertical_part = horizontal * y_factor;
        let vertical = horizontal * 8 - vertical_part + u32::from(*column);
        *byte = ((vertical + 32) >> 6) as u8;
        *column = vertical_part as u16;
    }
    true
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn full_pixel_shift_uses_previous_row_and_resets_left_edge() {
        let mut first = [10, 20, 30];
        let mut second = [40, 50, 60];
        let mut columns = [0; 3];
        assert!(row(&mut first, &mut columns, 8, 8));
        assert_eq!(first, [0, 0, 0]);
        assert!(row(&mut second, &mut columns, 8, 8));
        assert_eq!(second, [0, 10, 20]);
    }
    #[test]
    fn unsupported_factors_or_short_history_leave_inputs_untouched() {
        let mut pixels = [1, 2];
        let mut columns = [9];
        assert!(!row(&mut pixels, &mut columns, 0, 0));
        assert_eq!(pixels, [1, 2]);
        assert_eq!(columns, [9]);
        assert!(!row(&mut pixels, &mut [0; 2], 9, 0));
        assert_eq!(pixels, [1, 2]);
    }
}
