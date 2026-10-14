// SPDX-License-Identifier: GPL-2.0-or-later
//! Fixed 3x3 blur groups with scalar or SSE read/write ordering supplied by the
//! native adapter. Column storage is borrowed only for the current group.

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
