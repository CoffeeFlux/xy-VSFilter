// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned LoadUUEFont loop and LoadFont decoding from cleanup STS.cpp.
//! File reads, CString whitespace classification and process-lifetime Windows
//! font installation are external services. No native buffers are retained.
use alloc::vec::Vec;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
#[repr(u32)]
pub enum Action {
    #[default]
    Read = 0,
    Install = 1,
    Done = 2,
}

#[derive(Default)]
pub struct Fonts {
    action: Action,
    stop_after_install: bool,
    encoded: Vec<u16>,
    decoded: Vec<u8>,
}
impl Fonts {
    pub fn action(&self) -> Action {
        self.action
    }
    pub fn read(&mut self, input: &[u16], spaces: &[u8], eof: bool) -> bool {
        if self.action != Action::Read {
            return false;
        }
        let Some(line) = crate::ass_input::trim(input, spaces) else {
            return false;
        };
        // while(ReadString(s)) ignores a final nonempty fragment on false.
        let kind = if eof {
            0
        } else {
            crate::font_records::Records::classify(line)
        };
        if kind == 2 {
            // CString += CString reaches Append's StringLengthN check in the
            // pinned Windows build: each fragment stops at its first NUL.
            let end = line.iter().position(|&u| u == 0).unwrap_or(line.len());
            if self.encoded.len().saturating_add(end) > i32::MAX as usize {
                return false;
            }
            self.encoded.extend_from_slice(&line[..end]);
        } else {
            self.stop_after_install = kind == 0;
            if let Some(length) = crate::font::decoded_len(self.encoded.len()) {
                self.decoded.resize(length, 0);
                crate::font::decode_into(&self.encoded, &mut self.decoded);
                self.action = Action::Install;
            } else {
                self.action = if kind == 0 {
                    Action::Done
                } else {
                    Action::Read
                };
            }
            self.encoded.clear();
        }
        true
    }
    /// AddFontMemResourceEx's return and font count are ignored by the reference.
    /// Its process-private resource must not be released before process exit.
    pub fn installed(&mut self) -> bool {
        if self.action != Action::Install {
            return false;
        }
        self.decoded.clear();
        self.action = if self.stop_after_install {
            Action::Done
        } else {
            Action::Read
        };
        true
    }
    /// action:u32,reserved:u32, then owned decoded bytes during Install.
    pub fn snapshot(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        bytes.extend_from_slice(&(self.action as u32).to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());
        bytes.extend_from_slice(&self.decoded);
        bytes
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn read(fonts: &mut Fonts, text: &str, eof: bool) {
        let units: Vec<_> = text.encode_utf16().collect();
        let spaces: Vec<_> = units.iter().map(|&u| u8::from(u == 32)).collect();
        assert!(fonts.read(&units, &spaces, eof));
    }
    #[test]
    fn markers_flush_invalid_lengths_skip_and_sections_are_consumed() {
        let mut fonts = Fonts::default();
        read(&mut fonts, "fontname: empty", false);
        read(&mut fonts, "!", false);
        read(&mut fonts, "fontname: invalid", false);
        assert_eq!(fonts.action(), Action::Read);
        read(&mut fonts, " !! ", false);
        read(&mut fonts, "fontname: second", false);
        assert_eq!(&fonts.snapshot()[8..], &[0]);
        assert!(!fonts.read(&[], &[], true));
        assert!(fonts.installed());
        read(&mut fonts, "!!!", false);
        read(&mut fonts, "[Events]tail", false);
        assert_eq!(&fonts.snapshot()[8..], &[0, 0]);
        assert!(fonts.installed());
        assert_eq!(fonts.action(), Action::Done);
        assert!(!fonts.installed());
        assert!(!fonts.read(&[], &[], false));
    }
    #[test]
    fn false_read_discards_fragment_but_flushes_previous_record() {
        let mut fonts = Fonts::default();
        read(&mut fonts, "!!", false);
        read(&mut fonts, "!!!", true);
        assert_eq!(&fonts.snapshot()[8..], &[0]);
        assert!(fonts.installed());
        assert_eq!(fonts.action(), Action::Done);
        let mut fonts = Fonts::default();
        read(&mut fonts, "!!!!", true);
        assert_eq!(fonts.action(), Action::Done);
    }
    #[test]
    fn nul_truncated_fragments_and_invalid_predicates() {
        let mut fonts = Fonts::default();
        assert!(fonts.read(&[32, 0xd800, 0xd800, 0, 33, 32], &[1, 0, 0, 0, 0, 1], false));
        assert!(!fonts.read(&[33], &[2], false));
        read(&mut fonts, "", false);
        assert_eq!(&fonts.snapshot()[8..], &[0x7d]);
        let snapshot = fonts.snapshot();
        assert!(fonts.installed());
        assert_eq!(&snapshot[8..], &[0x7d]);
    }
}
