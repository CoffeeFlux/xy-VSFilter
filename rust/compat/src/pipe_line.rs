// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned legacy subtitle line, retaining the native FindChar callback for each
//! pipe replacement. Native charset/DBCS search semantics remain authoritative.
use alloc::vec::Vec;
pub struct Line {
    text: Vec<u16>,
    position: usize,
}
impl Line {
    pub fn new(text: &[u16]) -> Self {
        Self {
            text: text.to_vec(),
            position: 0,
        }
    }
    pub fn position(&self) -> Option<usize> {
        (self.position < self.text.len()).then_some(self.position)
    }
    pub fn found(&mut self, found: i32) {
        if let Ok(index) = usize::try_from(found) {
            if let Some(unit) = self.text.get_mut(index) {
                *unit = 10;
                self.position = index + 1;
                return;
            }
        }
        self.position = self.text.len();
    }
    pub fn text(&self) -> &[u16] {
        &self.text
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn callback_position_skips_dbcs_bytes_and_keeps_counted_nul_tail() {
        let mut line = Line::new(&[0x81, 124, 0, 124, 0xd800]);
        assert_eq!(line.position(), Some(0));
        line.found(3);
        assert_eq!(line.position(), Some(4));
        assert_eq!(line.text(), &[0x81, 124, 0, 10, 0xd800]);
        line.found(-1);
        assert_eq!(line.position(), None);
    }
    #[test]
    fn last_unit_replacement_ends_without_another_callback() {
        let mut line = Line::new(&[124]);
        line.found(0);
        assert_eq!(line.position(), None);
        assert_eq!(line.text(), &[10]);
        assert_eq!(Line::new(&[]).position(), None);
    }
}
