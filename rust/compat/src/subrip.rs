// SPDX-License-Identifier: GPL-2.0-or-later
//! The six ordered, case-sensitive replacements in SubRipper2SSA.
use alloc::vec::Vec;

pub fn convert(input: &[u16]) -> Option<Vec<u16>> {
    // CString's replacement behavior around embedded NULs has unresolved
    // allocation-dependent outcomes. Leave that existing boundary native.
    if input.contains(&0) {
        return None;
    }
    let mut current = Vec::new();
    current.try_reserve_exact(input.len()).ok()?;
    current.extend_from_slice(input);
    for (needle, replacement) in [
        (b"<i>".as_slice(), b"{\\i1}".as_slice()),
        (b"</i>".as_slice(), b"{\\i}".as_slice()),
        (b"<b>".as_slice(), b"{\\b1}".as_slice()),
        (b"</b>".as_slice(), b"{\\b}".as_slice()),
        (b"<u>".as_slice(), b"{\\u1}".as_slice()),
        (b"</u>".as_slice(), b"{\\u}".as_slice()),
    ] {
        let mut next = Vec::new();
        next.try_reserve_exact(current.len().checked_mul(2)?).ok()?;
        let mut cursor = 0;
        while cursor < current.len() {
            let found = current
                .get(cursor..cursor + needle.len())
                .is_some_and(|units| units.iter().zip(needle).all(|(&a, &b)| a == u16::from(b)));
            if found {
                next.extend(replacement.iter().copied().map(u16::from));
                cursor += needle.len();
            } else {
                next.push(current[cursor]);
                cursor += 1;
            }
        }
        if next.len() > i32::MAX as usize {
            return None;
        }
        current = next;
    }
    Some(current)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn exact_case_and_raw_utf16_survive_conversion() {
        let input: Vec<u16> = "<I><i>x</i><b><u>y</u></b>".encode_utf16().collect();
        assert!(convert(&input)
            .unwrap()
            .into_iter()
            .eq("<I>{\\i1}x{\\i}{\\b1}{\\u1}y{\\u}{\\b}".encode_utf16()));
        assert_eq!(
            convert(&[0xd800, 0xdc00, 0xffff]),
            Some(alloc::vec![0xd800, 0xdc00, 0xffff])
        );
        assert!(convert(&[60, 105, 62, 0]).is_none());
    }
}
