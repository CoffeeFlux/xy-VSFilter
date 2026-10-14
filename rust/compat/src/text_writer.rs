// SPDX-License-Identifier: GPL-2.0-or-later
//! TextFile write buffers. Every LF gains a preceding CR, including an LF
//! already preceded by CR. UTF-8 conversion stays with the native ATL boundary.
use alloc::vec::Vec;
pub struct Writer {
    text: Vec<u16>,
    bytes: Vec<u8>,
}
impl Writer {
    pub fn new(input: &[u16], big_endian: bool) -> Self {
        let mut text = Vec::new();
        for &unit in input {
            if unit == 10 {
                text.push(13);
            }
            text.push(unit);
        }
        let mut bytes = Vec::new();
        for &unit in &text {
            bytes.extend_from_slice(&if big_endian {
                unit.to_be_bytes()
            } else {
                unit.to_le_bytes()
            });
        }
        Self { text, bytes }
    }
    pub fn text(&self) -> &[u16] {
        &self.text
    }
    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn existing_crlf_is_expanded_and_surrogates_are_raw_units() {
        let input = [13, 10, 0xd800, 0, 0xdc00, 10];
        let le = Writer::new(&input, false);
        let be = Writer::new(&input, true);
        assert_eq!(le.text(), &[13, 13, 10, 0xd800, 0, 0xdc00, 13, 10]);
        assert_eq!(le.text(), be.text());
        assert_eq!(&le.bytes()[6..12], &[0, 0xd8, 0, 0, 0, 0xdc]);
        assert_eq!(&be.bytes()[6..12], &[0xd8, 0, 0, 0, 0xdc, 0]);
    }
}
