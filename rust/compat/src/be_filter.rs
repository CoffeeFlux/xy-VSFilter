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
}
