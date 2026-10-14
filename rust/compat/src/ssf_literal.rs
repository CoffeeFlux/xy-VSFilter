// SPDX-License-Identifier: GPL-2.0-or-later
// Derived from Gabest's SSF parser, Copyright (C) 2003-2006 Gabest.
//! Control flow of the pinned SSF quoted-string and brace-block parsers.
//! Native stream, append, commit and error callbacks run between steps.
#[repr(C)]
pub struct Literal {
    phase: u32,
    quote: i32,
    depth: i64,
    value: i32,
}
#[repr(u32)]
pub enum Action {
    Skip = 0,
    SkipBlock = 1,
    Get = 2,
    Peek = 3,
    Append = 4,
    String = 5,
    Block = 6,
    ExpectedQuote = 7,
    Newline = 8,
    StringEos = 9,
    UnterminatedString = 10,
    ExpectedBrace = 11,
    BlockEos = 12,
    UnterminatedBlock = 13,
}
impl Literal {
    pub fn new(block: bool) -> Self {
        Self {
            phase: if block { 20 } else { 0 },
            quote: 0,
            depth: 0,
            value: 0,
        }
    }
    pub fn value(&self) -> i32 {
        self.value
    }
    pub fn step(&mut self, last: i32) -> Action {
        loop {
            match self.phase {
                0 => {
                    self.phase = 1;
                    return Action::Skip;
                }
                1 => {
                    self.quote = last;
                    if last != 34 && last != 39 {
                        return Action::ExpectedQuote;
                    }
                    self.phase = 2;
                    return Action::Get;
                }
                2 => {
                    self.phase = 3;
                    return Action::Peek;
                }
                3 => {
                    if last == -1 {
                        return Action::UnterminatedString;
                    }
                    self.phase = 4;
                    return Action::Get;
                }
                4 => {
                    if last == self.quote {
                        return Action::String;
                    }
                    if last == 10 {
                        return Action::Newline;
                    }
                    self.phase = 5;
                    if last == 92 {
                        return Action::Get;
                    }
                }
                5 => {
                    if last == -1 {
                        return Action::StringEos;
                    }
                    self.value = last;
                    self.phase = 2;
                    return Action::Append;
                }
                20 => {
                    self.phase = 21;
                    return Action::SkipBlock;
                }
                21 => {
                    if last != 123 {
                        return Action::ExpectedBrace;
                    }
                    self.phase = 22;
                    return Action::Get;
                }
                22 => {
                    self.phase = 23;
                    return Action::Peek;
                }
                23 => {
                    if last == -1 {
                        return Action::UnterminatedBlock;
                    }
                    self.phase = 24;
                    return Action::Get;
                }
                24 => {
                    if last == 125 && self.depth == 0 {
                        return Action::Block;
                    }
                    if last == 92 {
                        self.value = last;
                        self.phase = 25;
                        return Action::Append;
                    }
                    // Wider safe depth avoids native integer overflow. Parity
                    // evidence is restricted to representable native depths.
                    if last == 123 {
                        self.depth = self.depth.saturating_add(1);
                    } else if last == 125 {
                        self.depth = self.depth.saturating_sub(1);
                    }
                    self.phase = 26;
                }
                25 => {
                    self.phase = 26;
                    return Action::Get;
                }
                26 => {
                    if last == -1 {
                        return Action::BlockEos;
                    }
                    self.value = last;
                    self.phase = 22;
                    return Action::Append;
                }
                _ => return Action::UnterminatedString,
            }
        }
    }
}
