// SPDX-License-Identifier: GPL-2.0-or-later
// Derived from LoadFont in STS.cpp, Copyright (C) 2003-2006 Gabest.
//! ASS font-attachment decoding, including out-of-alphabet code units.
//!
//! The old decoder subtracts 33 and truncates to BYTE. It never rejects an
//! out-of-alphabet unit. Do not substitute a conventional UU/base64 decoder.

pub fn decode_group(input: [u16; 4]) -> [u8; 3] {
    let [a, b, c, d] = input.map(|unit| unit.wrapping_sub(33) as u8);
    [
        ((a & 63) << 2) | ((b >> 4) & 3),
        ((b & 15) << 4) | ((c >> 2) & 15),
        ((c & 3) << 6) | (d & 63),
    ]
}

pub fn decoded_len(length: usize) -> Option<usize> {
    if length == 0 || length % 4 == 1 {
        None
    } else {
        Some((length / 4) * 3 + (length % 4).saturating_sub(1))
    }
}

/// Decode into caller-owned storage. Invalid lengths or insufficient space
/// return None without changing output; no encoding normalization occurs.
pub fn decode_into(input: &[u16], output: &mut [u8]) -> Option<usize> {
    let length = decoded_len(input.len())?;
    if output.len() < length {
        return None;
    }
    let mut written = 0;
    for chunk in input.chunks(4) {
        let mut units = [0; 4];
        units[..chunk.len()].copy_from_slice(chunk);
        let decoded = decode_group(units);
        let count = chunk.len() - 1;
        output[written..written + count].copy_from_slice(&decoded[..count]);
        written += count;
    }
    Some(length)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn preserves_out_of_alphabet_units_and_tail_lengths() {
        let mut output = [0xa5; 12];
        assert_eq!(decode_into(&[33, 33], &mut output), Some(1));
        assert_eq!(&output[..2], &[0, 0xa5]);
        assert_eq!(decode_into(&[0, 0, 0], &mut output), Some(2));
        assert_eq!(&output[..3], &[0x7d, 0xf7, 0xa5]);
        assert_eq!(decode_into(&[0xffff; 4], &mut output), Some(3));
        assert_eq!(&output[..4], &[0x79, 0xe7, 0x9e, 0xa5]);
        assert_eq!(decode_into(&[33; 6], &mut output), Some(4));
        assert_eq!(&output[..5], &[0, 0, 0, 0, 0xa5]);
    }

    #[test]
    fn rejects_without_partial_writes() {
        for input in [&[][..], &[33][..], &[33; 5][..]] {
            let mut output = [0xa5; 12];
            assert_eq!(decode_into(input, &mut output), None);
            assert_eq!(output, [0xa5; 12]);
        }
        let mut output = [0xa5; 2];
        assert_eq!(decode_into(&[33; 4], &mut output), None);
        assert_eq!(output, [0xa5; 2]);
    }
}
