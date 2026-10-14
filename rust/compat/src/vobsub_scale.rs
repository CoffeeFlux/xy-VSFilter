// SPDX-License-Identifier: GPL-3.0-or-later
//! The native Scale2x method is an edge filter which keeps the image dimensions.
use core::mem::MaybeUninit;

pub fn pixel(b: u32, d: u32, e: u32, f: u32, h: u32) -> u32 {
    let e0 = if d == b && b != f && d != h { d } else { e };
    let e1 = if b == f && b != d && f != h { f } else { e };
    let e2 = if d == h && d != b && h != f { d } else { e };
    let e3 = if h == f && d != h && b != f { f } else { e };
    // The +2 applies only to the lower channel in each packed pair. Preserve
    // the resulting different rounding of blue/green versus red/alpha.
    let lo = (e0 & 0x00ff00ff) + (e1 & 0x00ff00ff) + (e2 & 0x00ff00ff) + (e3 & 0x00ff00ff) + 2;
    let hi = ((e0 >> 8) & 0x00ff00ff)
        + ((e1 >> 8) & 0x00ff00ff)
        + ((e2 >> 8) & 0x00ff00ff)
        + ((e3 >> 8) & 0x00ff00ff)
        + 2;
    ((lo >> 2) & 0x00ff00ff) | ((hi << 6) & 0xff00ff00)
}

pub fn filter(
    source: &[u32],
    destination: &mut [MaybeUninit<u32>],
    width: usize,
    height: usize,
) -> bool {
    if width == 0
        || height == 0
        || width.checked_mul(height) != Some(source.len())
        || destination.len() != source.len()
    {
        return false;
    }
    for y in 0..height {
        for x in 0..width {
            let i = y * width + x;
            let e = source[i];
            let b = if y > 0 { source[i - width] } else { e };
            let d = if x > 0 { source[i - 1] } else { e };
            let f = if x < width - 1 { source[i + 1] } else { e };
            let h = if y < height - 1 { source[i + width] } else { e };
            destination[i].write(pixel(b, d, e, f, h));
        }
    }
    true
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn packed_pair_bias_does_not_round_red_and_alpha() {
        assert_eq!(pixel(0, 0, 0, u32::MAX, u32::MAX), 0x3f3f4040);
        assert_eq!(
            pixel(0x12345678, 0x12345678, 0x12345678, 0x12345678, 0x12345678),
            0x12345678
        );
    }
}
