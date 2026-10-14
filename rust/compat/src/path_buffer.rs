// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned temporary GDI output buffers. Native GetPath receives an exclusive
//! output loan; only complete successful output is consumed by the native loop.
use crate::path_align::Point;
use alloc::vec::Vec;
pub struct Buffer {
    types: Vec<u8>,
    points: Vec<Point>,
}
impl Buffer {
    pub fn new(count: i32) -> Option<Self> {
        if count <= 0 {
            return None;
        }
        let count = count as usize;
        if count > (isize::MAX as usize) / core::mem::size_of::<Point>() {
            return None;
        }
        let mut types = Vec::new();
        types.try_reserve_exact(count).ok()?;
        types.resize(count, 0);
        let mut points = Vec::new();
        points.try_reserve_exact(count).ok()?;
        points.resize(count, Point { x: 0, y: 0 });
        Some(Self { points, types })
    }
    pub fn buffers(&mut self) -> (&mut [Point], &mut [u8]) {
        (&mut self.points, &mut self.types)
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn disjoint_output_loans_cover_exactly_the_requested_records() {
        let mut buffer = Buffer::new(3).unwrap();
        let (points, types) = buffer.buffers();
        assert_eq!(points.len(), 3);
        assert_eq!(types.len(), 3);
        points[0] = Point {
            x: i32::MIN,
            y: i32::MAX,
        };
        types[0] = 255;
        let (points, types) = buffer.buffers();
        assert_eq!(
            points[0],
            Point {
                x: i32::MIN,
                y: i32::MAX
            }
        );
        assert_eq!(types[0], 255);
        assert!(Buffer::new(0).is_none());
        assert!(Buffer::new(-1).is_none());
    }
}
