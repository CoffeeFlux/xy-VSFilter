// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned source and brace traversal for GetMicroDVDStyle. Native one-unit case
//! conversion, prefix comparisons, CRT scans and style-field updates remain.
use alloc::vec::Vec;
pub struct Tags {
    input: Vec<u16>,
    position: usize,
    start: usize,
    end: usize,
}
impl Tags {
    pub fn new(input: &[u16]) -> Self {
        Self {
            input: input.to_vec(),
            position: 0,
            start: 0,
            end: 0,
        }
    }
    fn find(&self, target: u16, start: usize) -> Option<usize> {
        self.input
            .get(start..)?
            .iter()
            .take_while(|&&unit| unit != 0)
            .position(|&unit| unit == target)
            .map(|index| start + index)
    }
    pub fn advance(&mut self) -> bool {
        let Some(start) = self.find(123, self.position) else {
            return false;
        };
        self.start = start;
        self.end = self.find(125, start).unwrap_or(self.input.len());
        self.position = self.end.saturating_add(1);
        true
    }
    pub fn code(&self) -> &[u16] {
        &self.input[self.start..self.end]
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    fn units(text: &str) -> Vec<u16> {
        text.encode_utf16().collect()
    }
    #[test]
    fn first_closing_brace_wins_and_unclosed_tail_keeps_counted_nuls() {
        let mut tags = Tags::new(&units("x{{f:a}y{f:b\0}tail"));
        assert!(tags.advance());
        assert_eq!(tags.code(), units("{{f:a"));
        assert!(tags.advance());
        assert_eq!(tags.code(), units("{f:b\0}tail"));
        assert!(!tags.advance());
    }
    #[test]
    fn nul_prevents_later_opening_brace_search() {
        let mut tags = Tags::new(&units("{y:i}\0{f:ignored}"));
        assert!(tags.advance());
        assert_eq!(tags.code(), units("{y:i"));
        assert!(!tags.advance());
        assert!(!Tags::new(&[]).advance());
    }
}
