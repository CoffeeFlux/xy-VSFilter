// SPDX-License-Identifier: GPL-2.0-or-later
//! Scalar body/border alpha-mask arithmetic. Keep byte narrowing after the
//! signed shift, rather than saturating to the nominal opacity range.
pub fn scalar(source: u8, body: u8, mask: u8, color_alpha: i32, flags: u32) -> u8 {
    let coverage = if flags & 1 != 0 {
        source.saturating_sub(body)
    } else {
        source
    };
    let mut product = i64::from(coverage) * i64::from(color_alpha);
    if flags & 2 != 0 {
        product *= i64::from(mask);
        ((product + 2048) >> 12) as u8
    } else {
        ((product + 32) >> 6) as u8
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn scalar_opacity_wraps_after_rounding_and_keeps_negative_values() {
        assert_eq!(scalar(255, 0, 0, 255, 0), 248);
        assert_eq!(scalar(64, 0, 0, -1, 0), 255);
        assert_eq!(scalar(63, 64, 255, 255, 3), 0);
        assert_eq!(scalar(64, 0, 64, 255, 2), 255);
    }
}
