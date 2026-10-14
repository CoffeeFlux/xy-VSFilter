// SPDX-License-Identifier: GPL-2.0-or-later
// Derived from CPolygon::ParseStr, Copyright (C) 2003-2006 Gabest.
//! Raw UTF-16 drawing preparation and the original Tokenize/TrimLeft sequence.
//! Embedded-NUL input retains native command replacement before tokenization.
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
        Self::from_units(input.to_vec())
    }

    fn from_units(units: Vec<u16>) -> Self {
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

    /// Each native replacement inserts '*' before one lowercase command. Their
    /// replacement text introduces no other command, so one forward pass gives
    /// the same result. Declines leave the raw owner unchanged.
    pub fn prepare(&mut self) -> bool {
        if self.units.contains(&0) {
            return false;
        }
        let extra = self.units.iter().filter(|&&unit| command(unit)).count();
        let Some(length) = self.units.len().checked_add(extra) else {
            return false;
        };
        if length > i32::MAX as usize {
            return false;
        }
        let mut prepared = Vec::new();
        if prepared.try_reserve_exact(length).is_err() {
            return false;
        }
        for &unit in &self.units {
            if command(unit) {
                prepared.push(42);
            }
            prepared.push(unit);
        }
        *self = Self::from_units(prepared);
        true
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
    fn separators_and_trimming_are_not_unicode_whitespace() {
        let tokens = Tokens::scan(&"ju*nk**m\t1 2 *n 3 4".encode_utf16().collect::<Vec<_>>());
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

    #[test]
    fn preparation_splits_commands_inside_arbitrary_text() {
        let mut tokens = Tokens::scan(&"junk 0x1p+4**m 1 2".encode_utf16().collect::<Vec<_>>());
        assert!(tokens.prepare());
        let commands: Vec<_> = tokens.tokens().iter().map(|t| t.command).collect();
        assert_eq!(commands, vec![106, 110, 112, 109]);
        assert_eq!(tokens.parameters(1).unwrap(), &[107, 32, 48, 120, 49]);
        assert_eq!(tokens.parameters(2).unwrap(), &[43, 52]);
        assert_eq!(tokens.parameters(3).unwrap(), &[49, 32, 50]);
    }

    #[test]
    fn preparation_declines_nul_without_changing_tokens() {
        let mut tokens = Tokens::scan(&[109, 32, 49, 0, 108, 32, 50]);
        assert!(!tokens.prepare());
        assert_eq!(tokens.units, &[109, 32, 49, 0, 108, 32, 50]);
        assert_eq!(tokens.parameters(1).unwrap(), &[50]);
    }
}
