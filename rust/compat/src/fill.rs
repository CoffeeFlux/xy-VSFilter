// SPDX-License-Identifier: GPL-2.0-or-later
//! Exact packed and planar pixel blends used by the native rasterizer.
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

/// Additive packed blend: saturate color upward and inverse alpha downward.
pub fn add_pixel(dst: u32, alpha: u8, color: u32) -> u32 {
    let mut pixel = dst.to_le_bytes();
    let source = color.to_le_bytes();
    let weight = u16::from(alpha) + 1;
    for i in 0..3 {
        pixel[i] = pixel[i].saturating_add(((u16::from(source[i]) * weight) >> 8) as u8);
    }
    pixel[3] = pixel[3].saturating_sub(alpha);
    u32::from_le_bytes(pixel)
}

pub fn add_block(dst: &mut [u8], alpha: [u8; 16], color: u32) {
    for (pixel, alpha) in dst.chunks_exact_mut(4).zip(alpha) {
        let original = u32::from_le_bytes([pixel[0], pixel[1], pixel[2], pixel[3]]);
        pixel.copy_from_slice(&add_pixel(original, alpha, color).to_le_bytes());
    }
}

pub fn masked_plane_block(dst: &mut [u8], alpha: [u8; 16], value: u8) {
    for (byte, alpha) in dst.iter_mut().zip(alpha) {
        *byte = channel(*byte, value, u16::from(alpha) + 1, 256 - u16::from(alpha));
    }
}

/// Scalar packed arithmetic, including cross-lane carry for wider alpha values.
/// The native adapter retains its original path where signed setup overflows.
pub fn mix_pixel_scalar(dst: u32, color: u32, alpha: u32) -> u32 {
    let inverse = 256u32.wrapping_sub(alpha);
    let weight = alpha.wrapping_add(1);
    let rb = (dst & 0x00ff00ff)
        .wrapping_mul(inverse)
        .wrapping_add((color & 0x00ff00ff).wrapping_mul(weight));
    let g = (dst & 0x0000ff00)
        .wrapping_mul(inverse)
        .wrapping_add((color & 0x0000ff00).wrapping_mul(weight));
    let a = ((dst >> 8) & 0x00ff0000).wrapping_mul(inverse);
    ((rb & 0xff00ff00) >> 8) | ((g & 0x00ff0000) >> 8) | (a & 0xff000000)
}

/// The SSE helper constructs its two signed 16-bit weights with a bitwise OR,
/// then logically shifts and saturates the products. Wider alpha values must
/// retain this sequence rather than being narrowed to a byte.
pub fn mix_pixel_sse(dst: u32, color: u32, alpha: u32) -> u32 {
    let weights = (alpha.wrapping_add(1) << 16) | 256u32.wrapping_sub(alpha);
    let inverse = i32::from(weights as i16);
    let weight = i32::from((weights >> 16) as i16);
    let mut output = dst.to_le_bytes();
    let source = (color & 0x00ffffff).to_le_bytes();
    for i in 0..4 {
        // Byte * signed word products and their sum fit in i32.
        let product = i32::from(output[i]) * inverse + i32::from(source[i]) * weight;
        // Logical shift makes even negative products positive. The two native
        // packing operations then saturate the result to the byte range.
        output[i] = ((product as u32) >> 8).min(255) as u8;
    }
    u32::from_le_bytes(output)
}

pub fn mix_pixel_scaled(dst: u32, color: u32, shape: u32, clip: u32) -> u32 {
    let alpha = (shape.wrapping_mul(clip).wrapping_mul(color >> 24) >> 12) & 255;
    mix_pixel_sse(dst, color, alpha)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn wider_alpha_keeps_the_original_sse_weight_construction() {
        assert_eq!(mix_pixel_sse(0, 0xffffff, 257), 0x00ffffff);
        assert_eq!(mix_pixel_scalar(0, 0xffffff, 257), 0x00000000);
        assert_eq!(mix_pixel_sse(0xffffffff, 0, 0xffffffff), 0xffffffff);
    }
    #[test]
    fn additive_color_and_inverse_alpha_saturate_independently() {
        assert_eq!(add_pixel(0x8010fe00, 128, 0x993020ff), 0x0028ff80);
        assert_eq!(add_pixel(0xff123456, 0, 0xffffffff), 0xff123456);
    }
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
