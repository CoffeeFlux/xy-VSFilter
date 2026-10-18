// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned SubRip and MPL2 parser loops from cleanup STS.cpp. Native ReadString,
//! CString predicates, exact scanf conversions, formatting and Add are services.
//! No caller buffer, native object or callback is retained by the session.
use crate::ass_document::Error;
use crate::document::Document;
use alloc::vec::Vec;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Op {
    ReadHeader = 0,
    ScanHeader = 1,
    ReadCaption = 2,
    ScanCaption = 3,
    Emit = 4,
    Done = 5,
    Failed = 6,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Format {
    SubRip,
    Mpl2,
}
pub struct Run {
    format: Format,
    op: Op,
    line: Vec<u16>,
    cue: crate::subrip_cue::Cue,
    eof: bool,
    accepted: bool,
    start: i64,
    end: i64,
}
impl Run {
    pub fn new(format: Format) -> Self {
        Self {
            format,
            op: Op::ReadHeader,
            line: Vec::new(),
            cue: Default::default(),
            eof: false,
            accepted: false,
            start: 0,
            end: 0,
        }
    }
    pub fn read(
        &mut self,
        document: &Document,
        raw: &[u16],
        spaces: &[u8],
        eof: bool,
    ) -> Result<(), Error> {
        if !matches!(self.op, Op::ReadHeader | Op::ReadCaption) {
            return Err(Error::Protocol);
        }
        let line = crate::ass_input::trim(raw, spaces).ok_or(Error::Protocol)?;
        if self.op == Op::ReadHeader {
            // Both outer while(ReadString) loops discard false-read fragments.
            if eof {
                self.op = Op::Done;
                self.accepted = !document.entries().is_empty();
                return Ok(());
            }
            if line.is_empty() {
                return Ok(());
            }
            self.line = line.to_vec();
            self.op = Op::ScanHeader;
        } else {
            // SubRip's inner loop checks raw empty EOF before trimming.
            if eof && raw.is_empty() {
                self.op = Op::Emit;
                return Ok(());
            }
            if line.is_empty() {
                self.cue.empty_line();
            }
            self.line = line.to_vec();
            self.eof = eof;
            self.op = Op::ScanCaption;
        }
        Ok(())
    }
    /// Only initialized native scan values may be supplied: SubRip c>=11 gives
    /// [h1,m1,s1,ms1,h2,m2,s2,ms2], with each secondary ms parse defaulting to 0;
    /// MPL2 c==2 gives [start,end,0,0,0,0,0,0]. Other counts ignore the array.
    pub fn header(&mut self, count: i32, values: [i32; 8]) -> Result<(), Error> {
        let max = if self.format == Format::SubRip { 12 } else { 2 };
        if self.op != Op::ScanHeader || !(-1..=max).contains(&count) {
            return Err(Error::Protocol);
        }
        match self.format {
            Format::SubRip => {
                if count >= 11 {
                    fn time(t: &[i32]) -> i64 {
                        i64::from(t[0])
                            .wrapping_mul(60)
                            .wrapping_add(i64::from(t[1]))
                            .wrapping_mul(60)
                            .wrapping_add(i64::from(t[2]))
                            .wrapping_mul(1000)
                            .wrapping_add(i64::from(t[3]))
                            .wrapping_mul(10000)
                    }
                    self.start = time(&values[..4]);
                    self.end = time(&values[4..]);
                    self.cue = Default::default();
                    self.op = Op::ReadCaption;
                } else {
                    self.op = if count == 1 || count == -1 {
                        Op::ReadHeader
                    } else {
                        Op::Failed
                    };
                }
            }
            Format::Mpl2 => {
                if count == 2 {
                    self.start = i64::from(values[0]) * 1_000_000;
                    self.end = i64::from(values[1]) * 1_000_000;
                    // CString Find stops at NUL. Missing second ']' makes
                    // Find return -1, then Mid(-1+1) returns the entire line.
                    let c_end = self
                        .line
                        .iter()
                        .position(|&u| u == 0)
                        .unwrap_or(self.line.len());
                    let first = self.line[..c_end].iter().position(|&u| u == 93);
                    let after = first.map_or(0, |p| p + 1);
                    let start = self.line[after..c_end]
                        .iter()
                        .position(|&u| u == 93)
                        .map_or(0, |p| after + p + 1);
                    self.line = self.line[start..].to_vec();
                    self.op = Op::Emit;
                } else {
                    self.op = if count == -1 {
                        Op::ReadHeader
                    } else {
                        Op::Failed
                    };
                }
            }
        }
        Ok(())
    }
    pub fn caption(&mut self, numeric_only: bool) -> Result<(), Error> {
        if self.op != Op::ScanCaption {
            return Err(Error::Protocol);
        }
        if self.cue.boundary(numeric_only) {
            self.op = Op::Emit;
            return Ok(());
        }
        // tmp+'\n' preserves counted text, then CString += truncates that
        // fragment at NUL. A NUL in tmp therefore also suppresses its newline.
        let end = self
            .line
            .iter()
            .position(|&u| u == 0)
            .unwrap_or(self.line.len());
        let newline = usize::from(end == self.line.len());
        if self
            .cue
            .text()
            .len()
            .saturating_add(end)
            .saturating_add(newline)
            > i32::MAX as usize
        {
            return Err(Error::CountOverflow);
        }
        self.cue.append(&self.line[..end]);
        if newline != 0 {
            self.cue.append(&[10]);
        }
        self.op = if self.eof { Op::Emit } else { Op::ReadCaption };
        Ok(())
    }
    pub fn acknowledge(&mut self) -> Result<(), Error> {
        if self.op != Op::Emit {
            return Err(Error::Protocol);
        }
        self.op = Op::ReadHeader;
        self.line.clear();
        self.cue = Default::default();
        Ok(())
    }
    /// op:u32,accepted:u32,start:i64,end:i64, followed by raw UTF16LE. Scan
    /// requests expose the trimmed line; Emit exposes owned unformatted text.
    pub fn snapshot(&self) -> Vec<u8> {
        let text = match self.op {
            Op::ScanHeader | Op::ScanCaption => &self.line[..],
            Op::Emit => {
                if self.format == Format::SubRip {
                    self.cue.text()
                } else {
                    &self.line[..]
                }
            }
            _ => &[],
        };
        let mut bytes = Vec::new();
        bytes.extend_from_slice(&(self.op as u32).to_le_bytes());
        bytes.extend_from_slice(&u32::from(self.accepted).to_le_bytes());
        bytes.extend_from_slice(&self.start.to_le_bytes());
        bytes.extend_from_slice(&self.end.to_le_bytes());
        for u in text {
            bytes.extend_from_slice(&u.to_le_bytes());
        }
        bytes
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    fn read(run: &mut Run, doc: &Document, text: &str, eof: bool) {
        let raw: Vec<_> = text.encode_utf16().collect();
        let spaces: Vec<_> = raw
            .iter()
            .map(|&u| u8::from(matches!(u, 9 | 13 | 32)))
            .collect();
        run.read(doc, &raw, &spaces, eof).unwrap();
    }
    fn start(run: &mut Run, doc: &Document) {
        read(run, doc, "time", false);
        run.header(12, [0, 0, 1, 234, 0, 0, 4, 567]).unwrap();
    }
    #[test]
    fn blank_state_survives_intervening_text_and_consumes_numeric_boundary() {
        let doc = Document::default();
        let mut run = Run::new(Format::SubRip);
        start(&mut run, &doc);
        read(&mut run, &doc, "1", false);
        run.caption(true).unwrap();
        read(&mut run, &doc, " ", false);
        run.caption(false).unwrap();
        read(&mut run, &doc, "later", false);
        run.caption(false).unwrap();
        read(&mut run, &doc, "2", false);
        run.caption(true).unwrap();
        assert_eq!(run.op, Op::Emit);
        assert_eq!(
            run.cue.text(),
            &"1\n\nlater\n".encode_utf16().collect::<Vec<_>>()
        );
        assert_eq!((run.start, run.end), (12_340_000, 45_670_000));
        let snapshot = run.snapshot();
        run.acknowledge().unwrap();
        assert!(!snapshot[24..].is_empty());
        assert_eq!(run.caption(true), Err(Error::Protocol));
    }
    #[test]
    fn inner_eof_keeps_nonempty_fragment_and_nul_suppresses_its_newline() {
        let doc = Document::default();
        let mut run = Run::new(Format::SubRip);
        start(&mut run, &doc);
        read(&mut run, &doc, "prefix\0tail", true);
        run.caption(false).unwrap();
        assert_eq!(run.op, Op::Emit);
        assert_eq!(run.cue.text(), &"prefix".encode_utf16().collect::<Vec<_>>());
        run.acknowledge().unwrap();
        read(&mut run, &doc, "ignored final header", true);
        assert_eq!(run.op, Op::Done);
        assert!(!run.accepted);
        let mut run = Run::new(Format::SubRip);
        start(&mut run, &doc);
        read(&mut run, &doc, "", true);
        assert_eq!(run.op, Op::Emit);
        assert!(run.cue.text().is_empty());
    }
    #[test]
    fn mpl2_missing_closing_bracket_reuses_whole_line_and_keeps_raw_tail() {
        let doc = Document::default();
        let mut run = Run::new(Format::Mpl2);
        for (input, expected) in [("[1][2tail", "[1][2tail"), ("[1][2]tail\0raw", "tail\0raw")] {
            read(&mut run, &doc, input, false);
            let before = run.snapshot();
            assert_eq!(run.header(3, [0; 8]), Err(Error::Protocol));
            assert_eq!(run.snapshot(), before);
            run.header(2, [-1, 2, 0, 0, 0, 0, 0, 0]).unwrap();
            assert_eq!(run.line, expected.encode_utf16().collect::<Vec<_>>());
            assert_eq!((run.start, run.end), (-1_000_000, 2_000_000));
            run.acknowledge().unwrap();
        }
    }
    #[test]
    fn invalid_predicates_and_phase_leave_snapshot_unchanged() {
        let doc = Document::default();
        let mut run = Run::new(Format::SubRip);
        let before = run.snapshot();
        assert_eq!(run.read(&doc, &[65], &[2], false), Err(Error::Protocol));
        assert_eq!(run.header(0, [0; 8]), Err(Error::Protocol));
        assert_eq!(run.snapshot(), before);
        read(&mut run, &doc, "junk", false);
        run.header(0, [0; 8]).unwrap();
        assert_eq!(run.op, Op::Failed);
        assert_eq!(run.read(&doc, &[], &[], true), Err(Error::Protocol));
    }
}
