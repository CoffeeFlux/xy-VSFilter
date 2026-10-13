// SPDX-License-Identifier: GPL-2.0-or-later
// Derived from STS.cpp::TryNextStr, Copyright (C) 2003-2006 Gabest.
//! Delimiter scanning over raw code units, before native numeric conversion.

#[derive(Debug, PartialEq, Eq)]
pub struct Field {
    pub start: usize,
    pub end: usize,
}

/// Locate the next field in a bounded, possibly NUL-containing buffer.
/// Only ASCII space is trimmed. The caller replaces the ending separator with
/// NUL and advances past it, or stays at `start` when there was no separator.
pub fn next_field(input: &[u16], separator: u16) -> Field {
    let mut start = 0;
    while input.get(start) == Some(&32) {
        start += 1;
    }
    let mut end = start;
    while let Some(&unit) = input.get(end) {
        if unit == 0 || unit == separator {
            break;
        }
        end += 1;
    }
    Field { start, end }
}

/// CString-based GetStr uses CRT whitespace, preserves explicit lengths after
/// an embedded NUL, and throws on an empty trimmed buffer. A missing separator
/// returns the entire remaining string without consuming it.
pub fn string_field(input: &[u16], whitespace: &[u8], separator: u16) -> Field {
    let mut start = 0;
    while start < input.len() && input[start] != 0 && whitespace.get(start) == Some(&1) {
        start += 1;
    }
    let mut end = start;
    while end < input.len() && input[end] != separator && input[end] != 0 {
        end += 1;
    }
    if end < input.len() && input[end] != separator {
        end = input.len();
    }
    Field { start, end }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn only_ascii_space_is_trimmed_even_when_space_is_the_separator() {
        assert_eq!(
            next_field(&[32, 32, 9, 32, 44], 32),
            Field { start: 2, end: 3 }
        );
        assert_eq!(
            next_field(&[0x2003, 44, 0xd800], 44),
            Field { start: 0, end: 1 }
        );
    }

    #[test]
    fn empty_final_and_nul_terminated_fields_keep_their_start() {
        assert_eq!(next_field(&[32, 32], 44), Field { start: 2, end: 2 });
        assert_eq!(
            next_field(&[32, 49, 0, 44, 50], 44),
            Field { start: 1, end: 2 }
        );
        assert_eq!(next_field(&[44, 50], 44), Field { start: 0, end: 0 });
    }

    #[test]
    fn cstring_search_stops_at_nul_but_missing_separator_keeps_explicit_tail() {
        let input = [32, 0xd800, 0, 44, 50];
        assert_eq!(
            string_field(&input, &[1, 0, 0, 0, 0], 44),
            Field { start: 1, end: 5 }
        );
        assert_eq!(
            string_field(&input, &[1, 0, 0, 0, 0], 0),
            Field { start: 1, end: 2 }
        );
        assert_eq!(
            string_field(&[9, 32], &[1, 1], 44),
            Field { start: 2, end: 2 }
        );
    }
}
