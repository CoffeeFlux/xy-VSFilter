// SPDX-License-Identifier: GPL-3.0-or-later
//! RealText timing history and interval selection, before native map insertion.
use alloc::vec::Vec;

#[derive(Default)]
pub struct Timeline {
    starts: Vec<i32>,
    ends: Vec<i32>,
    missing: bool,
    pub first: i32,
    pub second: i32,
}
impl Timeline {
    /// 0: no interval; 1: direct interval; 2: native first + default duration.
    /// Arithmetic stays native so this step never substitutes overflow behavior.
    pub fn select(&mut self, next: Option<i32>, map_empty: bool) -> u32 {
        self.first = 0;
        self.second = 0;
        if self.missing {
            // advance is the only writer of missing and always appends a start.
            self.first = *self.starts.last().expect("missing end has a start");
            if let Some(end) = next {
                self.second = end;
                if self.first < end {
                    return 1;
                }
            }
            return 2;
        }
        if let (Some(&start), Some(&end)) = (self.starts.last(), self.ends.last()) {
            self.first = start;
            self.second = end;
            return 1;
        }
        if self.starts.is_empty() && self.ends.is_empty() && map_empty {
            if let Some(end) = next {
                if end > 0 {
                    self.second = end;
                    return 1;
                }
            }
        }
        0
    }
    /// Called only after the previous line's native insertion has completed.
    pub fn advance(&mut self, start: i32, end: i32) {
        self.starts.push(start);
        self.missing = end <= 0;
        if end > 0 {
            self.ends.push(end);
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn missing_ends_and_first_line_depend_on_distinct_history() {
        let mut times = Timeline::default();
        assert_eq!(times.select(Some(1000), false), 0);
        assert_eq!(times.select(Some(1000), true), 1);
        assert_eq!((times.first, times.second), (0, 1000));
        times.advance(1000, 2000);
        assert_eq!(times.select(Some(500), false), 1);
        assert_eq!((times.first, times.second), (1000, 2000));
        times.advance(i32::MAX, 0);
        assert_eq!(times.select(Some(i32::MIN), false), 2);
        assert_eq!(times.first, i32::MAX);
        assert_eq!(times.select(None, false), 2);
        times.advance(-1000, 0);
        assert_eq!(times.select(Some(0), false), 1);
        assert_eq!((times.first, times.second), (-1000, 0));
    }
}
