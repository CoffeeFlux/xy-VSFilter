// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned cleanup OpenMicroDVD lifecycle. Native services supply ReadString,
//! whitespace, initialized scanf results, style conversion/case mapping and Add.
use crate::ass_document::Error;
use crate::document::Document;
use alloc::vec::Vec;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Op {
    Read = 0,
    Primary = 1,
    Fallback = 2,
    Style = 3,
    Emit = 4,
    Done = 5,
    Failed = 6,
}
pub struct Run {
    op: Op,
    line: Vec<u16>,
    style: Vec<u16>,
    pending_style: Vec<u16>,
    charset: i32,
    check: bool,
    correct_previous: bool,
    accepted: bool,
    start: i64,
    end: i64,
}
impl Run {
    pub fn new(charset: i32) -> Self {
        Self {
            op: Op::Read,
            line: Vec::new(),
            style: "Default".encode_utf16().collect(),
            pending_style: Vec::new(),
            charset,
            check: false,
            correct_previous: false,
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
        if self.op != Op::Read {
            return Err(Error::Protocol);
        }
        let line = crate::ass_input::trim(raw, spaces).ok_or(Error::Protocol)?;
        if line.len() > i32::MAX as usize {
            return Err(Error::CountOverflow);
        }
        if eof {
            self.op = Op::Done;
            self.accepted = !document.entries().is_empty();
        } else if !line.is_empty() {
            self.line = line.to_vec();
            self.op = Op::Primary;
        }
        Ok(())
    }
    /// Primary {%d}{%d} conversions; values are meaningful only at count2.
    pub fn primary(
        &mut self,
        document: &mut Document,
        count: i32,
        start: i32,
        end: i32,
    ) -> Result<(), Error> {
        if self.op != Op::Primary || !(-1..=2).contains(&count) {
            return Err(Error::Protocol);
        }
        if count == 2 {
            self.emit(document, start, end);
        } else {
            self.op = Op::Fallback;
        }
        Ok(())
    }
    /// Fallback {%d}{} conversion, BEFORE the native +1 to its scan count.
    /// Only count1 supplies start. The unused native uninitialized start/end on
    /// failed conversion is never read; no visible result depends on that value.
    pub fn fallback(
        &mut self,
        document: &mut Document,
        count: i32,
        start: i32,
    ) -> Result<(), Error> {
        if self.op != Op::Fallback || !(-1..=1).contains(&count) {
            return Err(Error::Protocol);
        }
        self.check = true;
        if count == 1 {
            self.emit(document, start, start.wrapping_add(60));
        } else {
            let c_end = self
                .line
                .iter()
                .position(|&u| u == 0)
                .unwrap_or(self.line.len());
            let close = self.line[..c_end].iter().position(|&u| u == 125);
            if let Some(close) = close.filter(|&i| i > 1 && self.line.first() == Some(&123)) {
                self.pending_style = self.line[1..close].to_vec();
                self.line = self.line[close + 1..].to_vec();
                self.op = Op::Style;
            } else {
                // Native c is scan+1, hence never EOF on this path.
                self.op = Op::Failed;
            }
        }
        Ok(())
    }
    fn emit(&mut self, document: &mut Document, start: i32, end: i32) {
        self.start = i64::from(start) * 10000;
        self.end = i64::from(end) * 10000;
        if self.correct_previous && document.correct_last_end(self.start).is_some() {
            self.correct_previous = false;
        }
        let c_end = self
            .line
            .iter()
            .position(|&u| u == 0)
            .unwrap_or(self.line.len());
        let mut after = 0;
        for _ in 0..2 {
            after = self.line[after..c_end]
                .iter()
                .position(|&u| u == 125)
                .map_or(0, |p| after + p + 1);
        }
        self.line = self.line[after..].to_vec();
        self.op = Op::Emit;
    }
    /// Completion of GetMicroDVDStyle, native name case mapping and AddStyle.
    /// The style's charset is captured before AddStyle may retire its adapter.
    pub fn style(&mut self, success: bool, name: &[u16], charset: i32) -> Result<(), Error> {
        if self.op != Op::Style {
            return Err(Error::Protocol);
        }
        if name.len() > i32::MAX as usize {
            return Err(Error::CountOverflow);
        }
        if success {
            self.style = name.to_vec();
            self.charset = charset;
            self.op = Op::Read;
        } else {
            self.op = Op::Failed;
        }
        self.pending_style.clear();
        self.line.clear();
        // Deliberately preserve check across style declarations.
        Ok(())
    }
    pub fn acknowledge(&mut self) -> Result<(), Error> {
        if self.op != Op::Emit {
            return Err(Error::Protocol);
        }
        if self.check {
            self.check = false;
            self.correct_previous = true;
        }
        self.line.clear();
        self.op = Op::Read;
        Ok(())
    }
    /// Group0: op/accepted/charset/reserved:u32, start/end:i64, raw UTF16LE.
    /// Group1: raw UTF16LE style candidate during Style, current style otherwise.
    pub fn snapshot(&self, group: u32) -> Result<Vec<u8>, Error> {
        let mut bytes = Vec::new();
        let text = match group {
            0 => {
                for n in [
                    self.op as u32,
                    u32::from(self.accepted),
                    self.charset as u32,
                    0,
                ] {
                    bytes.extend_from_slice(&n.to_le_bytes());
                }
                bytes.extend_from_slice(&self.start.to_le_bytes());
                bytes.extend_from_slice(&self.end.to_le_bytes());
                if matches!(self.op, Op::Primary | Op::Fallback | Op::Style | Op::Emit) {
                    &self.line[..]
                } else {
                    &[]
                }
            }
            1 => {
                if self.op == Op::Style {
                    &self.pending_style[..]
                } else {
                    &self.style[..]
                }
            }
            _ => return Err(Error::Protocol),
        };
        for u in text {
            bytes.extend_from_slice(&u.to_le_bytes());
        }
        Ok(bytes)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::document::{AddMode, PreparedEntry};
    fn read(run: &mut Run, doc: &Document, text: &str, eof: bool) {
        let raw: Vec<_> = text.encode_utf16().collect();
        let spaces: Vec<_> = raw
            .iter()
            .map(|&u| u8::from(matches!(u, 9 | 13 | 32)))
            .collect();
        run.read(doc, &raw, &spaces, eof).unwrap();
    }
    fn add(doc: &mut Document, start: i64, end: i64) {
        doc.add_prepared(
            PreparedEntry {
                text: alloc::vec![65],
                style: alloc::vec![],
                actor: alloc::vec![],
                effect: alloc::vec![],
                unicode: true,
                margins: [0; 4],
                layer: 0,
                start,
                end,
                readorder: -1,
            },
            AddMode::Add,
        )
        .unwrap();
    }
    #[test]
    fn style_declaration_marks_next_complete_caption_for_later_correction() {
        let mut doc = Document::default();
        let mut run = Run::new(0);
        read(&mut run, &doc, "{STYLE}{h:128}", false);
        run.primary(&mut doc, 0, 0, 0).unwrap();
        run.fallback(&mut doc, 0, 0).unwrap();
        assert_eq!(run.op, Op::Style);
        assert_eq!(
            run.pending_style,
            "STYLE".encode_utf16().collect::<Vec<_>>()
        );
        assert_eq!(run.line, "{h:128}".encode_utf16().collect::<Vec<_>>());
        run.style(true, &"Style".encode_utf16().collect::<Vec<_>>(), 128)
            .unwrap();
        read(&mut run, &doc, "{10}{80}first", false);
        run.primary(&mut doc, 2, 10, 80).unwrap();
        add(&mut doc, run.start, run.end);
        run.acknowledge().unwrap();
        let segment_end = doc.segments()[0].end;
        read(&mut run, &doc, "{20}{90}second", false);
        run.primary(&mut doc, 2, 20, 90).unwrap();
        assert_eq!(doc.entries()[0].end, 200000);
        assert_eq!(doc.segments()[0].end, segment_end); // no segment rebuild
        assert!(!run.correct_previous);
        assert!(!run.check);
        assert_eq!(run.charset, 128);
    }
    #[test]
    fn provisional_correction_survives_empty_add_and_can_go_before_start() {
        let mut doc = Document::default();
        let mut run = Run::new(0);
        read(&mut run, &doc, "{10}{}", false);
        run.primary(&mut doc, 1, 0, 0).unwrap();
        run.fallback(&mut doc, 1, 10).unwrap();
        run.acknowledge().unwrap(); // native Add omitted the empty caption
        read(&mut run, &doc, "{20}{80}second", false);
        run.primary(&mut doc, 2, 20, 80).unwrap();
        assert!(run.correct_previous);
        add(&mut doc, run.start, run.end);
        run.acknowledge().unwrap();
        read(&mut run, &doc, "{-1}{80}third", false);
        run.primary(&mut doc, 2, -1, 80).unwrap();
        assert_eq!(doc.entries()[0].end, -10000);
    }
    #[test]
    fn fallback_overflow_missing_braces_and_outer_eof_are_preserved() {
        let mut doc = Document::default();
        let mut run = Run::new(0);
        read(&mut run, &doc, "{2147483647tail", false);
        run.primary(&mut doc, 1, 0, 0).unwrap();
        run.fallback(&mut doc, 1, i32::MAX).unwrap();
        assert_eq!(
            run.line,
            "{2147483647tail".encode_utf16().collect::<Vec<_>>()
        );
        assert_eq!(run.end, i64::from(i32::MIN + 59) * 10000);
        run.acknowledge().unwrap();
        add(&mut doc, 0, 10);
        read(&mut run, &doc, "ignored", true);
        assert_eq!(run.op, Op::Done);
        assert!(run.accepted);
    }
    #[test]
    fn protocol_rejection_is_atomic_and_snapshots_own_raw_units() {
        let mut doc = Document::default();
        let mut run = Run::new(0);
        read(&mut run, &doc, "{1}{4}raw\0tail", false);
        let before = run.snapshot(0).unwrap();
        assert_eq!(run.primary(&mut doc, 3, 0, 0), Err(Error::Protocol));
        assert_eq!(run.style(true, &[], 0), Err(Error::Protocol));
        assert_eq!(run.snapshot(0).unwrap(), before);
        run.primary(&mut doc, 2, 1, 4).unwrap();
        let retained = run.snapshot(0).unwrap();
        run.acknowledge().unwrap();
        assert_eq!(
            &retained[32..],
            &"raw\0tail"
                .encode_utf16()
                .flat_map(u16::to_le_bytes)
                .collect::<Vec<_>>()
        );
        read(&mut run, &doc, "bad", false);
        run.primary(&mut doc, 0, 0, 0).unwrap();
        run.fallback(&mut doc, 0, 0).unwrap();
        assert_eq!(run.op, Op::Failed);
        assert_eq!(run.read(&doc, &[], &[], true), Err(Error::Protocol));
    }
}
