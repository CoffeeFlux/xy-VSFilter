// SPDX-License-Identifier: GPL-3.0-or-later
//! DVD run clipping. Left clipping moves the start without shortening the run.
use core::mem::MaybeUninit;

#[derive(Debug, PartialEq, Eq)]
pub struct Span {
    pub offset: u32,
    pub length: u32,
}

/// None retains native handling of arithmetic/layouts outside the checked path.
/// A zero length is only returned for the original early rejection conditions.
pub fn clip(rect: [i32; 4], mut x: i32, y: i32, mut length: i32) -> Option<Span> {
    let [left, top, right, bottom] = rect;
    if length <= 0 || x.checked_add(length)? < left || x >= right || y < top || y >= bottom {
        return Some(Span {
            offset: 0,
            length: 0,
        });
    }
    if x < left {
        x = left;
    }
    if x.checked_add(length)? >= right {
        length = right.checked_sub(x)?;
    }
    let width = right.checked_sub(left)?;
    if width <= 0 || length <= 0 {
        return None;
    }
    let offset = width
        .checked_mul(y.checked_sub(top)?)?
        .checked_add(x.checked_sub(left)?)?;
    Some(Span {
        offset: u32::try_from(offset).ok()?,
        length: length as u32,
    })
}

/// Write complete packed pixels without reading previous output or padding.
pub fn fill(destination: &mut [MaybeUninit<u8>], color: [u8; 4]) {
    for pixel in destination.chunks_exact_mut(4) {
        for (slot, byte) in pixel.iter_mut().zip(color) {
            slot.write(byte);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn left_clipping_retains_the_original_full_run_length() {
        assert_eq!(
            clip([10, 20, 30, 25], 8, 21, 5),
            Some(Span {
                offset: 20,
                length: 5
            })
        );
        // Ending exactly at the left edge still paints after moving the start.
        assert_eq!(
            clip([10, 20, 30, 25], 5, 20, 5),
            Some(Span {
                offset: 0,
                length: 5
            })
        );
        assert_eq!(
            clip([10, 20, 30, 25], 4, 20, 5),
            Some(Span {
                offset: 0,
                length: 0
            })
        );
    }
    #[test]
    fn rejection_precedes_later_arithmetic_and_palette_access() {
        assert_eq!(
            clip([i32::MIN, i32::MIN, i32::MAX, i32::MAX], i32::MAX, 0, 0),
            Some(Span {
                offset: 0,
                length: 0
            })
        );
        assert_eq!(clip([0, 0, 10, 10], i32::MAX, 0, 1), None);
    }
}
