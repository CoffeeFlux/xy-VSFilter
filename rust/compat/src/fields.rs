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
}
