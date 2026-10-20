// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned blur traversal. Target-specific floating-point instructions are supplied
//! through scalar methods; every pixel, column history and transpose stays safe.
use alloc::vec::Vec;

pub trait Numeric {
    fn geometry(&mut self) -> [i32; 2];
    fn passes(&mut self) -> Option<i32>;
    fn fraction(&mut self) -> Option<u32>;
    fn axis_passes(&mut self, pass: u32) -> i32;
    fn axis_filter(&mut self) -> Option<[u32; 2]>;
}

pub(crate) fn zeroed<T: Clone + Default>(len: usize) -> Result<Vec<T>, u32> {
    let mut out = Vec::new();
    out.try_reserve_exact(len).map_err(|_| 5u32)?;
    out.resize(len, T::default());
    Ok(out)
}

/// The admitted plane has nonoverlapping rows and initialized logical pixels.
/// Padding and the outermost row/column are left exactly as they were.
pub fn plane(bytes: &mut [u8], w: usize, h: usize, stride: usize) -> Result<(), u32> {
    if w < 3 || h < 3 || stride < w || stride.checked_mul(h) != Some(bytes.len()) {
        return Err(6);
    }
    let mut columns = zeroed::<u16>(w)?;
    let mut sums = zeroed::<u16>(w)?;
    let seed = |bytes: &[u8], at: usize| {
        u32::from(bytes[at + 1]) | ((u32::from(bytes[at]) + u32::from(bytes[at + 1])) << 16)
    };
    for y in 0..2 {
        let mut state = seed(bytes, y * stride);
        for x in 2..w {
            state = group(
                u64::from(bytes[y * stride + x]),
                state,
                &mut columns[x - 2..x - 1],
                &mut sums[x - 2..x - 1],
                y as u32,
                false,
            )
            .state;
        }
    }
    for y in 2..h {
        let src = y * stride;
        let dst = (y - 1) * stride;
        let mut x = 2;
        let mut state = seed(bytes, src);
        while x < ((w - 2) & !7) {
            let input = u64::from_le_bytes(bytes[src + x..src + x + 8].try_into().unwrap());
            let step = group(
                input,
                state,
                &mut columns[x - 2..x + 6],
                &mut sums[x - 2..x + 6],
                2,
                true,
            );
            state = step.state;
            bytes[dst + x - 1..dst + x + 7].copy_from_slice(&step.pixels.to_le_bytes());
            x += 8;
        }
        state = seed(bytes, src + x - 2);
        while x < w {
            let step = group(
                u64::from(bytes[src + x]),
                state,
                &mut columns[x - 2..x - 1],
                &mut sums[x - 2..x - 1],
                2,
                false,
            );
            state = step.state;
            bytes[dst + x - 1] = step.pixels as u8;
            x += 1;
        }
    }
    Ok(())
}

fn weighted_row(bytes: &mut [u8], side: u32, middle: u32) {
    let w = bytes.len();
    if w == 0 {
        return;
    }
    let mut left = 0;
    let mut center = u32::from(bytes[0]);
    let mut x = 0;
    while x < ((w - 1) & !7) {
        let next = u64::from_le_bytes(bytes[x + 1..x + 9].try_into().unwrap());
        let output = crate::be_filter::weighted_group(next, left | (center << 8), side, middle, 3);
        bytes[x..x + 8].copy_from_slice(&output.to_le_bytes());
        left = ((next >> 48) & 255) as u32;
        center = (next >> 56) as u32;
        x += 8;
    }
    while x < w - 1 {
        let old = u32::from(bytes[x]);
        bytes[x] =
            crate::be_filter::weighted_pixel(left + u32::from(bytes[x + 1]), old, side, middle, 3);
        left = old;
        x += 1;
    }
    bytes[x] = crate::be_filter::weighted_pixel(left, u32::from(bytes[x]), side, middle, 3);
}

fn axis(bytes: &mut [u8], width: usize, stride: usize, pass: u32, numeric: &mut impl Numeric) {
    let count = numeric.axis_passes(pass);
    for _ in 0..count {
        for row in bytes.chunks_exact_mut(stride) {
            crate::be_filter::row(&mut row[..width], 3);
        }
    }
    if let Some([side, middle]) = numeric.axis_filter() {
        for row in bytes.chunks_exact_mut(stride) {
            weighted_row(&mut row[..width], side, middle);
        }
    }
}

