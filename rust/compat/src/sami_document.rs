// SPDX-License-Identifier: GPL-2.0-or-later
//! Checked cleanup OpenSami discovery, rewind, input and caption lifecycle.
//! Native services uppercase raw units, classify individual timestamp digits,
//! read/seek the file, convert markup and Add. No native pointer is retained.
use crate::ass_document::Error;
use crate::sami_file::File;
use alloc::vec::Vec;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Op {
    ProbeRead = 0,
    ProbeUpper = 1,
    ProbeDiscard = 2,
    Rewind = 3,
    Read = 4,
    Upper = 5,
    Digit = 6,
    Emit = 7,
    Done = 8,
    Failed = 9,
}
pub struct Run {
    op: Op,
    position: u64,
    line: Vec<u16>,
    upper: Vec<u16>,
    file: File,
    digit: usize,
    time: i32,
    final_emit: bool,
    accepted: bool,
}
fn prefix(raw: &[u16]) -> &[u16] {
    &raw[..raw.iter().position(|&u| u == 0).unwrap_or(raw.len())]
}
impl Run {
    pub fn new(position: u64) -> Self {
        Self {
            op: Op::ProbeRead,
            position,
            line: Vec::new(),
            upper: Vec::new(),
            file: File::default(),
            digit: 0,
            time: 0,
            final_emit: false,
            accepted: false,
        }
    }
    pub fn read(&mut self, raw: &[u16], spaces: &[u8], eof: bool) -> Result<(), Error> {
        if !matches!(self.op, Op::ProbeRead | Op::ProbeDiscard | Op::Read)
            || raw.len() != spaces.len()
            || spaces.iter().any(|&p| p > 1)
        {
            return Err(Error::Protocol);
        }
        if raw.len() > i32::MAX as usize {
            return Err(Error::CountOverflow);
        }
        match self.op {
            Op::ProbeDiscard => {
                // ReadString is the LEFT operand of && !fSAMI, so it executes
                // once more after detection, even when that extra read is EOF.
                self.line.clear();
                self.op = Op::Rewind;
            }
            Op::ProbeRead => {
                if eof {
                    self.op = Op::Failed;
                } else {
                    self.line = raw.to_vec();
                    self.op = Op::ProbeUpper;
                }
            }
            Op::Read => {
                if eof {
                    self.final_emit = true;
                    self.op = Op::Emit;
                } else {
                    let line = crate::ass_input::trim(raw, spaces).ok_or(Error::Protocol)?;
                    if !line.is_empty() {
                        self.line = line.to_vec();
                        self.op = Op::Upper;
                    }
                }
            }
            _ => unreachable!(),
        }
        Ok(())
    }
    pub fn uppercase(&mut self, upper: &[u16]) -> Result<(), Error> {
        if !matches!(self.op, Op::ProbeUpper | Op::Upper) || upper.len() != self.line.len() {
            return Err(Error::Protocol);
        }
        if self.op == Op::ProbeUpper {
            self.op = if crate::sami_file::contains(prefix(upper), b"<SAMI>") {
                Op::ProbeDiscard
            } else {
                Op::ProbeRead
            };
            return Ok(());
        }
        let action = self.file.action(upper);
        if action == 1
            && self
                .file
                .text()
                .len()
                .saturating_add(prefix(&self.line).len())
                > i32::MAX as usize
        {
            return Err(Error::CountOverflow);
        }
        self.file.begin(upper);
        match action {
            0 => {
                self.file.end();
                self.line.clear();
                self.op = Op::Read;
            }
            1 => self.append_line(),
            2 => {
                self.upper = upper.to_vec();
                self.time = 0;
                self.digit = 12;
                self.next_digit();
            }
            _ => unreachable!(),
        }
        Ok(())
    }
    pub fn rewind(&mut self) -> Result<(), Error> {
        if self.op != Op::Rewind {
            return Err(Error::Protocol);
        }
        self.line.clear();
        self.op = Op::Read;
        Ok(())
    }
    fn next_digit(&mut self) {
        self.op = if self
            .upper
            .get(self.digit)
            .is_some_and(|&u| u != 62 && u != 77)
        {
            Op::Digit
        } else {
            Op::Emit
        };
    }
    pub fn digit(&mut self, is_digit: bool) -> Result<(), Error> {
        if self.op != Op::Digit {
            return Err(Error::Protocol);
        }
        if is_digit {
            self.time = self
                .time
                .wrapping_mul(10)
                .wrapping_add(i32::from(self.upper[self.digit]) - 48);
        }
        self.digit += 1;
        self.next_digit();
        Ok(())
    }
    fn append_line(&mut self) {
        // CString += appends through its source NUL; no newline is inserted.
        self.file.append(prefix(&self.line));
        self.file.end();
        self.line.clear();
        self.upper.clear();
        self.op = Op::Read;
    }
    pub fn acknowledge(&mut self) -> Result<(), Error> {
        if self.op != Op::Emit {
            return Err(Error::Protocol);
        }
        if self.final_emit {
            self.accepted = true;
            self.op = Op::Done;
        } else {
            self.file.flushed(self.time);
            self.append_line();
        }
        Ok(())
    }
    /// op/accepted/digit-unit/reserved:u32[4], rewind-position:u64,
    /// caption-start/end:i64, then raw UTF16LE. Only Digit exposes a unit;
    /// ProbeUpper/Upper expose the line; Emit exposes the accumulated caption.
    pub fn snapshot(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        let unit = if self.op == Op::Digit {
            u32::from(self.upper[self.digit])
        } else {
            0
        };
        for n in [self.op as u32, u32::from(self.accepted), unit, 0] {
            bytes.extend_from_slice(&n.to_le_bytes());
        }
        bytes.extend_from_slice(&self.position.to_le_bytes());
        bytes.extend_from_slice(&(i64::from(self.file.start) * 10000).to_le_bytes());
        let end = if self.final_emit {
            i64::MAX
        } else {
            i64::from(self.time) * 10000
        };
        bytes.extend_from_slice(&end.to_le_bytes());
        let text = match self.op {
            Op::ProbeUpper | Op::Upper => &self.line[..],
            Op::Emit => self.file.text(),
            _ => &[],
        };
        for u in text {
            bytes.extend_from_slice(&u.to_le_bytes());
        }
        bytes
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn read(run: &mut Run, text: &str, eof: bool) {
        let raw: Vec<_> = text.encode_utf16().collect();
        let spaces: Vec<_> = raw
            .iter()
            .map(|&u| u8::from(matches!(u, 9 | 13 | 32)))
            .collect();
        run.read(&raw, &spaces, eof).unwrap();
    }
    fn upper(run: &mut Run) {
        let text: Vec<_> = run
            .line
            .iter()
            .map(|&u| if (97..=122).contains(&u) { u - 32 } else { u })
            .collect();
        run.uppercase(&text).unwrap();
    }
    fn begin() -> Run {
        let mut run = Run::new(42);
        read(&mut run, "<sami>", false);
        upper(&mut run);
        read(&mut run, "discarded probe fragment", true);
        run.rewind().unwrap();
        run
    }
    fn digits(run: &mut Run) {
        while run.op == Op::Digit {
            run.digit((48..=57).contains(&run.upper[run.digit]))
                .unwrap();
        }
    }
    #[test]
    fn discovery_discards_one_more_read_then_rewinds_even_at_eof() {
        let mut run = Run::new(u64::MAX);
        read(&mut run, "<sami>", false);
        upper(&mut run);
        assert_eq!(run.op, Op::ProbeDiscard);
        assert_eq!(run.rewind(), Err(Error::Protocol));
        read(&mut run, "nonempty final probe fragment", true);
        assert_eq!(run.op, Op::Rewind);
        assert_eq!(&run.snapshot()[16..24], &u64::MAX.to_le_bytes());
        run.rewind().unwrap();
        assert_eq!(run.op, Op::Read);
        let mut missing = Run::new(0);
        read(&mut missing, "<sami>", true);
        assert_eq!(missing.op, Op::Failed);
    }
    #[test]
    fn timestamp_scan_uses_absolute_column_and_wrapping_digit_arithmetic() {
        let mut run = begin();
        read(
            &mut run,
            "123456789012<SYNC START=4294967297>caption",
            false,
        );
        upper(&mut run);
        assert_eq!(run.digit, 12);
        digits(&mut run);
        assert_eq!(run.time, 1);
        run.acknowledge().unwrap();
        assert_eq!(run.file.start, 1);
        read(&mut run, "<SYNC START=12M999>tail", false);
        upper(&mut run);
        digits(&mut run);
        assert_eq!(run.time, 12);
        run.acknowledge().unwrap();
        read(&mut run, "ignored final fragment", true);
        assert_eq!(run.op, Op::Emit);
        assert_eq!(
            i64::from_le_bytes(run.snapshot()[32..40].try_into().unwrap()),
            i64::MAX
        );
        run.acknowledge().unwrap();
        assert!(run.accepted);
    }
    #[test]
    fn comment_close_skips_the_whole_line_and_fragments_append_without_newlines() {
        let mut run = begin();
        for line in [
            "<TITLE>skip</TITLE><SYNC START=1000>hidden",
            "first",
            "second\0discarded",
        ] {
            read(&mut run, line, false);
            upper(&mut run);
        }
        assert_eq!(
            run.file.text(),
            "firstsecond".encode_utf16().collect::<Vec<_>>()
        );
        read(&mut run, "<!--", false);
        upper(&mut run);
        read(&mut run, "<SYNC START=2000>skip-->", false);
        upper(&mut run);
        assert_eq!(run.op, Op::Read);
        assert_eq!(run.file.start, 0);
        read(&mut run, "", true);
        let snapshot = run.snapshot();
        run.acknowledge().unwrap();
        assert_eq!(
            &snapshot[40..],
            &"firstsecond"
                .encode_utf16()
                .flat_map(u16::to_le_bytes)
                .collect::<Vec<_>>()
        );
    }
    #[test]
    fn invalid_callbacks_leave_state_unchanged_and_empty_sami_still_succeeds() {
        let mut run = begin();
        let before = run.snapshot();
        assert_eq!(run.read(&[65], &[2], false), Err(Error::Protocol));
        assert_eq!(run.uppercase(&[]), Err(Error::Protocol));
        assert_eq!(run.digit(true), Err(Error::Protocol));
        assert_eq!(run.snapshot(), before);
        read(&mut run, "  raw  ", false);
        let before = run.snapshot();
        assert_eq!(run.uppercase(&[65]), Err(Error::Protocol));
        assert_eq!(run.snapshot(), before);
        let mut empty = begin();
        read(&mut empty, "", true);
        empty.acknowledge().unwrap();
        assert!(empty.accepted);
    }
}
