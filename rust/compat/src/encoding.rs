// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned code-unit buffers for STS ToMBCS/ToUnicode. Code-page translation,
//! lead-byte classification and conversion remain native, in reference order.
use alloc::vec::Vec;

pub struct Encoding {
    input: Vec<u16>,
    output: Vec<u16>,
    position: usize,
}
impl Encoding {
    pub fn new(input: &[u16]) -> Self {
        Self {
            input: input.to_vec(),
            output: Vec::new(),
            position: 0,
        }
    }
    /// -1 signals exhaustion; zero is an ordinary stored code unit.
    pub fn take(&mut self) -> i32 {
        match self.input.get(self.position) {
            Some(&unit) => {
                self.position += 1;
                i32::from(unit)
            }
            None => -1,
        }
    }
    pub fn append_unit(&mut self, unit: u16) {
        self.output.push(unit);
    }
    /// Only the initialized bytes reported by WideCharToMultiByte are loaned.
    /// An unsuccessful call appends '?', including a zero return value.
    pub fn append_bytes(&mut self, bytes: &[u8]) {
        if bytes.is_empty() {
            self.output.push(63);
        } else {
            self.output
                .extend(bytes.iter().map(|&byte| u16::from(byte)));
        }
    }
    pub fn output(&self) -> &[u16] {
        &self.output
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn units_include_nuls_surrogates_and_unconverted_trailing_leads() {
        let mut state = Encoding::new(&[0, 0xd800, 0xff81]);
        for unit in [0, 0xd800, 0xff81] {
            assert_eq!(state.take(), unit);
            state.append_unit(unit as u16);
        }
        assert_eq!(state.take(), -1);
        assert_eq!(state.take(), -1);
        assert_eq!(state.output(), &[0, 0xd800, 0xff81]);
    }
    #[test]
    fn byte_results_are_unsigned_and_empty_results_use_question_mark() {
        let mut state = Encoding::new(&[]);
        state.append_bytes(&[0, 127, 128, 255]);
        state.append_bytes(&[]);
        assert_eq!(state.output(), &[0, 127, 128, 255, 63]);
    }
}
