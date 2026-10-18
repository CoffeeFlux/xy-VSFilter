// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned ASS line preparation and read/dispatch lifecycle from cleanup STS.cpp.
//! Whitespace classifications are supplied by the native CString traits; no
//! Unicode scalar conversion or independent whitespace table is introduced.
use alloc::vec::Vec;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
#[repr(u32)]
pub enum Action {
    #[default]
    Read = 0,
    LowerKey = 1,
    Record = 2,
    Done = 3,
}

#[derive(Default)]
pub struct Input {
    action: Action,
    active: bool,
    eof: bool,
    key: Vec<u16>,
    payload: Vec<u16>,
}

/// FastTrimRight scans the explicit length, including past NUL. CString's
/// TrimLeft stops at NUL. The supplied predicate results must be strict bools.
pub fn trim<'a>(input: &'a [u16], spaces: &[u8]) -> Option<&'a [u16]> {
    if input.len() != spaces.len() || spaces.iter().any(|&b| b > 1) {
        return None;
    }
    let mut end = input.len();
    while end > 0 && spaces[end - 1] != 0 {
        end -= 1;
    }
    let mut start = 0;
    while start < end && input[start] != 0 && spaces[start] != 0 {
        start += 1;
    }
    Some(&input[start..end])
}

impl Input {
    pub fn action(&self) -> Action {
        self.action
    }
    pub fn can_classify(&self) -> bool {
        !self.active || self.action == Action::LowerKey
    }
    pub fn classified(&mut self) {
        if self.active {
            self.action = Action::Record;
        }
    }
    pub fn read(&mut self, raw: &[u16], spaces: &[u8], eof: bool) -> bool {
        if self.action != Action::Read {
            return false;
        }
        let Some(line) = trim(raw, spaces) else {
            return false;
        };
        // The raw empty EOF check precedes trimming in the reference.
        let action = if eof && raw.is_empty() {
            Action::Done
        } else if line.is_empty() || line[0] == 59 {
            Action::Read
        } else {
            Action::LowerKey
        };
        let (key, payload) = if action == Action::LowerKey {
            // After FastTrim, GetStr(':') cannot remove more leading space.
            // Find stops at NUL, but a missing separator preserves full length
            // and leaves the remainder unchanged, including its raw NUL tail.
            let c_end = line.iter().position(|&u| u == 0).unwrap_or(line.len());
            if let Some(colon) = line[..c_end].iter().position(|&u| u == 58) {
                (line[..colon].to_vec(), line[colon + 1..].to_vec())
            } else {
                (line.to_vec(), line.to_vec())
            }
        } else {
            (Vec::new(), Vec::new())
        };
        self.key = key;
        self.payload = payload;
        self.eof = eof;
        self.active = true;
        self.action = action;
        true
    }
    pub fn can_finish(&self) -> bool {
        self.active && self.action == Action::Record
    }
    pub fn finish(&mut self, success: bool) {
        self.action = if !success || self.eof {
            Action::Done
        } else {
            Action::Read
        };
    }
    pub fn part(&self, group: u32) -> Option<Vec<u8>> {
        let text = match group {
            0 => {
                if self.action == Action::LowerKey {
                    &self.key[..]
                } else {
                    &[]
                }
            }
            1 if self.action == Action::Record => &self.payload[..],
            _ => return None,
        };
        let mut bytes = Vec::new();
        bytes.extend_from_slice(&(self.action as u32).to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());
        for unit in text {
            bytes.extend_from_slice(&unit.to_le_bytes());
        }
        Some(bytes)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn units(text: &str) -> Vec<u16> {
        text.encode_utf16().collect()
    }
    fn spaces(input: &[u16]) -> Vec<u8> {
        input
            .iter()
            .map(|u| u8::from(matches!(u, 9 | 10 | 13 | 32)))
            .collect()
    }
    #[test]
    fn final_comments_and_trimmed_blanks_read_again_but_records_finish() {
        for line in [" ; last comment ", " \t "] {
            let line = units(line);
            let mut input = Input::default();
            assert!(input.read(&line, &spaces(&line), true));
            assert_eq!(input.action(), Action::Read);
            assert!(input.read(&[], &[], true));
            assert_eq!(input.action(), Action::Done);
            assert!(!input.read(&[], &[], true));
        }
        let line = units(" [Events] ");
        let mut input = Input::default();
        assert!(input.read(&line, &spaces(&line), true));
        assert_eq!(input.action(), Action::LowerKey);
        assert!(input.can_classify());
        input.classified();
        assert!(input.can_finish());
        input.finish(true);
        assert_eq!(input.action(), Action::Done);
    }
    #[test]
    fn trim_and_split_preserve_explicit_nul_tails_and_lone_surrogates() {
        let line = [32, 0xd800, 0, 58, 32, 0xdc00, 32];
        let mut input = Input::default();
        assert!(input.read(&line, &spaces(&line), false));
        let before = input.part(0).unwrap();
        assert_eq!(&before[8..], &[0, 0xd8, 0, 0, 58, 0, 32, 0, 0, 0xdc]);
        assert!(!input.read(&[], &[], true));
        assert_eq!(input.part(0).unwrap(), before);
        input.classified();
        assert_eq!(&input.part(1).unwrap()[8..], &before[8..]);
        input.finish(true);
        assert!(input.read(&[32, 58, 32, 65, 32], &[1, 0, 1, 0, 1], false));
        assert!(input.part(0).unwrap()[8..].is_empty());
        input.classified();
        assert_eq!(&input.part(1).unwrap()[8..], &[32, 0, 65, 0]);
    }
    #[test]
    fn malformed_predicate_shapes_leave_input_unchanged() {
        let mut input = Input::default();
        let before = input.part(0);
        assert!(!input.read(&[65], &[], false));
        assert!(!input.read(&[65], &[2], false));
        assert_eq!(input.part(0), before);
        assert_eq!(trim(&[0, 32], &[1, 0]), Some(&[0, 32][..]));
    }
}
