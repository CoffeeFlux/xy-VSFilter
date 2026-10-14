// SPDX-License-Identifier: GPL-3.0-or-later
//! RealText attribute parsing with native whitespace, lowercase and map commits.
use alloc::vec::Vec;

#[derive(Clone, Copy)]
enum Phase {
    Initial,
    Loop,
    Name,
    AfterName,
    Equals,
    AfterEquals,
    Quote,
    AfterQuote,
    Value,
    AfterValue,
    EndQuote,
    AfterEndQuote,
    Done(bool),
}

pub struct Attributes {
    pub position: u32,
    pub character: u16,
    pub name: Vec<u16>,
    pub value: Vec<u16>,
    phase: Phase,
    waiting: bool,
    quoted: bool,
}

impl Attributes {
    pub fn new(position: u32) -> Self {
        Self {
            position,
            character: 0,
            name: Vec::new(),
            value: Vec::new(),
            phase: Phase::Initial,
            waiting: false,
            quoted: false,
        }
    }

    fn finish(&mut self, success: bool) -> u32 {
        self.phase = Phase::Done(success);
        if success {
            3
        } else {
            4
        }
    }

    /// Actions: 1 asks native iswspace(character); 2 commits the current name and
    /// value at position; 3/4 finish with true/false. Input is freshly borrowed
    /// each time, so changes between native operations are visible. response is
    /// consumed only after a whitespace request.
    pub fn next(&mut self, input: &[u16], ignore_errors: bool, response: bool) -> u32 {
        if self.waiting {
            self.waiting = false;
            if response {
                self.position = self.position.wrapping_add(1);
            } else {
                if self.position as usize >= input.len() {
                    return self.finish(false);
                }
                self.phase = match self.phase {
                    Phase::Initial | Phase::AfterEndQuote => Phase::Loop,
                    Phase::AfterName => Phase::Equals,
                    Phase::AfterEquals => Phase::Quote,
                    Phase::AfterQuote => Phase::Value,
                    Phase::AfterValue => Phase::EndQuote,
                    _ => return self.finish(false),
                };
            }
        }
        loop {
            let pos = self.position as usize;
            match self.phase {
                Phase::Initial
                | Phase::AfterName
                | Phase::AfterEquals
                | Phase::AfterQuote
                | Phase::AfterValue
                | Phase::AfterEndQuote => {
                    let Some(&unit) = input.get(pos) else {
                        return self.finish(false);
                    };
                    self.character = unit;
                    self.waiting = true;
                    return 1;
                }
                Phase::Loop => match input.get(pos) {
                    Some(47 | 62) => return self.finish(true),
                    None => return self.finish(false),
                    _ => {
                        self.name.clear();
                        self.value.clear();
                        self.phase = Phase::Name;
                    }
                },
                Phase::Name => {
                    while let Some(&unit) = input.get(self.position as usize) {
                        if [13, 10, 9, 32, 61].contains(&unit) {
                            break;
                        }
                        self.name.push(unit);
                        self.position = self.position.wrapping_add(1);
                    }
                    if self.position as usize >= input.len() {
                        return self.finish(false);
                    }
                    self.phase = Phase::AfterName;
                }
                Phase::Equals => {
                    if input.get(pos) != Some(&61) {
                        if !ignore_errors {
                            return self.finish(false);
                        }
                        let Some(offset) = input[pos..].iter().position(|&unit| unit == 61) else {
                            return self.finish(false);
                        };
                        self.position = (pos + offset) as u32;
                    }
                    self.position = self.position.wrapping_add(1);
                    self.phase = Phase::AfterEquals;
                }
                Phase::Quote => {
                    self.quoted = matches!(input.get(pos), Some(39 | 34));
                    if self.quoted {
                        self.position = self.position.wrapping_add(1);
                    }
                    self.phase = Phase::AfterQuote;
                }
                Phase::Value => {
                    let stops: &[u16] = if self.quoted {
                        &[34, 39, 47, 62]
                    } else {
                        &[32, 9, 47, 62]
                    };
                    while let Some(&unit) = input.get(self.position as usize) {
                        if stops.contains(&unit) {
                            break;
                        }
                        self.value.push(unit);
                        self.position = self.position.wrapping_add(1);
                    }
                    if self.position as usize >= input.len() {
                        return self.finish(false);
                    }
                    self.phase = Phase::AfterValue;
                    return 2;
                }
                Phase::EndQuote => {
                    if matches!(input.get(pos), Some(39 | 34)) {
                        self.position = self.position.wrapping_add(1);
                    }
                    self.phase = Phase::AfterEndQuote;
                }
                Phase::Done(success) => return if success { 3 } else { 4 },
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn mismatched_quotes_and_partial_commits_keep_their_positions() {
        let input: Vec<u16> = "a=' x\" b=unfinished".encode_utf16().collect();
        let mut state = Attributes::new(0);
        let mut response = false;
        let mut committed = 0;
        loop {
            match state.next(&input, false, response) {
                1 => response = [9, 10, 13, 32].contains(&state.character),
                2 => {
                    assert_eq!(state.name, alloc::vec![97]);
                    assert_eq!(state.value, alloc::vec![120]);
                    assert_eq!(state.position, 5);
                    committed += 1;
                }
                4 => break,
                _ => panic!("unexpected completion"),
            }
        }
        assert_eq!(committed, 1);
        assert_eq!(state.position as usize, input.len());
    }
}
