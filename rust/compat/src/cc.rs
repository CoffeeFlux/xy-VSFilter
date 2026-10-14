// SPDX-License-Identifier: GPL-2.0-or-later
//! Closed-caption cursor and initialized cell updates.

pub fn move_cursor(mut x: i32, mut y: i32) -> Option<[i32; 2]> {
    x = x.max(0);
    y = y.max(0);
    if x >= 32 {
        x = 0;
        y = y.checked_add(1)?;
    }
    if y >= 16 {
        y = 0;
    }
    Some([x, y])
}

/// The 33rd cell of every native row is retained as its string terminator.
/// Invalid coordinates or a short backing are declined without any write.
pub fn put(buffer: &mut [u16], x: i32, y: i32, value: u16) -> bool {
    if buffer.len() < 16 * 33 || !(0..32).contains(&x) || !(0..16).contains(&y) {
        return false;
    }
    buffer[y as usize * 33 + x as usize] = value;
    true
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn wrapping_is_single_step_and_row_terminators_are_preserved() {
        assert_eq!(move_cursor(200, 14), Some([0, 15]));
        assert_eq!(move_cursor(32, 15), Some([0, 0]));
        assert_eq!(move_cursor(-1, -1), Some([0, 0]));
        assert_eq!(move_cursor(31, i32::MAX), Some([31, 0]));
        assert_eq!(move_cursor(32, i32::MAX), None);
        let mut data = [7u16; 528];
        assert!(put(&mut data, 31, 15, 0xd800));
        assert_eq!(data[526], 0xd800);
        assert_eq!(data[527], 7);
        assert!(!put(&mut data, 32, 15, 0));
        assert!(!put(&mut data[..527], 0, 0, 0));
    }
}
