// SPDX-License-Identifier: GPL-2.0-or-later
//! Logical-to-physical text-file seeking, including unsigned wrap and signed
//! reinterpretation before the final clamp. The native file call remains outside.
pub fn target(position: u64, length: u64, offset: i64, origin: u32, bom: i32) -> Option<i64> {
    let raw = match origin {
        1 => position.wrapping_add(offset as u64),
        2 => length.wrapping_sub(offset as u64),
        _ => offset as u64,
    };
    // Pinned Windows targets reinterpret the high unsigned bit as a negative
    // signed value here. This is deliberately before the max(..., 0) operation.
    (raw.min(length) as i64).max(0).checked_add(i64::from(bom))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn negative_begin_and_end_offsets_clamp_to_end() {
        assert_eq!(target(4, 10, -1, 0, 3), Some(13));
        assert_eq!(target(4, 10, -1, 2, 3), Some(13));
        assert_eq!(target(4, 10, 2, 2, 3), Some(11));
        assert_eq!(target(4, 10, -5, 1, 3), Some(13));
        assert_eq!(target(4, 10, 2, u32::MAX, 3), Some(5));
    }
    #[test]
    fn high_unsigned_values_are_reinterpreted_before_the_zero_clamp() {
        assert_eq!(target(u64::MAX, u64::MAX, 1, 1, 2), Some(2));
        assert_eq!(target(0, u64::MAX, -1, 0, 2), Some(2));
        assert_eq!(target(0, u64::MAX, i64::MAX, 0, 0), Some(i64::MAX));
        assert_eq!(target(0, u64::MAX, i64::MAX, 0, 1), None);
    }
}
