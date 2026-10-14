// SPDX-License-Identifier: GPL-3.0-or-later
//! Initialized-element transposition for the disjoint, positive-pitch matrix layout.
use alloc::vec::Vec;

pub struct Layout {
    pub width: usize,
    pub height: usize,
    pub source_stride: usize,
    pub destination_width: usize,
    pub destination_stride: usize,
    pub source_span: usize,
    pub destination_span: usize,
}

impl Layout {
    pub fn new(
        width: i32,
        height: i32,
        source_stride: i32,
        destination_width: i32,
        destination_stride: i32,
    ) -> Option<Self> {
        if width <= 0
            || height <= 0
            || source_stride < width
            || destination_width < height
            || destination_stride < destination_width
        {
            return None;
        }
        // Retain native fallback when its signed end-pointer calculations
        // cannot be represented, even on a wider host.
        height.checked_mul(source_stride)?;
        width.checked_mul(destination_stride)?;
        let (width, height, source_stride, destination_width, destination_stride) = (
            width as usize,
            height as usize,
            source_stride as usize,
            destination_width as usize,
            destination_stride as usize,
        );
        Some(Self {
            width,
            height,
            source_stride,
            destination_width,
            destination_stride,
            source_span: (height - 1) * source_stride + width,
            destination_span: (width - 1) * destination_stride + destination_width,
        })
    }

    /// Input is packed initialized rows. Output is packed transposed rows,
    /// including the original zero extension but excluding native pitch gaps.
    pub fn transpose<T: Copy + Default>(&self, input: &[T]) -> Option<Vec<T>> {
        if input.len() != self.width.checked_mul(self.height)? {
            return None;
        }
        let count = self.width.checked_mul(self.destination_width)?;
        let mut output = Vec::new();
        output.try_reserve_exact(count).ok()?;
        output.resize(count, T::default());
        for x in 0..self.width {
            for y in 0..self.height {
                output[x * self.destination_width + y] = input[y * self.width + x];
            }
        }
        Some(output)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn zero_extension_is_output_data_but_pitch_gaps_are_not() {
        let layout = Layout::new(2, 3, 4, 5, 7).unwrap();
        assert_eq!((layout.source_span, layout.destination_span), (10, 12));
        assert_eq!(
            layout.transpose(&[1, 2, 3, 4, 5, 6]).unwrap(),
            [1, 3, 5, 0, 0, 2, 4, 6, 0, 0]
        );
        assert!(Layout::new(5, 3, 4, 3, 3).is_none());
        assert!(Layout::new(2, i32::MAX, 2, i32::MAX, i32::MAX).is_none());
    }
}
