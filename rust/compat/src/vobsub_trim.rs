// SPDX-License-Identifier: GPL-3.0-or-later
//! Alpha-only bounding scans and bordered copies that can retain uninitialized RGB.
use alloc::vec::Vec;
use core::mem::MaybeUninit;

pub fn bounds(width: i32, height: i32, mut alpha: impl FnMut(usize) -> u8) -> Option<[i32; 4]> {
    if width < 0 || height < 0 {
        return None;
    }
    width.checked_mul(height)?;
    let mut rect = [width, height, 0, 0];
    for y in 0..height {
        for x in 0..width {
            if alpha((y * width + x) as usize) != 0 {
                rect[0] = rect[0].min(x);
                rect[1] = rect[1].min(y);
                rect[2] = rect[2].max(x);
                rect[3] = rect[3].max(y);
            }
        }
    }
    Some(rect)
}

pub struct Layout {
    pub width: usize,
    pub height: usize,
    pub pitch: usize,
    pub source_span: usize,
    pub destination_span: usize,
}
impl Layout {
    pub fn new(width: i32, height: i32, pitch: i32) -> Option<Self> {
        if width < 0 || height < 0 || pitch < width {
            return None;
        }
        let row = width.checked_add(2)?;
        let rows = height.checked_add(2)?;
        let count = row.checked_mul(rows)?;
        let source_span = if width == 0 || height == 0 {
            0
        } else {
            (height - 1).checked_mul(pitch)?.checked_add(width)?
        };
        Some(Self {
            width: width as usize,
            height: height as usize,
            pitch: pitch as usize,
            source_span: source_span as usize,
            destination_span: count as usize,
        })
    }

    /// Only active cropped pixels are copied. The surrounding border is zero;
    /// source padding and previous output are never read. MaybeUninit preserves
    /// the native memcpy contract for RGB bytes of otherwise transparent pixels.
    pub fn copy(
        &self,
        mut pixel: impl FnMut(usize) -> MaybeUninit<[u8; 4]>,
    ) -> Option<Vec<MaybeUninit<[u8; 4]>>> {
        let mut output = Vec::new();
        output.try_reserve_exact(self.destination_span).ok()?;
        output.resize(self.destination_span, MaybeUninit::new([0; 4]));
        for y in 0..self.height {
            for x in 0..self.width {
                output[(y + 1) * (self.width + 2) + x + 1] = pixel(y * self.pitch + x);
            }
        }
        Some(output)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn empty_and_transparent_scans_keep_the_original_initial_bounds() {
        assert_eq!(bounds(0, 0, |_| panic!("no pixels")), Some([0, 0, 0, 0]));
        assert_eq!(bounds(3, 2, |_| 0), Some([3, 2, 0, 0]));
        assert_eq!(
            bounds(3, 2, |i| u8::from(i == 1 || i == 5)),
            Some([1, 0, 2, 1])
        );
        let layout = Layout::new(0, 0, 0).unwrap();
        assert_eq!(layout.destination_span, 4);
        assert_eq!(layout.copy(|_| panic!("no source copy")).unwrap().len(), 4);
    }
}