pub fn fractional(
    bytes: &mut [u8],
    w: usize,
    h: usize,
    stride: usize,
    pass: u32,
    numeric: &mut impl Numeric,
) -> Result<(), u32> {
    if w == 0 || h == 0 || stride < w || stride.checked_mul(h) != Some(bytes.len()) {
        return Err(6);
    }
    let mut scratch = zeroed::<u8>(w.checked_mul(h).ok_or(6u32)?)?;
    axis(bytes, w, stride, pass, numeric);
    for y in 0..h {
        for x in 0..w {
            scratch[x * h + y] = bytes[y * stride + x];
        }
    }
    axis(&mut scratch, h, h, pass, numeric);
    for y in 0..h {
        for x in 0..w {
            bytes[y * stride + x] = scratch[x * h + y];
        }
    }
    Ok(())
}

#[derive(Clone, Copy, Debug)]
pub struct Step {
    pub pixels: u64,
    pub state: u32,
}

pub fn group(
    source: u64,
    state: u32,
    column_pixels: &mut [u16],
    column_sums: &mut [u16],
    phase: u32,
    vector: bool,
) -> Step {
    let mut old_pixel = state & 0xffff;
    let mut old_sum = state >> 16;
    let mut output = [0u8; 8];
    for (((input, output), column_pixel), column_sum) in source
        .to_le_bytes()
        .into_iter()
        .zip(&mut output)
        .zip(column_pixels)
        .zip(column_sums)
    {
        let next_sum = old_pixel + u32::from(input);
        let horizontal = old_sum + next_sum;
        old_pixel = u32::from(input);
        old_sum = next_sum;
        if phase == 0 {
            *column_pixel = horizontal as u16;
            continue;
        }
        let sum = u32::from(*column_pixel) + horizontal;
        *column_pixel = horizontal as u16;
        if phase != 1 {
            let combined = u32::from(*column_sum) + sum;
            *output = if vector {
                ((combined as u16) >> 4).min(255) as u8
            } else {
                (combined >> 4) as u8
            };
        }
        *column_sum = sum as u16;
    }
    Step {
        pixels: u64::from_le_bytes(output),
        state: (old_pixel & 0xffff) | (old_sum << 16),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn complete_kernel_matches_spatial_convolution_and_preserves_edges_and_padding() {
        for w in 3..36 {
            for h in [3, 4, 9] {
                let stride = w + 5;
                let original: Vec<u8> = (0..stride * h).map(|i| (i * 137 + i / 3) as u8).collect();
                let mut expected = original.clone();
                for y in 1..h - 1 {
                    for x in 1..w - 1 {
                        let mut sum = 0u32;
                        for (dy, wy) in [1, 2, 1].into_iter().enumerate() {
                            for (dx, wx) in [1, 2, 1].into_iter().enumerate() {
                                sum += u32::from(original[(y + dy - 1) * stride + x + dx - 1])
                                    * wy
                                    * wx;
                            }
                        }
                        expected[y * stride + x] = (sum >> 4) as u8;
                    }
                }
                let mut actual = original;
                plane(&mut actual, w, h, stride).unwrap();
                assert_eq!(actual, expected, "{w} by {h}");
            }
        }
    }

    #[test]
    fn invalid_plane_layout_is_rejected_before_mutation() {
        for (w, h, stride) in [(2, 3, 16), (3, 2, 16), (4, 3, 3), (3, 3, usize::MAX)] {
            let mut bytes = [91; 48];
            assert_eq!(plane(&mut bytes, w, h, stride), Err(6));
            assert_eq!(bytes, [91; 48]);
        }
    }

    #[test]
    fn fractional_transpose_filters_both_axes_without_touching_padding() {
        struct Fixed;
        impl Numeric for Fixed {
            fn geometry(&mut self) -> [i32; 2] {
                unreachable!()
            }
            fn passes(&mut self) -> Option<i32> {
                unreachable!()
            }
            fn fraction(&mut self) -> Option<u32> {
                unreachable!()
            }
            fn axis_passes(&mut self, _: u32) -> i32 {
                0
            }
            fn axis_filter(&mut self) -> Option<[u32; 2]> {
                Some([64, 128])
            }
        }
        let mut bytes = [0, 0, 0, 99, 0, 64, 0, 99, 0, 0, 0, 99];
        fractional(&mut bytes, 3, 3, 4, 0, &mut Fixed).unwrap();
        assert_eq!(bytes, [4, 8, 4, 99, 8, 16, 8, 99, 4, 8, 4, 99]);
    }

    #[test]
    fn column_history_builds_before_the_first_output_row() {
        let mut pixel = [0];
        let mut sum = [0];
        let seed = |value: u32| value | ((value * 2) << 16);
        group(16, seed(16), &mut pixel, &mut sum, 0, false);
        assert_eq!((pixel, sum), ([64], [0]));
        group(32, seed(32), &mut pixel, &mut sum, 1, false);
        assert_eq!((pixel, sum), ([128], [192]));
        let result = group(48, seed(48), &mut pixel, &mut sum, 2, false);
        assert_eq!(result.pixels, 32);
        assert_eq!((pixel, sum), ([192], [320]));
    }
}
