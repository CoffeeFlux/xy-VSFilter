// SPDX-License-Identifier: GPL-2.0-or-later
// Derived from CTextFile::ReadLine, Copyright (C) 2003-2006 Gabest.
//! Buffered line decoding. File operations and diagnostic logging are yielded
//! to the caller so native exceptions never unwind through a Rust frame.
use alloc::vec::Vec;

use crate::text::{next_char, Decoded, Encoding, NEED_MORE, REOPEN};

pub const CAPACITY: usize = 4096;

#[derive(Debug, PartialEq, Eq)]
pub enum Action {
    Read,
    Line,
    Reopen,
    Log(i32),
}

pub struct Reader {
    bytes: [u8; CAPACITY],
    position: usize,
    count: usize,
    line: Vec<u16>,
    // Some(tail) distinguishes incomplete-character refill from empty refill.
    refill_tail: Option<usize>,
}

impl Default for Reader {
    fn default() -> Self {
        Self {
            bytes: [0; CAPACITY],
            position: 0,
            count: 0,
            line: Vec::new(),
            refill_tail: None,
        }
    }
}

impl Reader {
    pub fn position(&self) -> usize {
        self.position
    }
    pub fn count(&self) -> usize {
        self.count
    }
    pub fn line(&self) -> &[u16] {
        &self.line
    }

    pub fn invalidate(&mut self) {
        // Open and Seek deliberately leave the old cursor alone.
        self.count = 0;
    }

    pub fn begin_line(&mut self) {
        self.line.clear();
        self.refill_tail = None;
    }

    pub fn decode(&mut self, encoding: Encoding) -> Decoded {
        let input = self.bytes.get(self.position..self.count).unwrap_or(&[]);
        let decoded = next_char(input, encoding);
        self.position += decoded.consumed as usize;
        decoded
    }

    pub fn advance(&mut self, encoding: Encoding, bom: bool) -> Action {
        loop {
            if self.position >= self.count {
                self.refill_tail = None;
                return Action::Read;
            }
            let c = self.decode(encoding).value;
            match c {
                13 => (),
                10 => return Action::Line,
                0..=0xffff => self.line.push(c as u16),
                0x10000..=0x10ffff => {
                    let c = c - 0x10000;
                    self.line.push(0xd800 | (c >> 10) as u16);
                    self.line.push(0xdc00 | (c & 0x3ff) as u16);
                }
                NEED_MORE => {
                    let tail = self.count - self.position;
                    self.bytes.copy_within(self.position..self.count, 0);
                    // Preserve old count/cursor until the native read succeeds.
                    self.refill_tail = Some(tail);
                    return Action::Read;
                }
                REOPEN if !bom => return Action::Reopen,
                REOPEN => return Action::Log(i32::from(b'?')),
                _ => return Action::Log(c),
            }
        }
    }

    /// Caller writes at most this many bytes, then calls finish_read. No reader
    /// method may run while the returned slice is borrowed for native I/O.
    pub fn read_destination(&mut self) -> &mut [u8] {
        &mut self.bytes[self.refill_tail.unwrap_or(0)..]
    }

    /// False means a broken I/O contract; the native bridge treats it as fatal.
    pub fn finish_read(&mut self, read: usize) -> bool {
        let tail = self.refill_tail.unwrap_or(0);
        if read > CAPACITY - tail {
            return false;
        }
        self.count = tail + read;
        // An empty ordinary refill keeps the previous cursor, but an empty
        // incomplete-character refill resets it. Repeated EOF reads expose this.
        if self.refill_tail.is_some() || read != 0 {
            self.position = 0;
        }
        self.refill_tail = None;
        true
    }

    pub fn append_replacement(&mut self) {
        // Called after native logging, matching the original side-effect order.
        self.line.push(u16::from(b'?'));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn refill(reader: &mut Reader, bytes: &[u8]) {
        reader.read_destination()[..bytes.len()].copy_from_slice(bytes);
        assert!(reader.finish_read(bytes.len()));
    }

    #[test]
    fn truncated_tail_and_empty_refill_have_distinct_eof_states() {
        let mut r = Reader::default();
        refill(&mut r, b"a\xf0\x9f");
        assert_eq!(r.advance(Encoding::Utf8, false), Action::Read);
        assert_eq!((r.position(), r.count()), (1, 3));
        refill(&mut r, &[]);
        assert_eq!((r.position(), r.count(), r.line()), (0, 2, &[97][..]));
        r.begin_line();
        assert_eq!(r.advance(Encoding::Utf8, false), Action::Read);
        refill(&mut r, &[0x98, 0x80, b'\n']);
        assert_eq!(r.advance(Encoding::Utf8, false), Action::Line);
        assert_eq!(r.line(), &[0xd83d, 0xde00]);
        assert_eq!(r.advance(Encoding::Utf8, false), Action::Read);
        refill(&mut r, &[]);
        assert_eq!((r.position(), r.count()), (5, 0));
    }

    #[test]
    fn logging_precedes_replacement_and_fallback_discards_line() {
        let mut r = Reader::default();
        refill(&mut r, b"a\xc0\xf4\xbf\xbf\xbf\n");
        assert_eq!(r.advance(Encoding::Utf8, true), Action::Log(63));
        assert_eq!(r.line(), &[97]);
        r.append_replacement();
        assert_eq!(r.advance(Encoding::Utf8, true), Action::Log(0x13ffff));
        r.append_replacement();
        assert_eq!(r.advance(Encoding::Utf8, true), Action::Line);
        assert_eq!(r.line(), &[97, 63, 63]);
        r.invalidate();
        r.begin_line();
        assert_eq!(r.advance(Encoding::Utf8, false), Action::Read);
        refill(&mut r, b"a\xc0");
        assert_eq!(r.advance(Encoding::Utf8, false), Action::Reopen);
        assert_eq!(r.position(), 2);
        r.begin_line();
        assert!(r.line().is_empty());
    }
}
