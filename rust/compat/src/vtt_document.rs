// SPDX-License-Identifier: GPL-2.0-or-later
//! Cleanup OpenVTT input, cue recovery, duplicate suppression and color ownership.
//! ReadString, CString whitespace, CRT scans, CSS regexes, formatting and Add
//! remain explicit services. All persistent values are owned raw UTF-16 units.
use crate::ass_document::Error;
use crate::document::Document;
use alloc::collections::BTreeMap;
use alloc::vec::Vec;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Op {
    ReadMagic = 0,
    ReadHeader = 1,
    ReadStyle = 2,
    ParseStyle = 3,
    ScanHeader = 4,
    TimeStart = 5,
    TimeEnd = 6,
    ReadCaption = 7,
    ConvertCaption = 8,
    Emit = 9,
    Done = 10,
    Failed = 11,
}
#[derive(Default)]
struct Color {
    foreground: Vec<u16>,
    background: Vec<u16>,
}
pub struct Run {
    op: Op,
    line: Vec<u16>,
    header: Vec<u16>,
    style: Vec<u16>,
    tokens: [Vec<u16>; 3],
    caption: Vec<u16>,
    last_caption: Vec<u16>,
    last_header: Vec<u16>,
    colors: BTreeMap<Vec<u16>, Color>,
    found_cue: bool,
    accepted: bool,
    start: i64,
    end: i64,
}
fn prefix(raw: &[u16]) -> &[u16] {
    &raw[..raw.iter().position(|&u| u == 0).unwrap_or(raw.len())]
}
fn ascii(raw: &[u16], text: &[u8]) -> bool {
    prefix(raw)
        .iter()
        .copied()
        .eq(text.iter().copied().map(u16::from))
}
fn find(raw: &[u16], pattern: &[u16]) -> Option<usize> {
    prefix(raw)
        .windows(pattern.len())
        .position(|w| w == pattern)
}
fn count(n: usize) -> Result<u32, Error> {
    if n > i32::MAX as usize {
        Err(Error::CountOverflow)
    } else {
        Ok(n as u32)
    }
}
fn append(bytes: &mut Vec<u8>, units: &[u16]) {
    bytes.extend(units.iter().flat_map(|u| u.to_le_bytes()));
}
/// Exact three-string packet: three LE unit counts, then the three raw spans.
pub fn decode_three(bytes: &[u8]) -> Result<[Vec<u16>; 3], Error> {
    if bytes.len() < 12 {
        return Err(Error::Protocol);
    }
    let mut lengths = [0; 3];
    let mut total = 12usize;
    for (n, word) in lengths.iter_mut().zip(bytes[..12].chunks_exact(4)) {
        *n = u32::from_le_bytes(word.try_into().unwrap()) as usize;
        count(*n)?;
        total = total
            .checked_add(n.checked_mul(2).ok_or(Error::CountOverflow)?)
            .ok_or(Error::CountOverflow)?;
    }
    if total != bytes.len() {
        return Err(Error::Protocol);
    }
    let mut offset = 12;
    Ok(lengths.map(|n| {
        let text = bytes[offset..offset + n * 2]
            .chunks_exact(2)
            .map(|p| u16::from_le_bytes([p[0], p[1]]))
            .collect();
        offset += n * 2;
        text
    }))
}
impl Default for Run {
    fn default() -> Self {
        Self::new()
    }
}
impl Run {
    pub fn new() -> Self {
        Self {
            op: Op::ReadMagic,
            line: Vec::new(),
            header: Vec::new(),
            style: Vec::new(),
            tokens: Default::default(),
            caption: Vec::new(),
            last_caption: Vec::new(),
            last_header: Vec::new(),
            colors: BTreeMap::new(),
            found_cue: false,
            accepted: false,
            start: 0,
            end: 0,
        }
    }
    fn default_colors(&mut self) {
        for (name, value) in [
            ("white", "ffffff"),
            ("lime", "00ff00"),
            ("cyan", "00ffff"),
            ("red", "ff0000"),
            ("yellow", "ffff00"),
            ("magenta", "ff00ff"),
            ("blue", "0000ff"),
            ("black", "000000"),
        ] {
            let foreground: Vec<u16> = value.encode_utf16().collect();
            self.colors.insert(
                ".".encode_utf16().chain(name.encode_utf16()).collect(),
                Color {
                    foreground: foreground.clone(),
                    background: Vec::new(),
                },
            );
            self.colors.insert(
                ".bg_".encode_utf16().chain(name.encode_utf16()).collect(),
                Color {
                    foreground: Vec::new(),
                    background: foreground,
                },
            );
        }
    }
    pub fn read(
        &mut self,
        doc: &Document,
        raw: &[u16],
        spaces: &[u8],
        eof: bool,
    ) -> Result<(), Error> {
        if !matches!(
            self.op,
            Op::ReadMagic | Op::ReadHeader | Op::ReadStyle | Op::ReadCaption
        ) || raw.len() != spaces.len()
            || spaces.iter().any(|&s| s > 1)
        {
            return Err(Error::Protocol);
        }
        count(raw.len())?;
        let right = spaces.iter().rposition(|&s| s == 0).map_or(0, |p| p + 1);
        match self.op {
            Op::ReadMagic => {
                // The first read result is ignored, including an EOF fragment.
                if raw.len() >= 6 && ascii(&raw[..6], b"WEBVTT") {
                    self.default_colors();
                    self.op = Op::ReadHeader;
                } else {
                    self.op = Op::Failed;
                }
            }
            Op::ReadHeader => {
                if eof {
                    self.op = Op::Done;
                    self.accepted = !doc.entries().is_empty();
                } else {
                    self.line = raw[..right].to_vec();
                    if !self.found_cue
                        && (ascii(&self.line, b"STYLE") || ascii(&self.line, b"Style:"))
                    {
                        self.style.clear();
                        self.op = Op::ReadStyle;
                    } else {
                        self.after_style();
                    }
                }
            }
            Op::ReadStyle => {
                let arrow = find(raw, &[45, 45, 62]).is_some();
                if eof || arrow || raw.is_empty() {
                    // False-read fragments are reused as headers without trimming.
                    self.line = if !eof && arrow {
                        raw[..right].to_vec()
                    } else {
                        raw.to_vec()
                    };
                    while let Some(start) = find(&self.style, &[47, 42]) {
                        let end = find(&self.style[start + 2..], &[42, 47])
                            .map_or(self.style.len(), |p| start + 2 + p + 1);
                        // Cleanup deletes through '*' only: the closing '/' survives.
                        self.style.drain(start..end);
                    }
                    if self.style.is_empty() {
                        self.after_style();
                    } else {
                        self.op = Op::ParseStyle;
                    }
                } else {
                    let tail = prefix(raw);
                    count(
                        self.style
                            .len()
                            .checked_add(tail.len())
                            .and_then(|n| n.checked_add(1))
                            .ok_or(Error::CountOverflow)?,
                    )?;
                    self.style.push(32);
                    self.style.extend_from_slice(tail);
                }
            }
            Op::ReadCaption => {
                if eof || right == 0 {
                    self.finish_cue();
                } else {
                    self.line = raw[..right].to_vec();
                    self.op = Op::ConvertCaption;
                }
            }
            _ => unreachable!(),
        }
        Ok(())
    }
    fn after_style(&mut self) {
        self.op = if self.line.is_empty() {
            Op::ReadHeader
        } else {
            Op::ScanHeader
        };
    }
    pub fn header(&mut self, count: i32, tokens: [Vec<u16>; 3]) -> Result<(), Error> {
        if self.op != Op::ScanHeader
            || !(-1..=3).contains(&count)
            || (count == 2 && !tokens[2].is_empty())
            || (count < 2 && tokens.iter().any(|s| !s.is_empty()))
        {
            return Err(Error::Protocol);
        }
        for token in &tokens {
            self::count(token.len())?;
        }
        if count >= 2 {
            self.tokens = tokens;
            self.header = self.line.clone();
            self.op = Op::TimeStart;
        } else {
            self.op = Op::ReadHeader;
        }
        Ok(())
    }
    pub fn time(&mut self, count: i32, values: [i32; 4]) -> Result<(), Error> {
        if !matches!(self.op, Op::TimeStart | Op::TimeEnd)
            || !(-1..=7).contains(&count)
            || (count == 5 && values[3] != 0)
            || (!matches!(count, 5 | 7) && values != [0; 4])
        {
            return Err(Error::Protocol);
        }
        if !matches!(count, 5 | 7) {
            self.op = Op::ReadHeader;
            return Ok(());
        }
        let [h, m, s, ms] = if count == 5 {
            [0, values[0], values[1], values[2]]
        } else {
            values
        };
        let time = i64::from(h)
            .wrapping_mul(60)
            .wrapping_add(i64::from(m))
            .wrapping_mul(60)
            .wrapping_add(i64::from(s))
            .wrapping_mul(1000)
            .wrapping_add(i64::from(ms))
            .wrapping_mul(10000);
        if self.op == Op::TimeStart {
            self.start = time;
            self.op = Op::TimeEnd;
        } else {
            self.end = time;
            self.found_cue = true;
            self.caption.clear();
            self.op = Op::ReadCaption;
        }
        Ok(())
    }
    pub fn color(&mut self, parts: [Vec<u16>; 3]) -> Result<(), Error> {
        if self.op != Op::ParseStyle {
            return Err(Error::Protocol);
        }
        for part in &parts {
            count(part.len())?;
        }
        let [key, foreground, background] = parts;
        if !foreground.is_empty() || !background.is_empty() {
            self.colors.insert(
                key,
                Color {
                    foreground,
                    background,
                },
            );
        }
        Ok(())
    }
    pub fn converted(&mut self, raw: &[u16]) -> Result<(), Error> {
        if self.op != Op::ConvertCaption {
            return Err(Error::Protocol);
        }
        count(raw.len())?;
        let text = prefix(raw);
        let newline = usize::from(text.len() == raw.len());
        count(
            self.caption
                .len()
                .checked_add(text.len())
                .and_then(|n| n.checked_add(newline))
                .ok_or(Error::CountOverflow)?,
        )?;
        self.caption.extend_from_slice(text);
        if newline != 0 {
            self.caption.push(10);
        }
        self.op = Op::ReadCaption;
        Ok(())
    }
    fn remember(&mut self) {
        self.last_caption = self.caption.clone();
        self.last_header = self.header.clone();
        self.op = Op::ReadHeader;
    }
    fn finish_cue(&mut self) {
        if prefix(&self.last_caption) == prefix(&self.caption)
            && prefix(&self.last_header) == prefix(&self.header)
        {
            self.remember();
        } else {
            self.op = Op::Emit;
        }
    }
    pub fn acknowledge(&mut self, command: u32) -> Result<(), Error> {
        match (command, self.op) {
            (0, Op::ParseStyle) => self.after_style(),
            (1, Op::Emit) => self.remember(),
            _ => return Err(Error::Protocol),
        }
        Ok(())
    }
    pub fn snapshot(&self, group: u32) -> Result<Vec<u8>, Error> {
        let mut bytes = Vec::new();
        match group {
            0 => {
                bytes.extend_from_slice(&(self.op as u32).to_le_bytes());
                bytes.extend_from_slice(&u32::from(self.accepted).to_le_bytes());
                bytes.extend_from_slice(&self.start.to_le_bytes());
                bytes.extend_from_slice(&self.end.to_le_bytes());
                append(
                    &mut bytes,
                    match self.op {
                        Op::ParseStyle => &self.style,
                        Op::TimeStart => &self.tokens[0],
                        Op::TimeEnd => &self.tokens[1],
                        Op::Emit => &self.caption,
                        _ => &self.line,
                    },
                );
            }
            1 => append(&mut bytes, &self.tokens[2]),
            2 => {
                bytes.extend_from_slice(&count(self.colors.len())?.to_le_bytes());
                for (key, color) in &self.colors {
                    let parts = [key, &color.foreground, &color.background];
                    for part in parts {
                        bytes.extend_from_slice(&count(part.len())?.to_le_bytes());
                    }
                    for part in parts {
                        append(&mut bytes, part);
                    }
                }
            }
            _ => return Err(Error::Protocol),
        }
        Ok(bytes)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;
    fn u(s: &str) -> Vec<u16> {
        s.encode_utf16().collect()
    }
    fn read(r: &mut Run, s: &str, eof: bool) {
        let raw = u(s);
        let spaces: Vec<u8> = raw.iter().map(|v| u8::from(*v == 32 || *v == 9)).collect();
        r.read(&Document::default(), &raw, &spaces, eof).unwrap();
    }
    fn cue(r: &mut Run, header: &str, caption: &[u16]) {
        read(r, header, false);
        r.header(2, [u("1:2.3"), u("1:3.4"), vec![]]).unwrap();
        r.time(5, [1, 2, 3, 0]).unwrap();
        r.time(5, [1, 3, 4, 0]).unwrap();
        read(r, "raw", false);
        r.converted(caption).unwrap();
        read(r, "", false);
    }
    #[test]
    fn style_eof_reuse_comments_and_replacement() {
        let mut r = Run::new();
        read(&mut r, "WEBVTT", true);
        assert_eq!(r.op, Op::ReadHeader);
        read(&mut r, "STYLE", false);
        read(&mut r, "/*closed*/::cue {color:red;}", false);
        read(&mut r, " \t", false);
        assert_eq!(r.op, Op::ReadStyle);
        read(&mut r, "1:2.3 --> 1:3.4  ", true);
        assert_eq!(r.op, Op::ParseStyle);
        assert_eq!(r.style, u(" /::cue {color:red;}  \t"));
        r.color([u(".red"), vec![], u("fff")]).unwrap();
        assert!(r.colors[&u(".red")].foreground.is_empty());
        assert_eq!(r.colors[&u(".red")].background, u("fff"));
        let saved = r.snapshot(2).unwrap();
        r.acknowledge(0).unwrap();
        assert_eq!(r.op, Op::ScanHeader);
        assert!(r.line.ends_with(&[32, 32]));
        assert_eq!(r.color([u(".x"), u("red"), vec![]]), Err(Error::Protocol));
        assert_eq!(saved, r.snapshot(2).unwrap());
    }
    #[test]
    fn duplicate_header_and_caption_and_post_cue_style() {
        let mut r = Run::new();
        read(&mut r, "WEBVTT", false);
        cue(&mut r, "header", &u("caption"));
        assert_eq!(r.op, Op::Emit);
        assert_eq!(r.start, 620030000);
        r.acknowledge(1).unwrap();
        cue(&mut r, "header", &u("caption"));
        assert_eq!(r.op, Op::ReadHeader);
        cue(&mut r, "changed", &u("caption"));
        assert_eq!(r.op, Op::Emit);
        r.acknowledge(1).unwrap();
        read(&mut r, "STYLE", false);
        assert_eq!(r.op, Op::ScanHeader);
        r.header(0, Default::default()).unwrap();
        read(&mut r, "discarded", true);
        assert_eq!(r.op, Op::Done);
        assert!(!r.accepted);
    }
    #[test]
    fn raw_nul_concat_and_scan_short_circuit_are_preserved() {
        let mut r = Run::new();
        read(&mut r, "WEBVTT", false);
        read(&mut r, "header", false);
        r.header(2, [u("bad"), u("unused"), vec![]]).unwrap();
        r.time(6, [0; 4]).unwrap();
        assert_eq!(r.op, Op::ReadHeader);
        cue(&mut r, "h", &[65, 0, 0xd800]);
        assert_eq!(r.caption, vec![65]);
        r.acknowledge(1).unwrap();
        cue(&mut r, "h", &[65, 0, 0xffff]);
        assert_eq!(r.op, Op::ReadHeader);
        read(&mut r, "h", false);
        let saved = r.snapshot(0).unwrap();
        assert_eq!(
            r.header(2, [vec![], vec![], u("invalid")]),
            Err(Error::Protocol)
        );
        assert_eq!(r.snapshot(0).unwrap(), saved);
        assert_eq!(
            r.read(&Document::default(), &[65], &[2], false),
            Err(Error::Protocol)
        );
    }
    #[test]
    fn packet_lengths_are_exact_and_units_uninterpreted() {
        let mut p = vec![1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0];
        p.extend_from_slice(&[0, 0xd8, 0, 0, 0xff, 0xff]);
        assert_eq!(
            decode_three(&p).unwrap(),
            [vec![0xd800], vec![], vec![0, 0xffff]]
        );
        assert_eq!(decode_three(&p[..p.len() - 1]), Err(Error::Protocol));
        p.push(0);
        assert_eq!(decode_three(&p), Err(Error::Protocol));
    }
}
