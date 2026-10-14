// SPDX-License-Identifier: GPL-2.0-or-later
//! Original table-driven integer Gaussian accumulation, including edge quirks.
use alloc::vec::Vec;
use core::ops::Range;
const BITS: u32 = 22;
const HALF: u32 = 1 << (BITS - 1);

#[derive(Clone, Copy)]
pub struct Kernel<'a> {
    radius: usize,
    width: usize,
    table: &'a [u32],
}
impl<'a> Kernel<'a> {
    pub fn new(radius: i32, width: i32, table: &'a [u32]) -> Option<Self> {
        if radius < 0
            || radius.checked_mul(2)?.checked_add(1)? != width
            || table.len() != (width as usize).checked_mul(256)?
        {
            return None;
        }
        Some(Self {
            radius: radius as usize,
            width: width as usize,
            table,
        })
    }
}
pub struct Output {
    pub scratch: Vec<u32>,
    pub pixels: Vec<u8>,
}
fn zeros<T: Clone + Default>(count: usize) -> Option<Vec<T>> {
    let mut result = Vec::new();
    result.try_reserve_exact(count).ok()?;
    result.resize(count, T::default());
    Some(result)
}
fn horizontal(
    scratch: &mut [u32],
    source: &[u8],
    base: usize,
    x: usize,
    range: Range<usize>,
    cumulative: i8,
    kernel: Kernel<'_>,
) -> Option<()> {
    let value = usize::from(*source.get(x)?);
    if value == 0 {
        return Some(());
    }
    let mut sum = 0u32;
    for n in 0..range.end.checked_sub(range.start)? {
        let mx = if cumulative < 0 {
            range.end - 1 - n
        } else {
            range.start + n
        };
        let coefficient = *kernel
            .table
            .get(value.checked_mul(kernel.width)?.checked_add(mx)?)?;
        sum = if cumulative == 0 {
            coefficient
        } else {
            sum.wrapping_add(coefficient)
        };
        let index = base
            .checked_add(1)?
            .checked_add(x)?
            .checked_add(mx)?
            .checked_sub(kernel.radius)?;
        let destination = scratch.get_mut(index)?;
        *destination = destination.wrapping_add(sum);
    }
    Some(())
}
fn vertical(
    scratch: &mut [u32],
    step: usize,
    col: usize,
    y: usize,
    range: Range<usize>,
    cumulative: i8,
    kernel: Kernel<'_>,
) -> Option<()> {
    let source = y.checked_mul(step)?.checked_add(col)?.checked_add(1)?;
    let value = *scratch.get(source)? as i32;
    if value == 0 {
        return Some(());
    }
    let value = usize::try_from(value.checked_add(HALF as i32)? >> BITS).ok()?;
    scratch[source] = HALF;
    let start = if cumulative < 0 {
        kernel.width - kernel.radius + 2 * y
    } else if y < kernel.radius {
        y + 1
    } else {
        y - kernel.radius
    };
    let mut sum = 0u32;
    for n in 0..range.end.checked_sub(range.start)? {
        let mx = if cumulative < 0 {
            range.end - 1 - n
        } else {
            range.start + n
        };
        let coefficient = *kernel
            .table
            .get(value.checked_mul(kernel.width)?.checked_add(mx)?)?;
        sum = if cumulative == 0 {
            coefficient
        } else {
            sum.wrapping_add(coefficient)
        };
        let row = if cumulative < 0 {
            start.checked_sub(n)?
        } else {
            start.checked_add(n)?
        };
        let index = row.checked_mul(step)?.checked_add(col)?;
        let destination = scratch.get_mut(index)?;
        *destination = destination.wrapping_add(sum);
    }
    Some(())
}

pub fn blur(
    input: &[u8],
    width: usize,
    height: usize,
    x_kernel: Kernel<'_>,
    y_kernel: Kernel<'_>,
) -> Option<Output> {
    let step = width.checked_add(1)?;
    if width < x_kernel.width
        || height < y_kernel.width
        || height < y_kernel.radius.checked_mul(3)?
        || input.len() != width.checked_mul(height)?
    {
        return None;
    }
    let mut scratch = zeros(step.checked_mul(height)?)?;
    for y in 0..height {
        let row = &input[y * width..(y + 1) * width];
        let base = y * step;
        let radius = x_kernel.radius;
        if radius > 0 {
            horizontal(
                &mut scratch,
                row,
                base,
                0,
                radius..x_kernel.width,
                -1,
                x_kernel,
            )?;
        }
        // The original starts this loop at one even when radius is zero.
        let mut x = 1;
        while x < radius {
            horizontal(
                &mut scratch,
                row,
                base,
                x,
                radius - x..x_kernel.width,
                0,
                x_kernel,
            )?;
            x += 1;
        }
        while x < width - radius {
            horizontal(&mut scratch, row, base, x, 0..x_kernel.width, 0, x_kernel)?;
            x += 1;
        }
        while x < width - 1 {
            horizontal(
                &mut scratch,
                row,
                base,
                x,
                0..radius + width - x,
                0,
                x_kernel,
            )?;
            x += 1;
        }
        if x == width - 1 {
            horizontal(
                &mut scratch,
                row,
                base,
                x,
                0..radius + width - x,
                1,
                x_kernel,
            )?;
        }
    }
    for x in 0..width {
        let radius = y_kernel.radius;
        if radius > 0 {
            vertical(
                &mut scratch,
                step,
                x,
                0,
                radius..y_kernel.width,
                -1,
                y_kernel,
            )?;
        }
        let mut y = 1;
        while y < radius {
            vertical(
                &mut scratch,
                step,
                x,
                y,
                radius - y..y_kernel.width,
                0,
                y_kernel,
            )?;
            y += 1;
        }
        while y < height - radius {
            vertical(&mut scratch, step, x, y, 0..y_kernel.width, 0, y_kernel)?;
            y += 1;
        }
        while y < height - 1 {
            vertical(
                &mut scratch,
                step,
                x,
                y,
                0..radius + height - y,
                0,
                y_kernel,
            )?;
            y += 1;
        }
        if y == height - 1 {
            vertical(
                &mut scratch,
                step,
                x,
                y,
                0..radius + height - y,
                1,
                y_kernel,
            )?;
        }
    }
    let mut pixels = zeros(width.checked_mul(height)?)?;
    for y in 0..height {
        for x in 0..width {
            pixels[y * width + x] = (scratch[y * step + x] >> BITS) as u8;
        }
    }
    Some(Output { scratch, pixels })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn radius_zero_keeps_the_original_skipped_first_row_and_column() {
        let table: Vec<u32> = (0..256).map(|value| value << BITS).collect();
        let kernel = Kernel::new(0, 1, &table).unwrap();
        let output = blur(&[1, 2, 3, 4], 2, 2, kernel, kernel).unwrap();
        assert_eq!(output.pixels, [0, 0, 0, 4]);
        assert_eq!(output.scratch, [0, 0, 2 << BITS, 0, 4 << BITS, HALF]);
    }
}
