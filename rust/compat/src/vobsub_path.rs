// SPDX-License-Identifier: GPL-3.0-or-later
//! One polygon-to-ASS command, before native string and point access.
//! The native caller updates its previous type from the last consumed entry,
//! including the third entry of a curve, after formatting has completed.
pub fn command(kind: u8, previous: u8, remaining: usize) -> u32 {
    let (letter, count) = match kind {
        6 => (b'm', 1),
        0xfe => (b'n', 1),
        2 => (b'l', 1),
        4 if remaining >= 3 => (b'b', 3),
        0xfc if remaining >= 3 => (b's', 3),
        0xfa => (b'p', 1),
        _ => return 0,
    };
    let prefix = if kind == previous { 0 } else { letter };
    u32::from(prefix) | (count << 8)
}

/// Six signed 32-bit decimal coordinates, including spaces, need at most 72
/// UTF-16 units. No terminator is required by CString's explicit-length setter.
pub fn coordinates(points: &[i32; 6], count: usize) -> Option<([u16; 72], usize)> {
    if count != 1 && count != 3 {
        return None;
    }
    let mut output = [0; 72];
    let mut len = 0;
    for &value in &points[..count * 2] {
        if value < 0 {
            output[len] = u16::from(b'-');
            len += 1;
        }
        let mut magnitude = value.unsigned_abs();
        let mut digits = [0u16; 10];
        let mut used = 0;
        loop {
            digits[used] = u16::from(b'0') + (magnitude % 10) as u16;
            used += 1;
            magnitude /= 10;
            if magnitude == 0 {
                break;
            }
        }
        for &digit in digits[..used].iter().rev() {
            output[len] = digit;
            len += 1;
        }
        output[len] = u16::from(b' ');
        len += 1;
    }
    Some((output, len))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn incomplete_curves_emit_nothing_and_type_flags_are_not_masked() {
        assert_eq!(command(4, 0, 2), 0);
        assert_eq!(command(4, 4, 3), 3 << 8);
        assert_eq!(command(4, 2, 3), (3 << 8) | u32::from(b'b'));
        assert_eq!(command(7, 0, 3), 0);
        assert_eq!(command(0xfa, 0, 1), (1 << 8) | u32::from(b'p'));
    }

    #[test]
    fn decimal_coordinates_include_signed_limits_and_trailing_spaces() {
        let (output, len) = coordinates(&[i32::MIN; 6], 3).unwrap();
        assert_eq!(len, 72);
        for chunk in output.chunks_exact(12) {
            assert!(chunk.iter().copied().eq("-2147483648 ".encode_utf16()));
        }
        let (output, len) = coordinates(&[0, i32::MAX, 7, 8, 9, 10], 1).unwrap();
        assert!(output[..len]
            .iter()
            .copied()
            .eq("0 2147483647 ".encode_utf16()));
        assert!(coordinates(&[0; 6], 2).is_none());
    }
}
