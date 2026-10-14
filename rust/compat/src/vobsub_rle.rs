// SPDX-License-Identifier: GPL-3.0-or-later
//! DVD nibble extraction and short-circuit variable-length run codes.

/// Low byte: nibble. Bit 8: the next read is byte-aligned and advances offset.
pub fn nibble(byte: u8, aligned: bool) -> u32 {
    let value = if aligned { byte >> 4 } else { byte & 15 };
    u32::from(value) | (u32::from(!aligned) << 8)
}

/// Low 32 bits: accumulated code. High 32 bits: 0 requests another nibble, 1 draws
/// an explicit run, 2 fills the rest of the row after the fourth short nibble.
/// Each nibble is supplied separately, preserving native read/draw interleaving.
pub fn code_step(code: u32, nibble: u8, stage: u32) -> u64 {
    let code = code.wrapping_shl(4) | u32::from(nibble);
    let threshold = [4, 16, 64, 256][stage.min(3) as usize];
    let action = if code >= threshold {
        1
    } else if stage >= 3 {
        2
    } else {
        0
    };
    u64::from(code) | (action << 32)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn stopping_thresholds_and_four_nibble_row_fill_are_distinct() {
        for (stage, threshold) in [(0, 4), (1, 16), (2, 64), (3, 256)] {
            let code = threshold - 1;
            assert_eq!(
                code_step(code >> 4, (code & 15) as u8, stage) >> 32,
                if stage == 3 { 2 } else { 0 }
            );
            assert_eq!(
                code_step(threshold >> 4, (threshold & 15) as u8, stage) >> 32,
                1
            );
        }
        assert_eq!(code_step(0, 3, 3), (2u64 << 32) | 3);
        assert_eq!(nibble(0xa7, true), 10);
        assert_eq!(nibble(0xa7, false), 0x107);
    }
}
