// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned Gaussian scratch and the cleanup renderer's SSE filter traversal.
//! Words are binary32 representations, including signed zero and subnormals.
use crate::be_blur::zeroed;
use alloc::vec::Vec;

pub trait Numeric {
    fn mul(&mut self, left: [u32; 4], right: [u32; 4]) -> [u32; 4];
    fn add(&mut self, left: [u32; 4], right: [u32; 4]) -> [u32; 4];
    fn probe(&mut self, value: u32) -> u32;
    fn equal(&mut self, left: u32, right: u32) -> bool;
    fn bytes(&mut self, values: [u32; 16]) -> [u8; 16];
    fn byte(&mut self, value: u32) -> u8;
}

pub struct Kernel {
    radius: usize,
    words: Vec<u32>,
}
impl Kernel {
    pub fn radius(&self) -> usize {
        self.radius
    }
    pub fn new(radius: i32, words: Vec<u32>) -> Result<Self, u32> {
        let radius = usize::try_from(radius).map_err(|_| 6u32)?;
        let required = radius
            .checked_mul(2)
            .and_then(|n| n.checked_add(4))
            .ok_or(6u32)?
            & !3;
        if words.len() != required {
            return Err(6);
        }
        Ok(Self { radius, words })
    }
}

/// Two signed radii, two word counts, then the two little-endian coefficient arrays.
pub fn kernels(packet: &[u8]) -> Result<[Kernel; 2], u32> {
    if packet.len() < 16 {
        return Err(6);
    }
    let header: [u32; 4] =
        core::array::from_fn(|i| u32::from_le_bytes(packet[i * 4..i * 4 + 4].try_into().unwrap()));
    let count = (header[2] as usize)
        .checked_add(header[3] as usize)
        .ok_or(6u32)?;
    if count.checked_mul(4).and_then(|n| n.checked_add(16)) != Some(packet.len()) {
        return Err(6);
    }
    let mut at = 16;
    let mut read = |radius: u32, count: u32| {
        let mut words = zeroed::<u32>(count as usize)?;
        for word in &mut words {
            *word = u32::from_le_bytes(packet[at..at + 4].try_into().unwrap());
            at += 4;
        }
        Kernel::new(radius as i32, words)
    };
    Ok([read(header[0], header[2])?, read(header[1], header[3])?])
}

fn window(first: [u32; 4], next: [u32; 4], shift: usize) -> [u32; 4] {
    core::array::from_fn(|i| {
        if i + shift < 4 {
            first[i + shift]
        } else {
            next[i + shift - 4]
        }
    })
}
fn group(
    n: &mut impl Numeric,
    first: [u32; 4],
    next: [u32; 4],
    coefficients: &[u32],
    mut sum: [u32; 4],
) -> [u32; 4] {
    // This order is observable: the native macro accumulates 0,2,1,3.
    for i in [0, 2, 1, 3] {
        let product = n.mul([coefficients[i]; 4], window(first, next, i));
        sum = n.add(sum, product);
    }
    sum
}
fn block(row: &[u32], at: usize, prefix: usize, width: usize) -> [u32; 4] {
    if at < prefix || at >= prefix + width {
        [0; 4]
    } else {
        row[at..at + 4].try_into().unwrap()
    }
}
#[allow(clippy::too_many_arguments)]
fn step(
    n: &mut impl Numeric,
    row: &[u32],
    pos: usize,
    prefix: usize,
    width: usize,
    filter: &[u32],
    f: usize,
    sum: [u32; 4],
) -> [u32; 4] {
    group(
        n,
        block(row, pos + f, prefix, width),
        block(row, pos + f + 4, prefix, width),
        &filter[f..f + 4],
        sum,
    )
}
fn almost_zero(bits: u32) -> bool {
    let signed = bits as i32;
    let ordered = if signed < 0 {
        i32::MIN.wrapping_sub(signed)
    } else {
        signed
    };
    ordered.wrapping_abs() <= 0
}

