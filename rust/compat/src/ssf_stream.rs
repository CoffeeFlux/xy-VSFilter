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

/// Preserve SSF's code-unit encoding, including its historical left shifts.
/// Three 16-bit callback values occupy bits 0..48; bits 48..64 hold the count.
/// The caller handles BOM state separately because callbacks can reenter it.
pub fn output(character: u16, encoding: u32) -> u64 {
    let c = u64::from(character);
    let (units, count) = match encoding {
        2 if c < 0x80 => ([c, 0, 0], 1),
        2 if c < 0x800 => ([0xc0 | ((c << 2) & 0x1f), 0x80 | (c & 0x3f), 0], 2),
        2 if c < 0xffff => (
            [
                0xe0 | ((c << 4) & 0x0f),
                0x80 | ((c << 2) & 0x3f),
                0x80 | (c & 0x3f),
            ],
            3,
        ),
        2 => ([63, 0, 0], 1),
        3 => ([c & 0xff, c >> 8, 0], 2),
        4 => ([c >> 8, c & 0xff, 0], 2),
        5 => ([c, 0, 0], 1),
        _ => ([0, 0, 0], 0),
    };
    (count << 48) | (units[2] << 32) | (units[1] << 16) | units[0]
}

/// An immutable snapshot for MemoryInputStream's copy mode.
pub struct Memory {
    bytes: alloc::vec::Vec<u8>,
}
impl Memory {
    pub fn copy(input: &[u8]) -> Option<Self> {
        let mut bytes = alloc::vec::Vec::new();
        bytes.try_reserve_exact(input.len()).ok()?;
        bytes.extend_from_slice(input);
        Some(Self { bytes })
    }
    pub fn data(&self) -> &[u8] {
        &self.bytes
    }
    pub fn read(&self, position: i32) -> i32 {
        usize::try_from(position)
            .ok()
            .and_then(|i| self.bytes.get(i))
            .map_or(-1, |&byte| i32::from(byte))
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
