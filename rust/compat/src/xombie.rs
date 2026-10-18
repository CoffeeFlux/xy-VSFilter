// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned XombieSub parser policy from cleanup STS.cpp. Native adapters supply
//! CString whitespace/lowercase and exact CRT conversion results. Native Add
//! and AddStyle remain callbacks; no native buffers or objects are retained.
use crate::ass_document::Error;
use crate::document::{Document, PreparedEntry, ScriptField};
use crate::document_style::StyleValue;
use alloc::vec::Vec;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Op {
    Read = 0,
    Lower = 1,
    Text = 2,
    Integer = 3,
    Real = 4,
    Style = 5,
    Entry = 6,
    FontRead = 7,
    FontInstall = 8,
    Done = 9,
    Failed = 10,
}

pub struct Run {
    op: Op,
    kind: u32,
    field: u32,
    key: Vec<u16>,
    rest: Vec<u16>,
    first: Vec<u16>,
    name: Vec<u16>,
    style: StyleValue,
    entry: PreparedEntry,
    times: [i32; 8],
    fonts: crate::ass_fonts::Fonts,
    accepted: bool,
}
impl Default for Run {
    fn default() -> Self {
        Self {
            op: Op::Read,
            kind: 0,
            field: 0,
            key: Vec::new(),
            rest: Vec::new(),
            first: Vec::new(),
            name: Vec::new(),
            style: crate::ass_style_value::Builder::default().value,
            entry: PreparedEntry {
                text: Vec::new(),
                style: Vec::new(),
                actor: Vec::new(),
                effect: Vec::new(),
                unicode: false,
                margins: [0; 4],
                layer: 0,
                start: 0,
                end: 0,
                readorder: 0,
            },
            times: [0; 8],
            fonts: Default::default(),
            accepted: false,
        }
    }
}
fn cstr(text: &[u16]) -> &[u16] {
    &text[..text.iter().position(|&u| u == 0).unwrap_or(text.len())]
}
fn eq(text: &[u16], key: &[u8]) -> bool {
    let text = cstr(text);
    text.len() == key.len() && text.iter().zip(key).all(|(&a, &b)| a == u16::from(b))
}
fn nonnegative(value: f64) -> f64 {
    if value > 0.0 {
        value
    } else {
        0.0
    }
}
// Actual cleanup MSVC evaluation is right operand first. CString concatenation
// copies explicit lengths; unlike Append, it preserves embedded NUL tails.
fn joined(second: &[u16], first: &[u16]) -> Vec<u16> {
    let mut result = second.to_vec();
    result.push(95);
    result.extend_from_slice(first);
    result
}
impl Run {
    pub fn read(
        &mut self,
        document: &Document,
        input: &[u16],
        spaces: &[u8],
        eof: bool,
    ) -> Result<(), Error> {
        if self.op == Op::FontRead {
            if !self.fonts.read(input, spaces, eof) {
                return Err(Error::Protocol);
            }
            self.font_action();
            return Ok(());
        }
        if self.op != Op::Read {
            return Err(Error::Protocol);
        }
        let line = crate::ass_input::trim(input, spaces).ok_or(Error::Protocol)?;
        if eof {
            self.op = Op::Done;
            self.accepted = !document.entries().is_empty();
            return Ok(());
        }
        if line.is_empty() || line[0] == 59 {
            return Ok(());
        }
        let end = cstr(line).len();
        if let Some(split) = line[..end].iter().position(|&u| u == 61) {
            self.key = line[..split].to_vec();
            self.rest = line[split + 1..].to_vec();
        } else {
            self.key = line.to_vec();
            self.rest = line.to_vec();
        }
        self.op = Op::Lower;
        Ok(())
    }
    pub fn classify(&mut self, key: &[u16]) -> Result<(), Error> {
        if self.op != Op::Lower {
            return Err(Error::Protocol);
        }
        self.kind = [
            &b"version"[..],
            b"screenhorizontal",
            b"screenvertical",
            b"style",
            b"line",
            b"fontname",
        ]
        .iter()
        .position(|k| eq(key, k))
        .map_or(0, |i| i as u32 + 1);
        self.field = 1;
        match self.kind {
            1 => self.op = Op::Real,
            2 | 3 => self.op = Op::Integer,
            4 => {
                self.style = crate::ass_style_value::Builder::default().value;
                self.name.clear();
                self.first.clear();
                self.op = Op::Text;
            }
            5 => {
                self.entry = Self::default().entry;
                self.times = [0; 8];
                self.first.clear();
                self.op = Op::Text;
            }
            6 => {
                self.fonts = Default::default();
                self.op = Op::FontRead;
            }
            _ => self.op = Op::Read,
        }
        Ok(())
    }
    pub fn failed(&mut self, document: &mut Document) -> Result<(), Error> {
        if !matches!(self.op, Op::Text | Op::Integer | Op::Real) {
            return Err(Error::Protocol);
        }
        if matches!(self.kind, 2 | 3) {
            document.set_script_size(0, 0);
        }
        self.op = Op::Failed;
        Ok(())
    }
    fn next(&mut self) {
        self.field += 1;
        self.op = match self.kind {
            4 => match self.field {
                1..=3 => Op::Text,
                4 | 18..=23 | 25 | 26 => Op::Real,
                5..=17 | 24 | 27..=31 => Op::Integer,
                _ => Op::Style,
            },
            5 => match self.field {
                1 | 2 | 12..=14 => Op::Text,
                3..=11 | 15..=17 => Op::Integer,
                _ => Op::Entry,
            },
            _ => Op::Read,
        };
        if self.op == Op::Style {
            let s = &mut self.style;
            s.font_scale_x = nonnegative(s.font_scale_x);
            s.font_scale_y = nonnegative(s.font_scale_y);
            s.font_spacing = nonnegative(s.font_spacing);
            s.outline_width_x = nonnegative(s.outline_width_x);
            s.outline_width_y = nonnegative(s.outline_width_y);
            s.shadow_depth_x = nonnegative(s.shadow_depth_x);
            s.shadow_depth_y = nonnegative(s.shadow_depth_y);
            s.border_style = i32::from(s.border_style == 3);
        }
        if self.op == Op::Entry {
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
            self.entry.start = time(&self.times[..4]);
            self.entry.end = time(&self.times[4..]);
            self.entry.text = self.rest.clone();
            let stars = self.entry.style.iter().take_while(|&&u| u == 42).count();
            self.entry.style.drain(..stars);
        }
    }
    /// kind matches Op's Text/Integer/Real. Preserve supplied raw f64 bits and
    /// i32 bits; conversion and remainder mutation occur outside registry loans.
    pub fn supply(
        &mut self,
        document: &mut Document,
        kind: u32,
        bits: u64,
        text: &[u16],
        rest: &[u16],
    ) -> Result<(), Error> {
        if kind != self.op as u32
            || !matches!(self.op, Op::Text | Op::Integer | Op::Real)
            || (kind == 2 && bits != 0)
            || (kind != 2 && !text.is_empty())
            || (kind == 3 && bits > u32::MAX as u64)
        {
            return Err(Error::Protocol);
        }
        if text.len() > i32::MAX as usize || rest.len() > i32::MAX as usize {
            return Err(Error::CountOverflow);
        }
        let n = bits as u32 as i32;
        let f = f64::from_bits(bits);
        if matches!(self.kind, 2 | 3) {
            let size = document.script_size().ok_or(Error::UnsuppliedScreen)?;
            let axis = if self.kind == 2 {
                ScriptField::Width
            } else {
                ScriptField::Height
            };
            document.replace_script_field(axis, n);
            if self.kind == 2 && size[1] <= 0 {
                document.replace_script_field(
                    ScriptField::Height,
                    if n == 1280 {
                        1024
                    } else {
                        n.wrapping_mul(3) / 4
                    },
                );
            }
            if self.kind == 3 && size[0] <= 0 {
                document.replace_script_field(
                    ScriptField::Width,
                    if n == 1024 {
                        1280
                    } else {
                        n.wrapping_mul(4) / 3
                    },
                );
            }
        } else if self.kind == 4 {
            let s = &mut self.style;
            match self.field {
                1 => self.first = text.to_vec(),
                2 => self.name = joined(text, &self.first),
                3 => s.font_name = text.to_vec(),
                4 => s.font_size = f,
                5..=8 => s.colors[(self.field - 5) as usize] = n as u32,
                9..=12 => s.alpha[(self.field - 9) as usize] = n as u8,
                13 => s.font_weight = if n != 0 { 700 } else { 400 },
                14 => s.italic = n != 0,
                15 => s.underline = n != 0,
                16 => s.strikeout = n != 0,
                17 => s.blur = f64::from(n != 0),
                18 => s.font_scale_x = f,
                19 => s.font_scale_y = f,
                20 => s.font_spacing = f,
                21 => s.font_angle_x = f,
                22 => s.font_angle_y = f,
                23 => s.font_angle_z = f,
                24 => s.border_style = n,
                25 => {
                    s.outline_width_x = f;
                    s.outline_width_y = f;
                }
                26 => {
                    s.shadow_depth_x = f;
                    s.shadow_depth_y = f;
                }
                27 => s.screen_alignment = n,
                28 => s.margins[0] = n,
                29 => s.margins[2] = n,
                30 => {
                    s.margins[1] = n;
                    s.margins[3] = n;
                }
                31 => s.char_set = n,
                _ => return Err(Error::Protocol),
            }
        } else if self.kind == 5 {
            match self.field {
                1 if !eq(text, b"D") => {
                    self.op = Op::Read;
                    return Ok(());
                }
                1 | 2 => {}
                3 => self.entry.layer = n,
                4..=11 => self.times[(self.field - 4) as usize] = n,
                12 => self.first = text.to_vec(),
                13 => self.entry.style = joined(text, &self.first),
                14 => self.entry.actor = text.to_vec(),
                15 => self.entry.margins[0] = n,
                16 => self.entry.margins[2] = n,
                17 => {
                    self.entry.margins[1] = n;
                    self.entry.margins[3] = n;
                }
                _ => return Err(Error::Protocol),
            }
        }
        self.rest = rest.to_vec();
        self.next();
        Ok(())
    }
    fn font_action(&mut self) {
        self.op = match self.fonts.action() {
            crate::ass_fonts::Action::Read => Op::FontRead,
            crate::ass_fonts::Action::Install => Op::FontInstall,
            crate::ass_fonts::Action::Done => Op::Read,
        };
    }
    pub fn acknowledge(&mut self) -> Result<(), Error> {
        match self.op {
            Op::Style | Op::Entry => self.op = Op::Read,
            Op::FontInstall => {
                if !self.fonts.installed() {
                    return Err(Error::Protocol);
                }
                self.font_action();
            }
            _ => return Err(Error::Protocol),
        }
        Ok(())
    }
    /// Group0: op:u32,separator:u32,kind:u32,accepted:u32,raw UTF16 text.
    /// Group1 style wire;2 raw style name;3 entry wire;4 decoded font bytes.
    pub fn part(&self, group: u32) -> Result<Vec<u8>, Error> {
        match group {
            0 => {
                let sep = if self.kind == 5 {
                    match self.field {
                        4 | 5 | 8 | 9 => 58,
                        6 | 10 => 46,
                        _ => 44,
                    }
                } else {
                    44
                };
                let mut bytes = Vec::new();
                for n in [self.op as u32, sep, self.kind, u32::from(self.accepted)] {
                    bytes.extend_from_slice(&n.to_le_bytes());
                }
                let text = if self.op == Op::Lower {
                    &self.key[..]
                } else if matches!(self.op, Op::Text | Op::Integer | Op::Real) {
                    &self.rest[..]
                } else {
                    &[]
                };
                for u in text {
                    bytes.extend_from_slice(&u.to_le_bytes());
                }
                Ok(bytes)
            }
            1 if self.op == Op::Style => {
                crate::document_style::encode(&self.style).map_err(|_| Error::CountOverflow)
            }
            2 if self.op == Op::Style => {
                Ok(self.name.iter().flat_map(|u| u.to_le_bytes()).collect())
            }
            3 if self.op == Op::Entry => {
                crate::document_wire::encode_entry(&self.entry).map_err(|_| Error::CountOverflow)
            }
            4 if self.op == Op::FontInstall => Ok(self.fonts.snapshot()[8..].to_vec()),
            _ => Err(Error::Protocol),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn units(s: &str) -> Vec<u16> {
        s.encode_utf16().collect()
    }
    fn start(run: &mut Run, doc: &Document, key: &str) {
        let text = units(key);
        run.read(doc, &text, &alloc::vec![0;text.len()], false)
            .unwrap();
        run.classify(&text).unwrap();
    }
    fn text(run: &mut Run, doc: &mut Document, s: &str) {
        run.supply(doc, 2, 0, &units(s), &[]).unwrap();
    }
    fn integer(run: &mut Run, doc: &mut Document, n: i32) {
        run.supply(doc, 3, n as u32 as u64, &[], &[]).unwrap();
    }
    #[test]
    fn eof_discards_text_and_font_requests_block_outer_reads_until_acknowledged() {
        let mut doc = Document::default();
        let mut run = Run::default();
        start(&mut run, &doc, "fontname");
        run.read(&doc, &[33, 33], &[0, 0], false).unwrap();
        run.read(&doc, &[33], &[0], true).unwrap();
        let bytes = run.part(4).unwrap();
        assert_eq!(bytes, [0]);
        assert_eq!(run.read(&doc, &[], &[], true), Err(Error::Protocol));
        run.acknowledge().unwrap();
        assert_eq!(run.part(4), Err(Error::Protocol));
        run.read(&doc, &units("style=discarded"), &[0; 15], true)
            .unwrap();
        assert_eq!(run.op, Op::Done);
        assert!(!run.accepted);
        assert_eq!(run.classify(&units("style")), Err(Error::Protocol));
        assert_eq!(bytes, [0]);
        doc.set_script_size(0, 0);
    }
    #[test]
    fn screen_failures_reset_both_axes_and_invalid_protocol_preserves_state() {
        let mut doc = Document::default();
        doc.set_script_size(0, 0);
        let mut run = Run::default();
        start(&mut run, &doc, "screenhorizontal");
        assert_eq!(run.supply(&mut doc, 2, 0, &[], &[]), Err(Error::Protocol));
        assert_eq!(doc.script_size(), Some([0, 0]));
        integer(&mut run, &mut doc, 1280);
        assert_eq!(doc.script_size(), Some([1280, 1024]));
        start(&mut run, &doc, "screenvertical");
        run.failed(&mut doc).unwrap();
        assert_eq!(doc.script_size(), Some([0, 0]));
        assert_eq!(run.op, Op::Failed);
    }
    #[test]
    fn reversed_name_raw_tail_and_distinct_xombie_style_normalization() {
        let mut doc = Document::default();
        let mut run = Run::default();
        start(&mut run, &doc, "style");
        text(&mut run, &mut doc, "First\0tail");
        text(&mut run, &mut doc, "**Second");
        text(&mut run, &mut doc, "Raw");
        while run.op != Op::Style {
            if run.op == Op::Real {
                run.supply(&mut doc, 4, (-0.0f64).to_bits(), &[], &[])
                    .unwrap();
            } else {
                integer(&mut run, &mut doc, -1);
            }
        }
        assert_eq!(run.name, units("**Second_First\0tail"));
        let style = crate::document_style::decode(&run.part(1).unwrap()).unwrap();
        assert_eq!(style.colors, [u32::MAX; 4]);
        assert_eq!(style.alpha, [255; 4]);
        assert_eq!(style.char_set, -1);
        assert_eq!(style.blur, 1.0);
        assert_eq!(style.font_size.to_bits(), (-0.0f64).to_bits());
        assert_eq!(style.font_angle_x.to_bits(), (-0.0f64).to_bits());
        assert_eq!(style.font_scale_x.to_bits(), 0);
        assert_eq!(style.border_style, 0);
        let snapshot = run.part(2).unwrap();
        run.acknowledge().unwrap();
        assert_eq!(run.part(2), Err(Error::Protocol));
        assert!(!snapshot.is_empty());
    }
    #[test]
    fn dialogue_retains_full_milliseconds_and_add_payload_before_native_trim() {
        let mut doc = Document::default();
        let mut run = Run::default();
        start(&mut run, &doc, "line");
        text(&mut run, &mut doc, "D");
        text(&mut run, &mut doc, "discarded id");
        for n in [9, 0, 0, 1, 234, 0, 0, 2, 345] {
            integer(&mut run, &mut doc, n);
        }
        text(&mut run, &mut doc, "First");
        text(&mut run, &mut doc, "**Second");
        text(&mut run, &mut doc, "Actor");
        integer(&mut run, &mut doc, 1);
        integer(&mut run, &mut doc, 2);
        run.supply(&mut doc, 3, 3, &[], &units("  body  ")).unwrap();
        let entry = crate::document_wire::decode_entry(&run.part(3).unwrap()).unwrap();
        assert_eq!((entry.start, entry.end), (12_340_000, 23_450_000));
        assert_eq!(entry.margins, [1, 3, 2, 3]);
        assert_eq!(entry.style, units("Second_First"));
        assert_eq!(entry.text, units("  body  "));
        assert_eq!(entry.actor, units("Actor"));
    }
}
