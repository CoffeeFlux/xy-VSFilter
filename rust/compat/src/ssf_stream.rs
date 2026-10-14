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
/// Transient state for one PeekChar call. A native exception discards this
/// state while retaining the queue changes already made by PushChar/PopChar.
#[repr(C)]
#[derive(Default)]
pub struct Peek {
    phase: u32,
    previous: i32,
    result: i32,
}
#[repr(u32)]
pub enum PeekAction {
    Read = 0,
    Pop = 1,
    Done = 2,
    Check = 3,
}
impl Peek {
    pub fn step(&mut self, count: usize, head: i32, tail: i32, last: i32) -> PeekAction {
        loop {
            match self.phase {
                0 => {
                    // Fill the two-character lookahead, then native ASSERT.
                    if count < 2 {
                        return PeekAction::Read;
                    }
                    self.phase = 1;
                    return PeekAction::Check;
                }
                1 => {
                    self.phase = if head == 47 && tail == 47 {
                        2
                    } else if head == 47 && tail == 42 {
                        6
                    } else {
                        self.result = head;
                        12
                    };
                }
                2 => {
                    // Line comment: discard the queued opener completely.
                    if count != 0 {
                        return PeekAction::Pop;
                    }
                    self.phase = 3;
                }
                3 => {
                    self.phase = 4;
                    return PeekAction::Read;
                }
                4 => {
                    self.phase = 5;
                    return PeekAction::Pop;
                }
                5 => {
                    self.phase = if last == 10 || last == -1 { 0 } else { 3 };
                }
                6 => {
                    // Block comment: discard opener, then seed one character.
                    if count != 0 {
                        return PeekAction::Pop;
                    }
                    self.phase = 7;
                }
                7 => {
                    self.phase = 8;
                    return PeekAction::Read;
                }
                8 => {
                    self.phase = 9;
                    return PeekAction::Read;
                }
                9 => {
                    self.previous = last;
                    self.phase = 10;
                    return PeekAction::Pop;
                }
                10 => {
                    self.phase = if (last == 42 && self.previous == 47) || last == -1 {
                        11
                    } else {
                        8
                    };
                }
                11 => {
                    self.phase = 0;
                    return PeekAction::Pop;
                }
                12 => return PeekAction::Done,
                _ => self.phase = 0,
            }
        }
    }
    pub fn value(&self) -> i32 {
        self.result
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
