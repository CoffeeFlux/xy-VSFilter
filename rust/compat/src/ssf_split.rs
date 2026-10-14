// SPDX-License-Identifier: GPL-2.0-or-later
//! CString-compatible separator search: start at the requested code-unit
//! offset, stop the search at NUL, and retain the original declared length.
pub fn find(input: &[u16], separator: &[u16], start: usize) -> Option<usize> {
    let remaining = input.get(start..)?;
    if separator.is_empty() {
        return Some(start);
    }
    let visible = remaining
        .iter()
        .position(|&c| c == 0)
        .unwrap_or(remaining.len());
    remaining[..visible]
        .windows(separator.len())
        .position(|window| window == separator)
        .map(|offset| start + offset)
}
