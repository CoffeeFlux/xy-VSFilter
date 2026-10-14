// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned rendered-text escape preparation and runs. Only ASCII space,
//! LF and NUL split words; NUL is skipped and scanning continues through its tail.
use alloc::vec::Vec;
pub struct Runs {
    input: Vec<u16>,
    position: usize,
    start: usize,
    pending: Option<usize>,
    first: usize,
    end: usize,
}
impl Runs {
    pub fn new(input: &[u16]) -> Self {
        Self {
            input: input.to_vec(),
            position: 0,
            start: 0,
            pending: None,
            first: 0,
            end: 0,
        }
    }
    /// Prepare the three exact escapes. None of their replacements introduces
    /// another escape, so a forward pass preserves the native replacement order.
    /// NUL-bearing input and allocation failure retain the native preparation.
    pub fn prepare(&mut self, wrap: i32) -> bool {
        if self.input.contains(&0) {
            return false;
        }
        let mut prepared = Vec::new();
        if prepared.try_reserve_exact(self.input.len()).is_err() {
            return false;
        }
        let mut index = 0;
        while index < self.input.len() {
            if self.input[index] == 92 && index + 1 < self.input.len() {
                let replacement = match self.input[index + 1] {
                    78 => Some(10),
                    110 => Some(if wrap < 2 || wrap == 3 { 32 } else { 10 }),
                    104 => Some(0xa0),
                    _ => None,
                };
                if let Some(unit) = replacement {
                    prepared.push(unit);
                    index += 2;
                    continue;
                }
            }
            prepared.push(self.input[index]);
            index += 1;
        }
        self.input = prepared;
        self.position = 0;
        self.start = 0;
        self.pending = None;
        self.first = 0;
        self.end = 0;
        true
    }
    fn separator(&mut self, index: usize) -> u32 {
        self.first = index;
        self.end = if self.input[index] == 10 {
            index
        } else {
            index + 1
        };
        if self.input[index] == 10 {
            2
        } else {
            3
        }
    }
    pub fn next_run(&mut self) -> u32 {
        if let Some(index) = self.pending.take() {
            return self.separator(index);
        }
        while self.position <= self.input.len() {
            let index = self.position;
            self.position += 1;
            let unit = self.input.get(index).copied().unwrap_or(0);
            if unit != 0 && unit != 10 && unit != 32 {
                continue;
            }
            let first = self.start;
            self.start = index + 1;
            if first < index {
                self.first = first;
                self.end = index;
                if unit == 10 || unit == 32 {
                    self.pending = Some(index);
                }
                return 1;
            }
            if unit == 10 || unit == 32 {
                return self.separator(index);
            }
        }
        self.first = 0;
        self.end = 0;
        0
    }
    pub fn text(&self) -> &[u16] {
        &self.input[self.first..self.end]
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn pending_separator_follows_word_and_nul_does_not_end_scanning() {
        let mut runs = Runs::new(&[65, 32, 32, 0, 66, 10, 9, 0xa0, 0xd800]);
        let mut result = Vec::new();
        loop {
            let kind = runs.next_run();
            if kind == 0 {
                break;
            }
            result.push((kind, runs.text().to_vec()));
        }
        assert_eq!(
            result,
            alloc::vec![
                (1, alloc::vec![65]),
                (3, alloc::vec![32]),
                (3, alloc::vec![32]),
                (1, alloc::vec![66]),
                (2, alloc::vec![]),
                (1, alloc::vec![9, 0xa0, 0xd800])
            ]
        );
        assert_eq!(runs.next_run(), 0);
    }
    #[test]
    fn final_nul_and_empty_input_emit_no_extra_word() {
        let mut runs = Runs::new(&[0, 10, 0]);
        assert_eq!(runs.next_run(), 2);
        assert_eq!(runs.next_run(), 0);
        assert_eq!(Runs::new(&[]).next_run(), 0);
    }
    #[test]
    fn preparation_preserves_literal_backslashes_and_wrap_dependent_breaks() {
        let input = [92, 92, 78, 92, 110, 92, 104, 0xd800];
        let mut soft = Runs::new(&input);
        assert!(soft.prepare(0));
        assert_eq!(soft.input, &[92, 10, 32, 0xa0, 0xd800]);
        let mut hard = Runs::new(&input);
        assert!(hard.prepare(2));
        assert_eq!(hard.input, &[92, 10, 10, 0xa0, 0xd800]);
        let mut nul = Runs::new(&[92, 78, 0, 65]);
        assert!(!nul.prepare(0));
        assert_eq!(nul.input, &[92, 78, 0, 65]);
    }
}
