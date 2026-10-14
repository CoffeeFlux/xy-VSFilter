// SPDX-License-Identifier: GPL-2.0-or-later
//! LoadUUEFont record assembly after native line reading and trimming. The
//! recognized section header is consumed; fontname markers flush even an empty
//! record. CString fragment concatenation and actual font loading stay native.
use alloc::vec::Vec;
#[derive(Default)]
pub struct Records {
    text: Vec<u16>,
}
fn prefix(input: &[u16], text: &[u8]) -> bool {
    input.len() >= text.len() && input.iter().zip(text).all(|(&a, &b)| a == u16::from(b))
}
impl Records {
    /// 0 stops; 1 flushes then clears; 2 appends the native counted fragment.
    pub fn classify(input: &[u16]) -> u32 {
        if input.is_empty() {
            return 0;
        }
        let headers: [&[u8]; 6] = [
            b"[Script Info]",
            b"[V4+ Styles]",
            b"[V4 Styles]",
            b"[Events]",
            b"[Fonts]",
            b"[Graphics]",
        ];
        if input[0] == 91 && headers.iter().any(|header| prefix(input, header)) {
            return 0;
        }
        if prefix(input, b"fontname:") {
            1
        } else {
            2
        }
    }
    pub fn append(&mut self, input: &[u16]) {
        self.text.extend_from_slice(input);
    }
    pub fn clear(&mut self) {
        self.text.clear();
    }
    pub fn text(&self) -> &[u16] {
        &self.text
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    fn units(text: &str) -> Vec<u16> {
        text.encode_utf16().collect()
    }
    #[test]
    fn exact_section_prefixes_exclude_v4_double_plus_and_lowercase() {
        assert_eq!(Records::classify(&units("[Events]tail")), 0);
        assert_eq!(Records::classify(&units("[V4++ Styles]")), 2);
        assert_eq!(Records::classify(&units("[events]")), 2);
        assert_eq!(Records::classify(&units("fontname:\0tail")), 1);
        assert_eq!(Records::classify(&[0]), 2);
        assert_eq!(Records::classify(&[]), 0);
    }
    #[test]
    fn counted_native_fragments_and_empty_flushes_are_preserved() {
        let mut record = Records::default();
        assert!(record.text().is_empty());
        record.append(&[65, 0, 0xd800]);
        record.append(&[66]);
        assert_eq!(record.text(), &[65, 0, 0xd800, 66]);
        record.clear();
        assert!(record.text().is_empty());
    }
}
