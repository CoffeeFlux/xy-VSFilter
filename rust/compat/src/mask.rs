// SPDX-License-Identifier: GPL-2.0-or-later
//! Scalar body/border alpha-mask arithmetic. Keep byte narrowing after the
//! signed shift, rather than saturating to the nominal opacity range.
use core::mem::MaybeUninit;

fn clip_byte(source: u8, inverse: bool) -> u8 {
    if inverse {
        64u8.wrapping_sub(source)
    } else {
        source
    }
}

/// Vector clip bytes narrow after subtraction, even outside nominal 0..64
/// coverage. Only active pixels are copied; callers retain row padding.
pub fn clip_row(destination: &mut [MaybeUninit<u8>], source: &[u8], inverse: bool) -> bool {
    if destination.len() != source.len() {
        return false;
    }
    for (out, &input) in destination.iter_mut().zip(source) {
        out.write(clip_byte(input, inverse));
    }
    true
}

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

/// The four-byte MMX path uses signed high-word multiplication for masked
/// opacity; the sixteen-byte SSE path uses unsigned high-word multiplication.
pub fn block(
    source: [u8; 16],
    body: [u8; 16],
    mask: [u8; 16],
    color_alpha: i32,
    flags: u32,
    mmx: bool,
) -> [u8; 16] {
    let mut output = [0; 16];
    for i in 0..16 {
        let coverage = if flags & 1 != 0 {
            source[i].saturating_sub(body[i])
        } else {
            source[i]
        };
        let product = u16::from(coverage).wrapping_mul(color_alpha as u16);
        let value = if flags & 2 != 0 {
            let mask_word = u16::from(mask[i]) << 8;
            let high = if mmx {
                ((i32::from(product as i16) * i32::from(mask_word as i16)) >> 16) as u16
            } else {
                ((u32::from(product) * u32::from(mask_word)) >> 16) as u16
            };
            high.saturating_add(8) >> 4
        } else {
            product.saturating_add(32) >> 6
        };
        output[i] = value.min(255) as u8;
    }
    output
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn clip_inversion_wraps_outside_nominal_coverage() {
        assert_eq!(
            [0, 64, 65, 255].map(|b| clip_byte(b, true)),
            [64, 0, 255, 65]
        );
        let mut output = [MaybeUninit::new(19); 4];
        assert!(!clip_row(&mut output, &[0, 64, 255], true));
        assert!(clip_row(&mut output, &[0, 64, 65, 255], true));
    }
    #[test]
    fn mmx_signed_mask_products_and_sse_unsigned_products_stay_distinct() {
        assert_eq!(block([16; 16], [0; 16], [255; 16], 64, 2, true), [255; 16]);
        assert_eq!(block([16; 16], [0; 16], [255; 16], 64, 2, false), [64; 16]);
        assert_eq!(block([255; 16], [0; 16], [0; 16], 255, 0, false), [255; 16]);
    }
    #[test]
    fn scalar_opacity_wraps_after_rounding_and_keeps_negative_values() {
        assert_eq!(scalar(255, 0, 0, 255, 0), 248);
        assert_eq!(scalar(64, 0, 0, -1, 0), 255);
        assert_eq!(scalar(63, 64, 255, 255, 3), 0);
        assert_eq!(scalar(64, 0, 64, 255, 2), 255);
    }
}