/// Each row owns a coefficient-width prefix followed by `width` source words.
/// Only that prefix and source are written; extra stride words are untouched.
pub fn filter(
    words: &mut [u32],
    width: usize,
    height: usize,
    stride: usize,
    coefficients: &[u32],
    n: &mut impl Numeric,
) -> Result<(), u32> {
    let m = coefficients.len();
    if width == 0
        || m == 0
        || width & 3 != 0
        || m & 3 != 0
        || width.checked_add(m).is_none_or(|v| v > stride)
        || stride.checked_mul(height) != Some(words.len())
    {
        return Err(6);
    }
    let mut length = m;
    let fixed = m <= 28 && m <= width;
    let symmetric = if fixed {
        // The probe is evaluated even when three zeros have already been skipped.
        while almost_zero(n.probe(coefficients[length - 1])) && m - length < 3 {
            length -= 1;
        }
        length == 3 && n.equal(coefficients[0], coefficients[2])
    } else {
        false
    };
    if height == 0 {
        return Ok(());
    }
    if symmetric {
        // MSVC hoists this product before the row loop.
        let zero_side = n.mul([0; 4], [coefficients[0]; 4]);
        for row in words.chunks_exact_mut(stride) {
            for pos in (0..m + width).step_by(4) {
                let first = block(row, pos, m, width);
                let next = block(row, pos + 4, m, width);
                let middle = n.mul(window(first, next, 1), [coefficients[1]; 4]);
                let left = if pos == 0 {
                    zero_side
                } else {
                    n.mul(first, [coefficients[0]; 4])
                };
                let right = n.mul(window(first, next, 2), [coefficients[0]; 4]);
                let sum = n.add(middle, left);
                row[pos..pos + 4].copy_from_slice(&n.add(sum, right));
            }
        }
    } else if fixed {
        if length & 3 != 1 {
            length = (length + 3) & !3;
        }
        for row in words.chunks_exact_mut(stride) {
            for pos in (0..m + width).step_by(4) {
                let mut sum = [0; 4];
                if pos != 0 || length & 3 != 1 {
                    let start = m.saturating_sub(pos + 4);
                    let end = length.min(m + width - pos);
                    for f in (start..end).step_by(4) {
                        if end - f == 1 {
                            let product =
                                n.mul(block(row, pos + f, m, width), [coefficients[f]; 4]);
                            sum = n.add(sum, product);
                        } else {
                            sum = step(n, row, pos, m, width, coefficients, f, sum);
                        }
                    }
                }
                row[pos..pos + 4].copy_from_slice(&sum);
            }
        }
    } else {
        for row in words.chunks_exact_mut(stride) {
            let mut pos = 0;
            let mut start = m;
            let mut end = m;
            while pos < m.min(width) {
                start -= 4;
                let mut sum = [0; 4];
                for f in (start..end).step_by(4) {
                    sum = step(n, row, pos, m, width, coefficients, f, sum);
                }
                row[pos..pos + 4].copy_from_slice(&sum);
                pos += 4;
            }
            while pos < m {
                start -= 4;
                end -= 4;
                let mut sum = [0; 4];
                for f in (start..=end).step_by(4) {
                    sum = step(n, row, pos, m, width, coefficients, f, sum);
                }
                row[pos..pos + 4].copy_from_slice(&sum);
                pos += 4;
            }
            // Interleave the two accumulators in exactly the v6 group order.
            while pos + 4 < width {
                let mut sum = step(n, row, pos, m, width, coefficients, 0, [0; 4]);
                let mut sum2 = [0; 4];
                for f in (4..end).step_by(4) {
                    sum2 = step(n, row, pos + 4, m, width, coefficients, f - 4, sum2);
                    sum = step(n, row, pos, m, width, coefficients, f, sum);
                }
                sum2 = step(n, row, pos + 4, m, width, coefficients, end - 4, sum2);
                row[pos..pos + 4].copy_from_slice(&sum);
                row[pos + 4..pos + 8].copy_from_slice(&sum2);
                pos += 8;
            }
            if pos + 4 == width {
                let mut sum = [0; 4];
                for f in (0..end).step_by(4) {
                    sum = step(n, row, pos, m, width, coefficients, f, sum);
                }
                row[pos..pos + 4].copy_from_slice(&sum);
                pos += 4;
            }
            while pos < m + width {
                end -= 4;
                let mut sum = [0; 4];
                for f in (0..=end).step_by(4) {
                    sum = step(n, row, pos, m, width, coefficients, f, sum);
                }
                row[pos..pos + 4].copy_from_slice(&sum);
                pos += 4;
            }
        }
    }
    Ok(())
}

