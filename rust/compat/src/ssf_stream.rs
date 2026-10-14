// SPDX-License-Identifier: GPL-2.0-or-later
//! SSF lookahead retains raw character values, including repeated EOS markers.
use alloc::collections::VecDeque;
#[derive(Default)]
pub struct Lookahead {
    values: VecDeque<i32>,
}
impl Lookahead {
    pub fn push(&mut self, value: i32) {
        self.values.push_back(value);
    }
    pub fn pop(&mut self) -> Option<i32> {
        self.values.pop_front()
    }
    pub fn len(&self) -> usize {
        self.values.len()
    }
    pub fn is_empty(&self) -> bool {
        self.values.is_empty()
    }
    pub fn first(&self) -> Option<i32> {
        self.values.front().copied()
    }
    pub fn last(&self) -> Option<i32> {
        self.values.back().copied()
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn raw_units_and_repeated_eof_survive_lookahead_and_wrap() {
        let mut queue = Lookahead::default();
        assert_eq!(queue.pop(), None);
        for round in 0..128 {
            for value in [0, 47, 42, 10, 0xd800, 0xffff, -1, -1, i32::MIN, i32::MAX] {
                queue.push(value);
            }
            assert_eq!(queue.len(), 10);
            assert_eq!(queue.first(), Some(0));
            assert_eq!(queue.last(), Some(i32::MAX));
            for value in [0, 47, 42, 10, 0xd800, 0xffff, -1, -1, i32::MIN, i32::MAX] {
                assert_eq!(queue.pop(), Some(value), "round {round}");
            }
            assert!(queue.is_empty());
        }
    }
}
