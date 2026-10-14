// SPDX-License-Identifier: GPL-2.0-or-later
//! SubRip caption storage and persistent blank-line boundary state. Native
//! scans, trimming and CString fragment construction retain their ordering.
use alloc::vec::Vec;
#[derive(Default)]
pub struct Cue {
    text: Vec<u16>,
    found_empty: bool,
}
impl Cue {
    pub fn empty_line(&mut self) {
        self.found_empty = true;
    }
    pub fn boundary(&self, numeric_line: bool) -> bool {
        numeric_line && self.found_empty
    }
    pub fn append(&mut self, fragment: &[u16]) {
        self.text.extend_from_slice(fragment);
    }
    pub fn text(&self) -> &[u16] {
        &self.text
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn blank_line_state_survives_later_nonempty_lines() {
        let mut cue = Cue::default();
        assert!(!cue.boundary(true));
        cue.append(&[49, 10]);
        cue.empty_line();
        cue.append(&[10]);
        assert!(!cue.boundary(false));
        cue.append(&[65, 0, 0xd800, 10]);
        assert!(cue.boundary(true));
        assert_eq!(cue.text(), &[49, 10, 10, 65, 0, 0xd800, 10]);
    }
}