/// Byte input is already admitted as initialized logical rows by the image owner.
/// Output padding is untouched; private scratch prefixes are never read as input.
#[allow(clippy::too_many_arguments)]
pub fn plane(
    source: &[u8],
    width: usize,
    height: usize,
    stride: usize,
    output: &mut [u8],
    pitch: usize,
    x: &Kernel,
    y: &Kernel,
    n: &mut impl Numeric,
) -> Result<(), u32> {
    let tw = width
        .checked_add(x.radius.checked_mul(2).ok_or(6u32)?)
        .ok_or(6u32)?;
    let th = height
        .checked_add(y.radius.checked_mul(2).ok_or(6u32)?)
        .ok_or(6u32)?;
    if width == 0
        || height == 0
        || stride < width
        || pitch < tw
        || stride.checked_mul(height) != Some(source.len())
        || pitch.checked_mul(th) != Some(output.len())
    {
        return Err(6);
    }
    let fw = width.checked_add(3).ok_or(6u32)? & !3;
    let fh = height.checked_add(3).ok_or(6u32)? & !3;
    let hs = fw.checked_add(x.words.len()).ok_or(6u32)?;
    let vs = fh.checked_add(y.words.len()).ok_or(6u32)?;
    let mut horizontal = zeroed::<u32>(hs.checked_mul(height).ok_or(5u32)?)?;
    let mut vertical = zeroed::<u32>(vs.checked_mul(tw).ok_or(5u32)?)?;
    for row in 0..height {
        crate::byte_float::row(
            &source[row * stride..row * stride + width],
            &mut horizontal[row * hs + x.words.len()..(row + 1) * hs],
        );
    }
    filter(&mut horizontal, fw, height, hs, &x.words, n)?;
    let hx = x.words.len() - x.radius * 2;
    for col in 0..tw {
        for row in 0..height {
            let value = horizontal[row * hs + hx + col];
            vertical[col * vs + y.words.len() + row] = if row >= height & !3 {
                n.probe(value)
            } else {
                value
            };
        }
    }
    filter(&mut vertical, fh, tw, vs, &y.words, n)?;
    let vy = y.words.len() - y.radius * 2;
    for row in 0..th {
        let mut col = 0;
        while col < tw & !15 {
            let input = core::array::from_fn(|i| vertical[(col + i) * vs + vy + row]);
            output[row * pitch + col..row * pitch + col + 16].copy_from_slice(&n.bytes(input));
            col += 16;
        }
        while col < tw {
            output[row * pitch + col] = n.byte(vertical[col * vs + vy + row]);
            col += 1;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    struct Arithmetic;
    impl Numeric for Arithmetic {
        fn mul(&mut self, a: [u32; 4], b: [u32; 4]) -> [u32; 4] {
            core::array::from_fn(|i| (f32::from_bits(a[i]) * f32::from_bits(b[i])).to_bits())
        }
        fn add(&mut self, a: [u32; 4], b: [u32; 4]) -> [u32; 4] {
            core::array::from_fn(|i| (f32::from_bits(a[i]) + f32::from_bits(b[i])).to_bits())
        }
        fn probe(&mut self, v: u32) -> u32 {
            v
        }
        fn equal(&mut self, a: u32, b: u32) -> bool {
            f32::from_bits(a) == f32::from_bits(b)
        }
        fn bytes(&mut self, v: [u32; 16]) -> [u8; 16] {
            v.map(|v| f32::from_bits(v) as u8)
        }
        fn byte(&mut self, v: u32) -> u8 {
            f32::from_bits(v) as u8
        }
    }
    #[test]
    fn identity_preserves_pixels_and_unwritten_padding_through_both_transposes() {
        let k = Kernel::new(0, alloc::vec![1f32.to_bits(), 0, 0, 0]).unwrap();
        for width in 1..34 {
            for height in 1..8 {
                let stride = width + 3;
                let pitch = width + 5;
                let mut source = alloc::vec![0xee;stride*height];
                for y in 0..height {
                    for x in 0..width {
                        source[y * stride + x] = ((x * 17 + y * 23) % 256) as u8;
                    }
                }
                let mut out = alloc::vec![0xa5;pitch*height];
                plane(
                    &source,
                    width,
                    height,
                    stride,
                    &mut out,
                    pitch,
                    &k,
                    &k,
                    &mut Arithmetic,
                )
                .unwrap();
                for y in 0..height {
                    assert_eq!(
                        &out[y * pitch..y * pitch + width],
                        &source[y * stride..y * stride + width]
                    );
                    assert!(out[y * pitch + width..(y + 1) * pitch]
                        .iter()
                        .all(|&v| v == 0xa5));
                }
            }
        }
    }
    #[test]
    fn malformed_shapes_refuse_without_touching_destination() {
        assert!(Kernel::new(-1, alloc::vec![0;4]).is_err());
        assert!(Kernel::new(2, alloc::vec![0;4]).is_err());
        assert!(kernels(&[0; 16]).is_err());
        let mut out = [0xa5; 12];
        assert_eq!(filter(&mut out, 4, 1, 12, &[0; 3], &mut Arithmetic), Err(6));
        assert_eq!(out, [0xa5; 12]);
        assert_eq!(
            filter(&mut out, usize::MAX, 1, 12, &[0; 4], &mut Arithmetic),
            Err(6)
        );
        assert_eq!(out, [0xa5; 12]);
    }
}
