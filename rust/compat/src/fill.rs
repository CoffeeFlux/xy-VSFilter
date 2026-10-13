// SPDX-License-Identifier: GPL-2.0-or-later
//! Exact constant-alpha row blends used by the native solid-rectangle filler.
//! The legacy source weight is alpha + 1; destination weight is 256 - alpha.

fn channel(dst: u8, source: u8, source_weight: u16, destination_weight: u16) -> u8 {
    // The maximum sum is 255 * 257 = 65535, including both endpoints.
    ((u16::from(dst) * destination_weight + u16::from(source) * source_weight) >> 8) as u8
}

/// Windows packed byte order B, G, R, inverse alpha. A trailing partial pixel
/// is untouched; the native bridge supplies an integral number of pixels.
pub fn packed_row(dst: &mut [u8], alpha: u8, color: u32) {
    let source_weight = u16::from(alpha) + 1;
    let destination_weight = 256 - u16::from(alpha);
    let [b, g, r, _] = color.to_le_bytes();
    for pixel in dst.chunks_exact_mut(4) {
        pixel[0] = channel(pixel[0], b, source_weight, destination_weight);
        pixel[1] = channel(pixel[1], g, source_weight, destination_weight);
        pixel[2] = channel(pixel[2], r, source_weight, destination_weight);
        pixel[3] = ((u16::from(pixel[3]) * destination_weight) >> 8) as u8;
    }
}

pub fn plane_row(dst: &mut [u8], alpha: u8, value: u8) {
    let source_weight = u16::from(alpha) + 1;
    let destination_weight = 256 - u16::from(alpha);
    for byte in dst {
        *byte = channel(*byte, value, source_weight, destination_weight);
    }
}

/// One legacy SSE group. The caller snapshots these 16 alpha bytes before
/// lending the destination, preserving aliasing with an earlier/later group.
pub fn masked_block(dst: &mut [u8], alpha: [u8; 16], color: u32) {
    for (pixel, alpha) in dst.chunks_exact_mut(4).zip(alpha) {
        packed_row(pixel, alpha, color);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn packed_color_ignores_its_own_alpha_and_preserves_partial_tail() {
        let mut row = [255, 128, 0, 255, 17];
        packed_row(&mut row, 128, 0xfe204080);
        assert_eq!(row, [192, 96, 16, 127, 17]);
    }
    #[test]
    fn extreme_channel_products_fit_without_changing_rounding() {
        for alpha in 0..=255 {
            let mut row = [255];
            plane_row(&mut row, alpha, 255);
            assert_eq!(row, [255]);
        }
        let mut row = [0, 1, 127, 255];
        plane_row(&mut row, 0, 255);
        assert_eq!(row, [0, 1, 127, 255]);
        plane_row(&mut row, 255, 17);
        assert_eq!(row, [17; 4]);
    }
}
