// SPDX-License-Identifier: GPL-3.0-or-later
//! Owned reverse-order timecode fields; native numeric folding is retained.
use alloc::vec::Vec;

pub struct Timecode {
    index: Option<usize>,
    finished: bool,
    pub last: bool,
    pub missing: usize,
    pub part: Vec<u16>,
}
impl Timecode {
    pub fn new(length: usize) -> Self {
        Self {
            index: length.checked_sub(1),
            finished: false,
            last: false,
            missing: 0,
            part: Vec::new(),
        }
    }
    /// Zero finishes; one exposes a field; two requests the original native
    /// bounds-checked read (and its exception) after an input shrink. A fresh
    /// input loan keeps changes between numeric conversions observable.
    pub fn next(&mut self, input: &[u16]) -> u32 {
        if self.finished {
            return 0;
        }
        self.part.clear();
        while let Some(index) = self.index {
            let Some(&unit) = input.get(index) else {
                self.missing = index;
                return 2;
            };
            self.index = index.checked_sub(1);
            if unit == 46 || unit == 58 {
                self.last = false;
                self.part.reverse();
                return 1;
            }
            self.part.push(unit);
        }
        self.part.reverse();
        self.last = true;
        self.finished = true;
        1
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn empty_fields_final_prefix_and_live_shrink_are_retained() {
        let input: alloc::vec::Vec<u16> = "1::2.3".encode_utf16().collect();
        let mut state = Timecode::new(input.len());
        for expected in ["3", "2", "", "1"] {
            assert_eq!(state.next(&input), 1);
            assert!(state.part.iter().copied().eq(expected.encode_utf16()));
        }
        assert!(state.last);
        assert_eq!(state.next(&input), 0);
        let mut state = Timecode::new(3);
        assert_eq!(state.next(&[49, 58, 50]), 1);
        assert_eq!(state.next(&[]), 2);
        assert_eq!(state.missing, 0);
    }
}
