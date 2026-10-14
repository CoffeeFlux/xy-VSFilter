// SPDX-License-Identifier: GPL-2.0-or-later
//! MPL2's leading slash and pipe-slash expansion before MicroDVD conversion.
use alloc::vec::Vec;
pub fn prepare(input: &[u16]) -> Option<Vec<u16>> {
    // The native CString replacement boundary around embedded NULs is retained.
    if input.contains(&0) {
        return None;
    }
    let mut output = Vec::new();
    output
        .try_reserve_exact(input.len().checked_mul(3)?.checked_add(5)?)
        .ok()?;
    let mut index = 0;
    if input.first() == Some(&47) {
        output.extend(b"{y:i}".iter().copied().map(u16::from));
        index = 1;
    }
    while index < input.len() {
        if input.get(index..index + 2) == Some(&[124, 47]) {
            output.extend(b"|{y:i}".iter().copied().map(u16::from));
            index += 2;
        } else {
            output.push(input[index]);
            index += 1;
        }
    }
    if output.len() > i32::MAX as usize {
        return None;
    }
    Some(output)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn only_line_initial_slashes_expand_without_unicode_decoding() {
        let input: Vec<u16> = "//a|//b|/| /c".encode_utf16().collect();
        assert!(prepare(&input)
            .unwrap()
            .into_iter()
            .eq("{y:i}/a|{y:i}/b|{y:i}| /c".encode_utf16()));
        assert_eq!(
            prepare(&[47, 0xd800]),
            Some(alloc::vec![123, 121, 58, 105, 125, 0xd800])
        );
        assert_eq!(prepare(&[124, 47, 0]), None);
    }
}
