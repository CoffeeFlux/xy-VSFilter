// SPDX-License-Identifier: GPL-3.0-or-later
//! RealText timestamp components; native streams retain locale and formatting.

/// Preserve signed truncation and the original linear precision divisor.
/// Decline precisions whose native intermediate arithmetic is unrepresentable.
pub fn components(time: i32, precision: i32) -> Option<[i32; 4]> {
    let mut millis = time % 1000;
    if precision < 3 {
        let divisor = 3i32.checked_sub(precision)?.checked_mul(10)?;
        millis /= divisor;
    }
    Some([
        time / 1000 / 60 / 60,
        (time / 1000 / 60) % 60,
        (time / 1000) % 60,
        millis,
    ])
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn precision_is_linear_and_signed_components_truncate_toward_zero() {
        assert_eq!(components(3_723_999, 1), Some([1, 2, 3, 49]));
        assert_eq!(components(-3_723_999, 1), Some([-1, -2, -3, -49]));
        assert_eq!(components(i32::MIN, 3), Some([-596, -31, -23, -648]));
        assert_eq!(components(999, -214_748_361), Some([0, 0, 0, 0]));
        assert_eq!(components(999, -214_748_362), None);
        assert_eq!(components(999, i32::MIN), None);
        assert_eq!(components(999, i32::MAX), Some([0, 0, 0, 999]));
    }
}
