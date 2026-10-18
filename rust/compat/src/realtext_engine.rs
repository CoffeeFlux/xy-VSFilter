// SPDX-License-Identifier: GPL-3.0-or-later
//! Owned RealText helpers. Native services are only per-unit CRT predicates/case,
//! _wtoi, file reads, initial CString comparison and final subtitle conversion.
use crate::ass_document::Error;
use crate::document::Document;
use crate::realtext_lists::StoredTag;
use crate::realtext_session::{Op, Run as Session};
use alloc::vec::Vec;

fn empty_tag() -> StoredTag {
    StoredTag {
        name: Vec::new(),
        flags: 0,
        attributes: Vec::new(),
    }
}
struct Request {
    op: u32,
    unit: u16,
    index: u32,
    text: Vec<u16>,
}
impl Request {
    fn unit(op: u32, unit: u16) -> Self {
        Self {
            op,
            unit,
            index: 0,
            text: Vec::new(),
        }
    }
    fn integer(text: Vec<u16>) -> Self {
        Self {
            op: 4,
            unit: 0,
            index: 0,
            text,
        }
    }
    fn fault(input: &[u16], index: u32) -> Self {
        Self {
            op: 9,
            unit: 0,
            index,
            text: input.to_vec(),
        }
    }
}
enum Event {
    Request(Request),
    Extracted(bool, Vec<u16>, StoredTag),
    Time(i32),
    Rendered(Vec<u16>),
}
#[derive(Clone, Copy)]
enum Stage {
    Tag,
    Text,
    Attributes,
    LowerTag,
    LowerAttribute,
}
struct Extraction {
    input: Vec<u16>,
    tag: StoredTag,
    stage: Stage,
    parser: crate::realtext_tag::Tag,
    text: crate::realtext_text::Text,
    attributes: crate::realtext_attributes::Attributes,
    lower: crate::realtext_lower::Lower,
    lower_input: Vec<u16>,
    lower_response: u16,
    response: bool,
    position: u32,
}
impl Extraction {
    fn new(input: Vec<u16>, text: bool) -> Self {
        let mut tag = empty_tag();
        if text {
            tag.flags = 8;
        }
        Self {
            input,
            tag,
            stage: if text { Stage::Text } else { Stage::Tag },
            parser: Default::default(),
            text: Default::default(),
            attributes: crate::realtext_attributes::Attributes::new(0),
            lower: Default::default(),
            lower_input: Vec::new(),
            lower_response: 0,
            response: false,
            position: 0,
        }
    }
    fn lower(&mut self, input: Vec<u16>, stage: Stage) {
        self.lower_input = input;
        self.lower = Default::default();
        self.lower_response = 0;
        self.stage = stage;
    }
    fn finish(&mut self, ok: bool) -> Event {
        Event::Extracted(
            ok,
            core::mem::take(&mut self.input),
            core::mem::replace(&mut self.tag, empty_tag()),
        )
    }
    fn consume(&mut self, n: usize) -> bool {
        if n > self.input.len() {
            return false;
        }
        self.input.drain(..n);
        true
    }
    fn poll(&mut self) -> Event {
        loop {
            match self.stage {
                Stage::Tag => {
                    let action = self
                        .parser
                        .next(&self.input, true, self.response, self.position);
                    self.position = self.parser.position;
                    for (mask, flag) in [(1, 4), (2, 1), (4, 2)] {
                        if self.parser.patch & mask != 0 {
                            self.tag.flags = (self.tag.flags & !flag)
                                | if self.parser.patch & (mask << 8) != 0 {
                                    flag
                                } else {
                                    0
                                };
                        }
                    }
                    match action {
                        0 | 1 => return self.finish(action == 1),
                        2 => return Event::Request(Request::unit(2, self.parser.character)),
                        3 => self.lower(self.tag.name.clone(), Stage::LowerTag),
                        4 => {
                            self.attributes =
                                crate::realtext_attributes::Attributes::new(self.position);
                            self.response = false;
                            self.stage = Stage::Attributes;
                        }
                        5 => self.tag.name.extend_from_slice(&self.parser.text),
                        6 => self.tag.name = self.parser.text.clone(),
                        7 => {
                            if !self.consume(self.parser.consume) {
                                return Event::Request(Request::fault(&self.input, self.position));
                            }
                        }
                        8 => return Event::Request(Request::fault(&self.input, self.position)),
                        _ => unreachable!(),
                    }
                }
                Stage::Text => match self.text.next(&self.input, true, self.response) {
                    0 => return self.finish(false),
                    1 => return self.finish(true),
                    2 => return Event::Request(Request::unit(2, self.text.character)),
                    3 => self.tag.name.clear(),
                    4 => self.tag.name.extend_from_slice(&self.text.text),
                    5 => {
                        if !self.consume(self.text.position as usize) {
                            return Event::Request(Request::fault(&self.input, self.text.position));
                        }
                    }
                    _ => unreachable!(),
                },
                Stage::Attributes => match self.attributes.next(&self.input, true, self.response) {
                    1 => return Event::Request(Request::unit(2, self.attributes.character)),
                    2 => self.lower(self.attributes.name.clone(), Stage::LowerAttribute),
                    action @ (3 | 4) => {
                        self.position = self.attributes.position;
                        self.response = action == 3;
                        self.stage = Stage::Tag;
                    }
                    _ => unreachable!(),
                },
                Stage::LowerTag | Stage::LowerAttribute => {
                    if self.lower.next(&self.lower_input, self.lower_response) {
                        return Event::Request(Request::unit(3, self.lower.character));
                    }
                    if matches!(self.stage, Stage::LowerTag) {
                        self.tag.name = self.lower.text.clone();
                        self.stage = Stage::Tag;
                    } else {
                        let key = self.lower.text.clone();
                        let value = self.attributes.value.clone();
                        match self.tag.attributes.binary_search_by(|(k, _)| k.cmp(&key)) {
                            Ok(i) => self.tag.attributes[i].1 = value,
                            Err(i) => self.tag.attributes.insert(i, (key, value)),
                        }
                        self.stage = Stage::Attributes;
                    }
                }
            }
        }
    }
}
struct Number {
    input: Vec<u16>,
    parser: crate::realtext_timecode::Timecode,
    multiplier: i32,
    value: i32,
}
impl Number {
    fn new(input: Vec<u16>) -> Self {
        let multiplier = if input.iter().any(|&u| u == 46 || u == 58) {
            1
        } else {
            1000
        };
        Self {
            parser: crate::realtext_timecode::Timecode::new(input.len()),
            input,
            multiplier,
            value: 0,
        }
    }
    fn poll(&mut self) -> Event {
        match self.parser.next(&self.input) {
            0 => Event::Time(self.value),
            1 => {
                let mut part = self.parser.part.clone();
                if !self.parser.last && self.multiplier == 1 {
                    while part.len() < 3 {
                        part.push(48);
                    }
                }
                Event::Request(Request::integer(part))
            }
            2 => Event::Request(Request::fault(&self.input, self.parser.missing as u32)),
            _ => unreachable!(),
        }
    }
    fn supply(&mut self, value: i32) {
        self.value = self.value.wrapping_add(self.multiplier.wrapping_mul(value));
        if !self.parser.last {
            self.multiplier = if self.multiplier == 1 {
                1000
            } else {
                self.multiplier.wrapping_mul(60)
            };
        }
    }
}
#[derive(Clone, Copy)]
enum FontRequest {
    Digit,
    Integer,
}
struct Render {
    tags: Vec<StoredTag>,
    renderer: crate::realtext::Tags,
    index: usize,
    attribute: Option<usize>,
    pending: Option<FontRequest>,
}
impl Render {
    fn new(tags: Vec<StoredTag>) -> Self {
        // OpenRealText constructs CRealTextParser with these private defaults.
        Self {
            tags,
            renderer: crate::realtext::Tags::new(0, 14, 25),
            index: 0,
            attribute: None,
            pending: None,
        }
    }
    fn commit(&mut self, numeric: bool, value: i32) {
        let i = self.attribute.unwrap();
        let (key, text) = &self.tags[self.index].attributes[i];
        self.renderer.attribute(key, text, numeric, value);
        self.attribute = Some(i + 1);
        self.pending = None;
    }
    fn poll(&mut self) -> Event {
        loop {
            if self.index == self.tags.len() {
                return Event::Rendered(self.renderer.result().to_vec());
            }
            let tag = &self.tags[self.index];
            if let Some(i) = self.attribute {
                if i == tag.attributes.len() {
                    self.renderer.end_font();
                    self.attribute = None;
                    self.index += 1;
                    continue;
                }
                let (key, value) = &tag.attributes[i];
                if matches!(self.pending, Some(FontRequest::Integer)) {
                    return Event::Request(Request::integer(value.clone()));
                }
                let kind = self.renderer.attribute_kind(key);
                if kind == 2 && !value.is_empty() {
                    self.pending = Some(FontRequest::Digit);
                    return Event::Request(Request::unit(5, value[0]));
                }
                self.commit(false, 0);
            } else {
                let flags = (tag.flags & 3) | if tag.flags & 8 != 0 { 4 } else { 0 };
                if self.renderer.begin(&tag.name, flags) {
                    self.attribute = Some(0);
                } else {
                    self.index += 1;
                }
            }
        }
    }
    fn supply(&mut self, value: i32) {
        match self.pending.unwrap() {
            FontRequest::Digit => {
                if value != 0 {
                    self.pending = Some(FontRequest::Integer);
                } else {
                    self.commit(false, 0);
                }
            }
            FontRequest::Integer => self.commit(true, value),
        }
    }
}
enum Helper {
    Extract(Extraction),
    Number(Number),
    Render(Render),
}
impl Helper {
    fn poll(&mut self) -> Event {
        match self {
            Self::Extract(x) => x.poll(),
            Self::Number(n) => n.poll(),
            Self::Render(r) => r.poll(),
        }
    }
    fn supply(&mut self, op: u32, value: i32) {
        match self {
            Self::Extract(x) => {
                if op == 3 {
                    x.lower_response = value as u16;
                } else {
                    x.response = value != 0;
                }
            }
            Self::Number(n) => n.supply(value),
            Self::Render(r) => r.supply(value),
        }
    }
}
pub struct Run {
    session: Session,
    helper: Option<Helper>,
    request: Option<Request>,
}
impl Default for Run {
    fn default() -> Self {
        Self::new()
    }
}
impl Run {
    pub fn new() -> Self {
        Self {
            session: Session::new(4000),
            helper: None,
            request: None,
        }
    }
    fn op(&self) -> u32 {
        if let Some(r) = &self.request {
            return r.op;
        }
        match self.session.operation() {
            Op::Read => 0,
            Op::CheckStart => 1,
            Op::Emit => 6,
            Op::Done => 7,
            Op::Failed => 8,
            _ => unreachable!(),
        }
    }
    fn pump(&mut self, doc: &Document) -> Result<(), Error> {
        loop {
            if self.helper.is_none() {
                self.helper = match self.session.operation() {
                    Op::ExtractTag | Op::ExtractText => Some(Helper::Extract(Extraction::new(
                        self.session.input().to_vec(),
                        self.session.operation() == Op::ExtractText,
                    ))),
                    Op::TimeStart | Op::TimeEnd => {
                        Some(Helper::Number(Number::new(self.session.token().to_vec())))
                    }
                    Op::Render => Some(Helper::Render(Render::new(self.session.tags().to_vec()))),
                    _ => return Ok(()),
                };
            }
            match self.helper.as_mut().unwrap().poll() {
                Event::Request(r) => {
                    self.request = Some(r);
                    return Ok(());
                }
                Event::Extracted(response, input, tag) => {
                    self.session.extracted(doc, response, input, tag)?
                }
                Event::Time(value) => self.session.time(doc, value)?,
                Event::Rendered(text) => self.session.rendered(doc, &text)?,
            }
            self.helper = None;
        }
    }
    pub fn read(
        &mut self,
        doc: &Document,
        raw: &[u16],
        spaces: &[u8],
        eof: bool,
    ) -> Result<(), Error> {
        if self.op() != 0 {
            return Err(Error::Protocol);
        }
        self.session.read(doc, raw, spaces, eof)?;
        self.pump(doc)
    }
    /// Commands equal the requested operation. Bool replies must be 0/1;
    /// lowercase replies are raw u16; integer/CompareNoCase accept all i32.
    pub fn step(&mut self, doc: &Document, command: u32, value: i32) -> Result<(), Error> {
        if command != self.op()
            || !(1..=6).contains(&command)
            || (matches!(command, 2 | 5) && !matches!(value, 0 | 1))
            || (command == 3 && !(0..=65535).contains(&value))
            || (command == 6 && value != 0)
        {
            return Err(Error::Protocol);
        }
        match command {
            1 => self.session.check_start(value)?,
            6 => self.session.acknowledge(doc)?,
            _ => {
                self.helper.as_mut().unwrap().supply(command, value);
                self.request = None;
            }
        }
        self.pump(doc)
    }
    /// Group0: op/accepted:u32,start/end:i64,unit/index:u32,raw payload.
    /// Other groups retain the session's immutable tags/map/window snapshots.
    pub fn snapshot(&self, group: u32) -> Result<Vec<u8>, Error> {
        if group != 0 {
            return self.session.snapshot(group);
        }
        let mut bytes = self.session.snapshot(0)?;
        bytes[..4].copy_from_slice(&self.op().to_le_bytes());
        let payload = bytes.split_off(24);
        let (unit, index) = self
            .request
            .as_ref()
            .map_or((0, 0), |r| (u32::from(r.unit), r.index));
        bytes.extend_from_slice(&unit.to_le_bytes());
        bytes.extend_from_slice(&index.to_le_bytes());
        if let Some(r) = &self.request {
            bytes.extend(r.text.iter().flat_map(|u| u.to_le_bytes()));
        } else {
            bytes.extend(payload);
        }
        Ok(bytes)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::{string::String, vec};
    fn u(s: &str) -> Vec<u16> {
        s.encode_utf16().collect()
    }
    fn parse(text: &str) -> (Run, Vec<(i64, i64, Vec<u16>)>) {
        let mut run = Run::new();
        let doc = Document::default();
        let raw = u(text);
        run.read(&doc, &raw, &vec![0; raw.len()], false).unwrap();
        run.step(&doc, 1, 1).unwrap();
        run.read(&doc, &[], &[], true).unwrap();
        let mut rows = Vec::new();
        for _ in 0..10000 {
            let s = run.snapshot(0).unwrap();
            let op = u32::from_le_bytes(s[..4].try_into().unwrap());
            let unit = u32::from_le_bytes(s[24..28].try_into().unwrap());
            let text: Vec<u16> = s[32..]
                .chunks_exact(2)
                .map(|b| u16::from_le_bytes([b[0], b[1]]))
                .collect();
            let value = match op {
                2 => i32::from([9, 10, 13, 32].contains(&unit)),
                3 => {
                    if (65..=90).contains(&unit) {
                        unit as i32 + 32
                    } else {
                        unit as i32
                    }
                }
                4 => String::from_utf16(&text)
                    .unwrap()
                    .parse::<i64>()
                    .unwrap_or(0) as i32,
                5 => i32::from((48..=57).contains(&unit)),
                6 => {
                    rows.push((
                        i64::from_le_bytes(s[8..16].try_into().unwrap()),
                        i64::from_le_bytes(s[16..24].try_into().unwrap()),
                        text,
                    ));
                    0
                }
                7 | 8 => return (run, rows),
                _ => panic!("unexpected request {op}"),
            };
            run.step(&doc, op, value).unwrap();
        }
        panic!("test exceeded bounded service replies")
    }
    #[test]
    fn duplicate_folded_attributes_filter_sizes_and_restore_open_tags() {
        let (run,rows)=parse("<window><time begin='1' end='5'/><font SIZE='13' SiZe='20' color='red'>A<clear/>B</font></window>");
        assert_eq!(run.op(), 7);
        assert_eq!(rows.len(), 1);
        assert_eq!(rows[0].2, u("<font color=\"red\" size=\"20\">B</font>"));
        let (_, rows) = parse("<window><time begin='1' end='2'/><font size='13'>A</font></window>");
        assert_eq!(rows[0].2, u("<font>A</font>"));
    }
    #[test]
    fn time_fields_are_padded_on_right_and_fold_with_wrapping_arithmetic() {
        let (_, rows) = parse("<window><time begin='1:2.3' end='1:3.04'/>A</window>");
        assert_eq!((rows[0].0, rows[0].1), (623000000, 630400000));
        let mut n = Number::new(u("1:2:3:4:5:6:7"));
        let mut expected = 0i32;
        let mut multiplier = 1i32;
        for _ in 0..7 {
            assert!(matches!(n.poll(), Event::Request(_)));
            n.supply(i32::MAX);
            expected = expected.wrapping_add(multiplier.wrapping_mul(i32::MAX));
            multiplier = if multiplier == 1 {
                1000
            } else {
                multiplier.wrapping_mul(60)
            };
        }
        assert!(matches!(n.poll(),Event::Time(v) if v==expected));
    }
    #[test]
    fn malformed_trailing_text_and_final_comments_preserve_failure() {
        let (run, rows) = parse("<window><time begin='1'/>text");
        assert_eq!(run.op(), 8);
        assert!(rows.is_empty());
        let (run, rows) = parse("<window><time begin='1'/>text<!comment>");
        assert_eq!(run.op(), 8);
        assert!(rows.is_empty());
        let (_, rows) = parse("<window><time begin='1'/>text<!comment></window>");
        assert_eq!(rows[0].2, u("text"));
    }
    #[test]
    fn scalar_refusals_leave_pending_request_and_owned_input_unchanged() {
        let doc = Document::default();
        let mut r = Run::new();
        let raw = u("<window>");
        r.read(&doc, &raw, &[0; 8], false).unwrap();
        r.step(&doc, 1, 1).unwrap();
        r.read(&doc, &[], &[], true).unwrap();
        let saved = r.snapshot(0).unwrap();
        assert_eq!(r.op(), 2);
        assert_eq!(r.step(&doc, 2, 2), Err(Error::Protocol));
        assert_eq!(r.step(&doc, 3, 0), Err(Error::Protocol));
        assert_eq!(r.read(&doc, &[], &[], true), Err(Error::Protocol));
        assert_eq!(r.snapshot(0).unwrap(), saved);
    }
}
