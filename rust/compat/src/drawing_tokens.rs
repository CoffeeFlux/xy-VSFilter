// SPDX-License-Identifier: GPL-2.0-or-later
// Derived from CPolygon::ParseStr, Copyright (C) 2003-2006 Gabest.
//! Raw UTF-16 drawing tokens. This models the original Replace/Tokenize/TrimLeft
//! sequence, including embedded NULs; it is not a drawing grammar validator.
use alloc::vec::Vec;
use core::ops::Range;

fn command(unit: u16) -> bool {
    matches!(unit, 109 | 110 | 108 | 98 | 115 | 112 | 99)
}

pub struct Token {
    pub command: u16,
    pub parameters: Range<usize>,
}

pub struct Tokens {
    units: Vec<u16>,
    tokens: Vec<Token>,
}

impl Tokens {
    pub fn scan(input: &[u16]) -> Self {
        let mut units = Vec::new();
        // The ignored SpanIncluding return does not filter the input. Each of
        // the seven replacements inserts '*' before its command everywhere.
        for &unit in input {
            if command(unit) {
                units.push(u16::from(b'*'));
            }
            units.push(unit);
        }
        let mut tokens = Vec::new();
        let mut cursor = 0;
        while cursor < units.len() {
            while units.get(cursor) == Some(&u16::from(b'*')) {
                cursor += 1;
            }
            if cursor >= units.len() || units[cursor] == 0 {
                break;
            }
            let start = cursor;
            while cursor < units.len() && units[cursor] != 0 && units[cursor] != u16::from(b'*') {
                cursor += 1;
            }
            let end = cursor;
            let mut parameters = start;
            while parameters < end && (command(units[parameters]) || units[parameters] == 32) {
                parameters += 1;
            }
            tokens.push(Token {
                command: units[start],
                parameters: parameters..end,
            });
            // CString::Tokenize advances one unit past its separator, including
            // a NUL inside its explicit length. A NUL at the next start stops
            // the original outer !IsEmpty loop instead of skipping that unit.
            cursor += 1;
        }
        Self { units, tokens }
    }

    pub fn tokens(&self) -> &[Token] {
        &self.tokens
    }
    pub fn parameters(&self, index: usize) -> Option<&[u16]> {
        self.tokens
            .get(index)
            .map(|token| &self.units[token.parameters.clone()])
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;

    #[test]
    fn commands_split_inside_words_and_trimming_is_not_unicode_whitespace() {
        let tokens = Tokens::scan(&"junk*m\t1 2 n 3 4".encode_utf16().collect::<Vec<_>>());
        let commands: Vec<_> = tokens.tokens().iter().map(|t| t.command).collect();
        assert_eq!(commands, vec![106, 110, 109, 110]);
        assert_eq!(tokens.parameters(2).unwrap(), &[9, 49, 32, 50, 32]);
        assert_eq!(tokens.parameters(3).unwrap(), &[51, 32, 52]);
    }

    #[test]
    fn embedded_nul_can_end_a_token_or_stop_iteration() {
        let t = Tokens::scan(&[109, 32, 49, 0, 108, 32, 50]);
        assert_eq!(t.tokens().len(), 2);
        assert_eq!(t.parameters(0).unwrap(), &[49]);
        assert_eq!(t.parameters(1).unwrap(), &[50]);
        assert!(Tokens::scan(&[0, 109, 32, 49]).tokens().is_empty());
        assert_eq!(Tokens::scan(&[109, 0, 0, 108]).tokens().len(), 1);
    }
}
