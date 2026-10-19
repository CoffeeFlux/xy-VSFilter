// SPDX-License-Identifier: GPL-2.0-or-later
//! Cleanup ScanLineData's ordered integer line sampler. All signed machine
//! wraparound is explicit; no floating arithmetic or path pointer is involved.
use crate::path_align::Point;

pub struct Line {
    row: i32,
    end: i32,
    accumulator: i64,
    increment: i64,
    flag: i32,
}
impl Line {
    /// The native caller orders endpoints by strictly ascending y and skips
    /// horizontal lines. Retain its half-sample prestep for nonempty row ranges.
    pub fn new(start: Point, end: Point, up: bool) -> Option<Self> {
        if start.y >= end.y {
            return None;
        }
        let y = (start.y.wrapping_add(3) & !7).wrapping_add(4);
        let row = y >> 3;
        let last = end.y.wrapping_sub(5) >> 3;
        let mut result = Self {
            row,
            end: last,
            accumulator: i64::from(start.x).wrapping_shl(13),
            increment: 0,
            flag: i32::from(up),
        };
        if row <= last {
            let dy = i64::from(end.y.wrapping_sub(start.y));
            result.increment = i64::from(end.x.wrapping_sub(start.x))
                .wrapping_shl(16)
                .checked_div(dy)?;
            result.accumulator = result.accumulator.wrapping_add(
                result
                    .increment
                    .wrapping_mul(i64::from(y.wrapping_sub(start.y)))
                    >> 3,
            );
        }
        Some(result)
    }
    pub fn rows(&self) -> Option<(i32, i32)> {
        (self.row <= self.end).then_some((self.row, self.end))
    }
}
impl Iterator for Line {
    type Item = (i32, i32);
    fn next(&mut self) -> Option<Self::Item> {
        if self.row > self.end {
            return None;
        }
        let ix = (self.accumulator.wrapping_add(32768) >> 16) as i32;
        let result = (self.row, ix.wrapping_shl(1) | self.flag);
        // Both row bounds came from i32 arithmetic shifts by three, so this
        // increment cannot overflow even on the last emitted row.
        self.row += 1;
        self.accumulator = self.accumulator.wrapping_add(self.increment);
        Some(result)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec::Vec;
    #[test]
    fn half_sample_boundaries_and_signed_x_difference_wrap_are_explicit() {
        let points = |y| (Point { x: i32::MAX, y: 0 }, Point { x: i32::MIN, y });
        let (a, b) = points(4);
        assert!(Line::new(a, b, true).unwrap().next().is_none());
        let (a, b) = points(5);
        assert_eq!(
            Line::new(a, b, true).unwrap().collect::<Vec<_>>(),
            [(0, 536870913)]
        );
        let a = Point { x: -32, y: 0 };
        let b = Point { x: 32, y: 16 };
        assert_eq!(
            Line::new(a, b, false).unwrap().collect::<Vec<_>>(),
            [(0, -4), (1, 4)]
        );
        assert!(Line::new(b, a, false).is_none());
    }
}
