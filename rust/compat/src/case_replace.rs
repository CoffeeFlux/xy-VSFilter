// SPDX-License-Identifier: GPL-2.0-or-later
//! ReplaceNoCase after native lowercase construction. The needle is deliberately
//! not lowercased, and replacement text is never searched again.
use alloc::vec::Vec;
pub fn replace(source: &[u16], lowered: &[u16], from: &[u16], to: &[u16]) -> Option<Vec<u16>> {
    if source.len() != lowered.len()
        || from.is_empty()
        || [source, lowered, from, to]
            .iter()
            .any(|part| part.contains(&0))
    {
        return None;
    }
    let mut output = Vec::new();
    output.try_reserve_exact(source.len()).ok()?;
    let mut cursor = 0;
    while cursor < source.len() {
        if lowered.get(cursor..cursor.checked_add(from.len())?) == Some(from) {
            output.try_reserve(to.len()).ok()?;
            output.extend_from_slice(to);
            cursor += from.len();
        } else {
            output.try_reserve(1).ok()?;
            output.push(source[cursor]);
            cursor += 1;
        }
        if output.len() > i32::MAX as usize {
            return None;
        }
    }
    Some(output)
}
#[cfg(test)]
mod tests {
    use super::*;
    fn units(text: &str) -> Vec<u16> {
        text.encode_utf16().collect()
    }
    #[test]
    fn source_case_survives_and_replacement_is_not_revisited() {
        assert_eq!(
            replace(
                &units("AaBaa"),
                &units("aabaa"),
                &units("aa"),
                &units("AAaa")
            ),
            Some(units("AAaaBAAaa"))
        );
        assert_eq!(
            replace(&units("AA"), &units("aa"), &units("AA"), &units("x")),
            Some(units("AA"))
        );
        assert_eq!(
            replace(&[0xd800, 65], &[0xd800, 97], &[97], &[0xdfff]),
            Some(alloc::vec![0xd800, 0xdfff])
        );
        assert!(replace(&[1], &[1], &[], &[2]).is_none());
        assert!(replace(&[1, 0], &[1, 0], &[1], &[2]).is_none());
        assert!(replace(&[1], &[], &[1], &[2]).is_none());
    }
}
