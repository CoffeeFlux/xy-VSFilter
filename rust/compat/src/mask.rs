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

pub fn banner_fade(data: &mut [u8], width: usize, fade: i32, delta: i32) -> bool {
    if width == 0 || width > i32::MAX as usize || data.len() % width != 0 || fade <= 0 {
        return false;
    }
    if delta != 16384 / fade {
        return false;
    }
    let fade = fade as usize;
    let delta = delta as u32;
    for row in data.chunks_exact_mut(width) {
        let mut alpha = 0;
        for byte in &mut row[..fade.min(width)] {
            *byte = ((u32::from(*byte) * alpha) >> 14) as u8;
            alpha += delta;
        }
        alpha = 16384 - (fade.saturating_sub(width) as u32) * delta;
        for byte in &mut row[width.saturating_sub(fade)..] {
            *byte = ((u32::from(*byte) * alpha) >> 14) as u8;
            alpha -= delta;
        }
    }
    true
}

/// One native scroll pass. Geometry and floating-point conversions stay in
/// their original caller order; all bounds/arithmetic are checked before writes.
#[allow(clippy::too_many_arguments)]
pub fn scroll_fade(
    data: &mut [u8],
    width: usize,
    first: i32,
    last: i32,
    alpha: i32,
    delta: i32,
    clear_before: bool,
    clear_after: bool,
) -> bool {
    if width == 0 || data.len() > i32::MAX as usize || data.len() % width != 0 || first < 0 {
        return false;
    }
    let height = data.len() / width;
    if first as usize >= height || i64::from(last) > height as i64 {
        return false;
    }
    let rows = (i64::from(last) - i64::from(first)).max(0);
    let final_alpha = i64::from(alpha) + rows * i64::from(delta);
    if i32::try_from(final_alpha).is_err() {
        return false;
    }
    if rows > 0 {
        let last_alpha = final_alpha - i64::from(delta);
        if i32::try_from(i64::from(alpha) * 255).is_err()
            || i32::try_from(last_alpha * 255).is_err()
        {
            return false;
        }
    }
    let start = first as usize * width;
    let end = (first as usize + rows as usize) * width;
    if clear_before {
        data[..start].fill(0);
    }
    let mut alpha = alpha;
    for row in data[start..end].chunks_exact_mut(width) {
        for byte in row {
            *byte = ((i32::from(*byte) * alpha) >> 14) as u8;
        }
        alpha += delta;
    }
    if clear_after {
        data[end..].fill(0);
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
    fn overlapping_banner_edges_apply_twice_in_native_order() {
        let mut pixels = [64; 3];
        assert!(banner_fade(&mut pixels, 3, 4, 4096));
        assert_eq!(pixels, [0, 8, 8]);
    }
    #[test]
    fn empty_scroll_interval_still_clears_the_requested_side() {
        let mut pixels = [64; 6];
        assert!(scroll_fade(
            &mut pixels,
            2,
            1,
            -3,
            16384,
            -4096,
            false,
            true
        ));
        assert_eq!(pixels, [64, 64, 0, 0, 0, 0]);
        let before = pixels;
        assert!(!scroll_fade(&mut pixels, 2, 0, 3, i32::MAX, 1, true, true));
        assert_eq!(pixels, before);
    }
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
