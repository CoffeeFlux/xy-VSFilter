// SPDX-License-Identifier: GPL-3.0-or-later
//! Original horizontal [1, 2, 1] filtering and its four rounding policies.

fn rounded(sum: u16, policy: u32) -> u8 {
    let patch = match policy {
        0 => 0,
        1 => 1,
        2 => 2,
        _ => 2 + ((sum >> 2) & 1),
    };
    ((sum + patch) >> 2) as u8
}

pub fn row(bytes: &mut [u8], policy: u32) -> bool {
    if policy > 3 {
        return false;
    }
    let Some(&first) = bytes.first() else {
        return true;
    };
    let mut old_sum = u16::from(first);
    for x in 0..bytes.len() - 1 {
        let new_sum = u16::from(bytes[x]) + u16::from(bytes[x + 1]);
        bytes[x] = rounded(old_sum + new_sum, policy);
        old_sum = new_sum;
    }
    let last = bytes.len() - 1;
    bytes[last] = rounded(old_sum + u16::from(bytes[last]), policy);
    true
}

/// Scalar weighted filtering uses full-width unsigned wrapping arithmetic,
/// followed by the original signed shift and byte narrowing (same low byte).
pub fn weighted_pixel(neighbors: u32, center: u32, side: u32, middle: u32, policy: u32) -> u8 {
    let sum = neighbors
        .wrapping_mul(side)
        .wrapping_add(center.wrapping_mul(middle));
    let patch = match policy {
        0 => 0,
        1 => 127,
        2 => 128,
        _ => 128 + ((sum >> 8) & 1),
    };
    (sum.wrapping_add(patch) >> 8) as u8
}

/// An SSE group snapshots the next eight bytes and both coefficients before
/// output. Unlike the scalar tail, it wraps each product to a word and then
/// saturates additions. The named even policy examines the already patched sum.
pub fn weighted_group(next: u64, state: u32, side: u32, middle: u32, policy: u32) -> u64 {
    let mut left = (state & 255) as u16;
    let mut center = ((state >> 8) & 255) as u16;
    let mut output = 0;
    for i in 0..8 {
        let right = ((next >> (i * 8)) & 255) as u16;
        let sum = (left + right)
            .wrapping_mul(side as u16)
            .saturating_add(center.wrapping_mul(middle as u16));
        let rounded = match policy {
            0 => sum,
            1 => sum.saturating_add(127),
            2 => sum.saturating_add(128),
            _ => {
                let patched = sum.saturating_add(128);
                patched.saturating_add((patched >> 8) & 1)
            }
        };
        output |= u64::from(rounded >> 8) << (i * 8);
        left = center;
        center = right;
    }
    output
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn named_even_policy_keeps_the_original_pre_shift_patch() {
        let mut pixels = [1, 3, 0];
        assert!(row(&mut pixels, 3));
        assert_eq!(pixels, [2, 2, 1]);
        let mut single = [255];
        assert!(row(&mut single, 0));
        assert_eq!(single, [127]);
    }

    #[test]
    fn weighted_vector_products_wrap_then_saturate_independently() {
        let group = weighted_group(u64::MAX, 0xffff, 256, 256, 0);
        assert_eq!(group, u64::MAX);
        assert_eq!(weighted_pixel(510, 255, 256, 256, 0), 253);
        assert_eq!(weighted_pixel(0, 255, u32::MAX, u32::MAX, 3), 255);
    }
}
