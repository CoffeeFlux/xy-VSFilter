// SPDX-License-Identifier: GPL-2.0-or-later
//! Private font key packet: charset/weight i32 LE, three bool bytes, one zero,
//! name count u32 LE, font-size f64 bits LE, then raw UTF16LE units.
#![forbid(unsafe_code)]
use crate::font_intern::Key;
pub const HEADER: usize = 24;
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Shape,
    Capacity,
}
pub fn decode(bytes: &[u8]) -> Result<Key, Error> {
    if bytes.len() < HEADER || bytes[11] != 0 || bytes[8..11].iter().any(|&b| b > 1) {
        return Err(Error::Shape);
    }
    let count = u32::from_le_bytes(bytes[12..16].try_into().unwrap()) as usize;
    if count > i32::MAX as usize {
        return Err(Error::Capacity);
    }
    let length = count
        .checked_mul(2)
        .and_then(|v| v.checked_add(HEADER))
        .ok_or(Error::Capacity)?;
    if length > isize::MAX as usize {
        return Err(Error::Capacity);
    }
    if length != bytes.len() {
        return Err(Error::Shape);
    }
    Ok(Key {
        char_set: i32::from_le_bytes(bytes[..4].try_into().unwrap()),
        font_weight: i32::from_le_bytes(bytes[4..8].try_into().unwrap()),
        italic: bytes[8] != 0,
        underline: bytes[9] != 0,
        strikeout: bytes[10] != 0,
        font_size: f64::from_bits(u64::from_le_bytes(bytes[16..24].try_into().unwrap())),
        font_name: bytes[24..]
            .chunks_exact(2)
            .map(|v| u16::from_le_bytes(v.try_into().unwrap()))
            .collect(),
    })
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn raw_key_bits_and_strict_metadata() {
        let mut bytes = alloc::vec![0;30];
        bytes[..4].copy_from_slice(&i32::MIN.to_le_bytes());
        bytes[4..8].copy_from_slice(&i32::MAX.to_le_bytes());
        bytes[8] = 1;
        bytes[12..16].copy_from_slice(&3u32.to_le_bytes());
        bytes[16..24].copy_from_slice(&0x7ff0000000000001u64.to_le_bytes());
        bytes[24..].copy_from_slice(&[0, 0, 0, 0xd8, 0xff, 0xff]);
        let k = decode(&bytes).unwrap();
        assert_eq!(k.char_set, i32::MIN);
        assert_eq!(k.font_weight, i32::MAX);
        assert_eq!(k.font_size.to_bits(), 0x7ff0000000000001);
        assert_eq!(k.font_name, [0, 0xd800, 0xffff]);
        assert!(k.italic);
        for (offset, value) in [(8, 2), (11, 1), (12, 4)] {
            let mut bad = bytes.clone();
            bad[offset] = value;
            assert!(decode(&bad).is_err());
        }
        assert!(decode(&bytes[..23]).is_err());
        bytes[12..16].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(decode(&bytes), Err(Error::Capacity)));
    }
}
