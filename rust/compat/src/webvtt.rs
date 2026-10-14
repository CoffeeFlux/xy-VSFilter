// SPDX-License-Identifier: GPL-2.0-or-later
//! Selection of the single cue-setting line removed by WebVTTCueStrip.
pub fn cue_prefix(input: &[u16]) -> usize {
    // CString Find stops at the first NUL even when GetLength includes a tail.
    let Some(newline) = input.iter().take_while(|&&c| c != 0).position(|&c| c == 10) else {
        return 0;
    };
    if newline == 0 {
        return 0;
    }
    for prefix in [
        b"align:".as_slice(),
        b"position:",
        b"vertical:",
        b"line:",
        b"size:",
    ] {
        if input
            .get(..prefix.len())
            .is_some_and(|units| units.iter().zip(prefix).all(|(&a, &b)| a == u16::from(b)))
        {
            // The newline remains for the subsequent native TrimLeft call.
            return newline;
        }
    }
    0
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn only_exact_leading_settings_before_the_first_nul_are_removed() {
        let input: alloc::vec::Vec<u16> = "align:x\n  text".encode_utf16().collect();
        assert_eq!(cue_prefix(&input), 7);
        assert_eq!(cue_prefix(&[10, 97]), 0);
        let input: alloc::vec::Vec<u16> = "align:x\0\ntext".encode_utf16().collect();
        assert_eq!(cue_prefix(&input), 0);
        let input: alloc::vec::Vec<u16> = "Align:x\ntext".encode_utf16().collect();
        assert_eq!(cue_prefix(&input), 0);
    }
}
