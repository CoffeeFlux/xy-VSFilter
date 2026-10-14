// SPDX-License-Identifier: GPL-3.0-or-later
//! Owned lowercase output with one native classification per original code unit.
use alloc::vec::Vec;

#[derive(Default)]
pub struct Lower {
    position: u32,
    waiting: bool,
    pub character: u16,
    pub text: Vec<u16>,
}
impl Lower {
    /// Commit the previous native result, then request the next live input unit.
    /// No source loan survives the call; native case conversion remains external.
    pub fn next(&mut self, input: &[u16], response: u16) -> bool {
        if self.waiting {
            self.text.push(response);
            self.position = self.position.wrapping_add(1);
            self.waiting = false;
        }
        let Some(&unit) = input.get(self.position as usize) else {
            return false;
        };
        self.character = unit;
        self.waiting = true;
        true
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn native_result_is_committed_before_observing_shortened_input() {
        let mut lower = Lower::default();
        assert!(lower.next(&[65, 66], 0));
        assert_eq!(lower.character, 65);
        assert!(!lower.next(&[], 97));
        assert_eq!(lower.text, [97]);
    }
}
