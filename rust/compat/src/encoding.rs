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

/// Own the remaining SSA text and accumulated output while native searches,
/// charset-number parsing and conversion callbacks execute between steps.
pub struct Ssa {
    input: Vec<u16>,
    output: Vec<u16>,
    stage: u8,
    pub count: usize,
}
impl Ssa {
    pub fn new(input: &[u16]) -> Self {
        Self {
            input: input.to_vec(),
            output: Vec::new(),
            stage: 0,
            count: 0,
        }
    }
    pub fn input(&self) -> &[u16] {
        &self.input
    }
    pub fn output(&self) -> &[u16] {
        &self.output
    }
    pub fn append(&mut self, fragment: &[u16]) {
        self.output.extend_from_slice(fragment);
    }
    /// Actions: 0 done; 1 find opening brace; 2 find closing brace; 3 parse
    /// charset before closing brace; 4 convert prefix with current charset;
    /// 5 convert tag with original charset; 6 convert remaining text.
    pub fn step(&mut self, reply: i32) -> u32 {
        loop {
            match self.stage {
                0 => {
                    if self.input.is_empty() {
                        self.stage = 6;
                        return 0;
                    }
                    self.stage = 1;
                    return 1;
                }
                1 => {
                    if reply < 0 {
                        self.stage = 6;
                        return 6;
                    }
                    self.count = (reply as usize).min(self.input.len());
                    self.stage = 2;
                    return 4;
                }
                2 => {
                    self.input.drain(..self.count);
                    self.stage = 3;
                    return 2;
                }
                3 => {
                    if reply < 0 {
                        self.stage = 6;
                        return 6;
                    }
                    self.count = (reply as usize + 1).min(self.input.len());
                    self.stage = 4;
                    return 3;
                }
                4 => {
                    self.stage = 5;
                    return 5;
                }
                5 => {
                    self.input.drain(..self.count);
                    self.stage = 0;
                }
                _ => return 0,
            }
        }
    }
}
#[cfg(test)]
mod ssa_tests {
    use super::*;
    #[test]
    fn empty_prefix_is_converted_and_unterminated_tail_survives() {
        let mut state = Ssa::new(&[123, 125, 65, 123, 66]);
        assert_eq!(state.step(0), 1);
        assert_eq!(state.step(0), 4);
        assert_eq!(state.count, 0);
        state.append(&[]);
        assert_eq!(state.step(0), 2);
        assert_eq!(state.step(1), 3);
        assert_eq!(state.count, 2);
        assert_eq!(state.step(0), 5);
        state.append(&[123, 125]);
        assert_eq!(state.step(0), 1);
        assert_eq!(state.input(), &[65, 123, 66]);
        assert_eq!(state.step(1), 4);
        state.append(&[65]);
        assert_eq!(state.step(0), 2);
        assert_eq!(state.step(-1), 6);
        state.append(&[123, 66]);
        assert_eq!(state.step(0), 0);
        assert_eq!(state.output(), &[123, 125, 65, 123, 66]);
        assert_eq!(Ssa::new(&[]).step(0), 0);
    }
}
