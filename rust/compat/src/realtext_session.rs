// SPDX-License-Identifier: GPL-3.0-or-later
//! Cleanup OpenRealText/ParseRealText ownership. The realtext_engine module
//! supplies extraction, time folding and tag rendering using safe Rust helpers.
//! Input, tags, attributes, timeline and interval-map values stay in safe Rust.
use crate::ass_document::Error;
use crate::document::Document;
use crate::realtext_lists::{Lists, StoredTag};
use alloc::collections::BTreeMap;
use alloc::vec::Vec;
use core::ops::Bound::{Excluded, Unbounded};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Op {
    Read = 0,
    CheckStart = 1,
    ExtractTag = 2,
    ExtractText = 3,
    TimeStart = 4,
    TimeEnd = 5,
    Render = 6,
    Emit = 7,
    Done = 8,
    Failed = 9,
}
pub struct Run {
    op: Op,
    input: Vec<u16>,
    line: Vec<u16>,
    token: Vec<u16>,
    rendered: Vec<u16>,
    flow: crate::realtext_document::Document,
    timeline: crate::realtext_timeline::Timeline,
    lists: Lists,
    tag: StoredTag,
    window: StoredTag,
    center: bool,
    response: bool,
    start: i32,
    end: i32,
    duration: i32,
    entries: BTreeMap<(i32, i32), Vec<u16>>,
    emitting: Option<(i32, i32)>,
    accepted: bool,
}
fn empty_tag() -> StoredTag {
    StoredTag {
        name: Vec::new(),
        flags: 0,
        attributes: Vec::new(),
    }
}
fn prefix(raw: &[u16]) -> &[u16] {
    &raw[..raw.iter().position(|&u| u == 0).unwrap_or(raw.len())]
}
fn count(n: usize) -> Result<u32, Error> {
    if n > i32::MAX as usize {
        Err(Error::CountOverflow)
    } else {
        Ok(n as u32)
    }
}
fn units(bytes: &mut Vec<u8>, raw: &[u16]) {
    bytes.extend(raw.iter().flat_map(|u| u.to_le_bytes()));
}
fn word(bytes: &mut Vec<u8>, n: usize) -> Result<(), Error> {
    bytes.extend_from_slice(&count(n)?.to_le_bytes());
    Ok(())
}
fn encode_tag(bytes: &mut Vec<u8>, tag: &StoredTag) -> Result<(), Error> {
    bytes.extend_from_slice(&tag.flags.to_le_bytes());
    word(bytes, tag.name.len())?;
    word(bytes, tag.attributes.len())?;
    units(bytes, &tag.name);
    for (key, value) in &tag.attributes {
        word(bytes, key.len())?;
        word(bytes, value.len())?;
        units(bytes, key);
        units(bytes, value);
    }
    Ok(())
}
struct Reader<'a> {
    bytes: &'a [u8],
    offset: usize,
}
impl Reader<'_> {
    fn word(&mut self) -> Result<u32, Error> {
        let end = self.offset.checked_add(4).ok_or(Error::CountOverflow)?;
        let p = self.bytes.get(self.offset..end).ok_or(Error::Protocol)?;
        self.offset = end;
        Ok(u32::from_le_bytes(p.try_into().unwrap()))
    }
    fn count(&mut self) -> Result<usize, Error> {
        let n = self.word()? as usize;
        count(n)?;
        Ok(n)
    }
    fn units(&mut self, n: usize) -> Result<Vec<u16>, Error> {
        let end = self
            .offset
            .checked_add(n.checked_mul(2).ok_or(Error::CountOverflow)?)
            .ok_or(Error::CountOverflow)?;
        let raw = self.bytes.get(self.offset..end).ok_or(Error::Protocol)?;
        self.offset = end;
        Ok(raw
            .chunks_exact(2)
            .map(|b| u16::from_le_bytes([b[0], b[1]]))
            .collect())
    }
    fn tag(&mut self) -> Result<StoredTag, Error> {
        let flags = self.word()?;
        if flags > 15 {
            return Err(Error::Protocol);
        }
        let name_len = self.count()?;
        let attributes = self.count()?;
        let name = self.units(name_len)?;
        let mut tag = StoredTag {
            name,
            flags,
            attributes: Vec::new(),
        };
        for _ in 0..attributes {
            let k = self.count()?;
            let v = self.count()?;
            let key = self.units(k)?;
            let value = self.units(v)?;
            if tag
                .attributes
                .last()
                .is_some_and(|(previous, _)| previous >= &key)
            {
                return Err(Error::Protocol);
            }
            tag.attributes.push((key, value));
        }
        Ok(tag)
    }
}
/// Remaining length:u32 and units; tag flags/name-length/attribute-count:u32,
/// name units, then key/value lengths and units in strictly sorted map order.
pub fn decode_extraction(bytes: &[u8]) -> Result<(Vec<u16>, StoredTag), Error> {
    let mut r = Reader { bytes, offset: 0 };
    let len = r.count()?;
    let input = r.units(len)?;
    let tag = r.tag()?;
    if r.offset != bytes.len() {
        return Err(Error::Protocol);
    }
    Ok((input, tag))
}
impl Run {
    pub(crate) fn operation(&self) -> Op {
        self.op
    }
    pub(crate) fn input(&self) -> &[u16] {
        &self.input
    }
    pub(crate) fn token(&self) -> &[u16] {
        &self.token
    }
    pub(crate) fn tags(&self) -> &[StoredTag] {
        &self.lists.tags
    }
    pub fn new(duration: i32) -> Self {
        Self {
            op: Op::Read,
            input: Vec::new(),
            line: Vec::new(),
            token: Vec::new(),
            rendered: Vec::new(),
            flow: Default::default(),
            timeline: Default::default(),
            lists: Default::default(),
            tag: empty_tag(),
            window: empty_tag(),
            center: false,
            response: false,
            start: 0,
            end: 0,
            duration,
            entries: BTreeMap::new(),
            emitting: None,
            accepted: false,
        }
    }
    pub fn read(
        &mut self,
        doc: &Document,
        raw: &[u16],
        spaces: &[u8],
        eof: bool,
    ) -> Result<(), Error> {
        if self.op != Op::Read {
            return Err(Error::Protocol);
        }
        let line = crate::ass_input::trim(raw, spaces).ok_or(Error::Protocol)?;
        count(raw.len())?;
        if eof {
            self.drive(doc);
            return Ok(());
        }
        if line.is_empty() {
            return Ok(());
        }
        count(
            self.input
                .len()
                .checked_add(prefix(line).len())
                .and_then(|n| n.checked_add(1))
                .ok_or(Error::CountOverflow)?,
        )?;
        self.line = line.to_vec();
        if self.input.is_empty() {
            self.op = Op::CheckStart;
        } else {
            self.append_line();
        }
        Ok(())
    }
    fn append_line(&mut self) {
        self.input.push(10);
        self.input.extend_from_slice(prefix(&self.line));
        self.line.clear();
        self.op = Op::Read;
    }
    pub fn check_start(&mut self, comparison: i32) -> Result<(), Error> {
        if self.op != Op::CheckStart {
            return Err(Error::Protocol);
        }
        if comparison < 0 {
            self.op = Op::Failed;
        } else {
            self.append_line();
        }
        Ok(())
    }
    pub fn extracted(
        &mut self,
        doc: &Document,
        response: bool,
        input: Vec<u16>,
        tag: StoredTag,
    ) -> Result<(), Error> {
        if !matches!(self.op, Op::ExtractTag | Op::ExtractText) {
            return Err(Error::Protocol);
        }
        count(input.len())?;
        count(tag.name.len())?;
        count(tag.attributes.len())?;
        if tag.flags > 15 || tag.attributes.windows(2).any(|p| p[0].0 >= p[1].0) {
            return Err(Error::Protocol);
        }
        for (k, v) in &tag.attributes {
            count(k.len())?;
            count(v.len())?;
        }
        self.input = input;
        self.tag = tag;
        self.response = response;
        self.drive(doc);
        Ok(())
    }
    pub fn time(&mut self, doc: &Document, value: i32) -> Result<(), Error> {
        match self.op {
            Op::TimeStart => self.start = value,
            Op::TimeEnd => self.end = value,
            _ => return Err(Error::Protocol),
        }
        self.drive(doc);
        Ok(())
    }
    pub fn rendered(&mut self, doc: &Document, text: &[u16]) -> Result<(), Error> {
        if self.op != Op::Render {
            return Err(Error::Protocol);
        }
        count(text.len())?;
        self.rendered = text.to_vec();
        self.drive(doc);
        Ok(())
    }
    pub fn acknowledge(&mut self, doc: &Document) -> Result<(), Error> {
        if self.op != Op::Emit {
            return Err(Error::Protocol);
        }
        self.next_entry(doc);
        Ok(())
    }
    fn set_tag(&mut self) {
        self.lists.current = Some(self.tag.clone());
    }
    fn time_token(&mut self, name: &str) {
        let key: Vec<u16> = name.encode_utf16().collect();
        let index = match self.tag.attributes.binary_search_by(|(k, _)| k.cmp(&key)) {
            Ok(i) => i,
            Err(i) => {
                self.tag.attributes.insert(i, (key, Vec::new()));
                i
            }
        };
        self.token = self.tag.attributes[index].1.clone();
    }
    fn next_entry(&mut self, doc: &Document) {
        self.emitting = match self.emitting {
            None => self.entries.keys().next().copied(),
            Some(key) => self
                .entries
                .range((Excluded(key), Unbounded))
                .next()
                .map(|(key, _)| *key),
        };
        if self.emitting.is_some() {
            self.op = Op::Emit;
        } else {
            self.op = Op::Done;
            self.accepted = !doc.entries().is_empty();
        }
    }
    fn drive(&mut self, doc: &Document) {
        loop {
            let action = self.flow.next(
                self.input.is_empty(),
                self.input.first().copied().unwrap_or(0),
                self.response,
                &self.tag.name,
                self.tag.flags,
            );
            match action {
                0 => {
                    self.op = Op::Failed;
                    return;
                }
                1 => {
                    self.next_entry(doc);
                    return;
                }
                2 => {
                    self.op = Op::ExtractTag;
                    return;
                }
                3 => {
                    self.op = Op::ExtractText;
                    return;
                }
                4 => {
                    self.set_tag();
                    self.lists.append();
                }
                5 => {
                    self.time_token("begin");
                    self.op = Op::TimeStart;
                    return;
                }
                6 => {
                    self.time_token("end");
                    self.op = Op::TimeEnd;
                    return;
                }
                7 | 17 => {
                    self.op = Op::Render;
                    return;
                }
                8 | 18 => {
                    let interval = self.timeline.select(
                        if action == 18 { None } else { Some(self.start) },
                        self.entries.is_empty(),
                    );
                    if interval != 0 && !self.rendered.is_empty() {
                        let first = self.timeline.first;
                        let last = if interval == 2 {
                            first.wrapping_add(self.duration)
                        } else {
                            self.timeline.second
                        };
                        self.entries.insert((first, last), self.rendered.clone());
                    }
                }
                9 => self.timeline.advance(self.start, self.end),
                10 => self.set_tag(),
                11 => self.lists.open(),
                12 => self.lists.close(),
                13 => self.lists.append(),
                14 => self.lists.clear(),
                15 => self.window = self.tag.clone(),
                16 => self.center = true,
                _ => unreachable!(),
            }
        }
    }
    pub fn snapshot(&self, group: u32) -> Result<Vec<u8>, Error> {
        let mut bytes = Vec::new();
        match group {
            0 => {
                bytes.extend_from_slice(&(self.op as u32).to_le_bytes());
                bytes.extend_from_slice(&u32::from(self.accepted).to_le_bytes());
                let (start, end) = self.emitting.unwrap_or((0, 0));
                bytes.extend_from_slice(&(i64::from(start) * 10000).to_le_bytes());
                bytes.extend_from_slice(&(i64::from(end) * 10000).to_le_bytes());
                let payload = match self.op {
                    Op::CheckStart => &self.line,
                    Op::ExtractTag | Op::ExtractText => &self.input,
                    Op::TimeStart | Op::TimeEnd => &self.token,
                    Op::Emit => &self.entries[&self.emitting.unwrap()],
                    _ => &[][..],
                };
                units(&mut bytes, payload);
            }
            1 => {
                word(&mut bytes, self.lists.tags.len())?;
                for tag in &self.lists.tags {
                    encode_tag(&mut bytes, tag)?;
                }
            }
            2 => {
                word(&mut bytes, self.entries.len())?;
                for ((start, end), text) in &self.entries {
                    bytes.extend_from_slice(&start.to_le_bytes());
                    bytes.extend_from_slice(&end.to_le_bytes());
                    word(&mut bytes, text.len())?;
                    units(&mut bytes, text);
                }
            }
            3 => {
                bytes.extend_from_slice(&u32::from(self.center).to_le_bytes());
                encode_tag(&mut bytes, &self.window)?;
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
    fn tag(name: &str, flags: u32) -> StoredTag {
        StoredTag {
            name: u(name),
            flags,
            attributes: vec![],
        }
    }
    fn parse() -> Run {
        let mut r = Run::new(4000);
        r.input = u("<window>");
        r.drive(&Document::default());
        r
    }
    fn extracted(r: &mut Run, name: &str, flags: u32) {
        r.extracted(&Document::default(), true, u("<next>"), tag(name, flags))
            .unwrap();
    }
    fn time(r: &mut Run, start: i32, end: i32, text: &str) {
        extracted(r, "time", 3);
        assert_eq!(r.op, Op::TimeStart);
        assert!(r.token.is_empty());
        r.time(&Document::default(), start).unwrap();
        assert_eq!(r.op, Op::TimeEnd);
        r.time(&Document::default(), end).unwrap();
        r.rendered(&Document::default(), &u(text)).unwrap();
    }
    #[test]
    fn accumulated_sorted_map_replaces_exact_intervals_and_skips_empty_render() {
        let mut r = parse();
        time(&mut r, 2000, 3000, "");
        time(&mut r, 1000, 2000, "later");
        time(&mut r, 2000, 3000, "earlier");
        time(&mut r, 4000, 5000, "replacement");
        assert_eq!(r.entries[&(2000, 3000)], u("replacement"));
        assert_eq!(r.entries[&(1000, 2000)], u("earlier"));
        r.extracted(&Document::default(), true, vec![], tag("ignored", 0))
            .unwrap();
        assert_eq!(r.op, Op::Render);
        r.rendered(&Document::default(), &[]).unwrap();
        assert_eq!(r.op, Op::Emit);
        assert_eq!(r.emitting, Some((1000, 2000)));
        r.acknowledge(&Document::default()).unwrap();
        assert_eq!(r.emitting, Some((2000, 3000)));
        r.acknowledge(&Document::default()).unwrap();
        assert_eq!(r.op, Op::Done);
    }
    #[test]
    fn missing_end_wrap_and_failure_do_not_emit_partial_document() {
        let mut r = parse();
        time(&mut r, i32::MAX, 0, "");
        time(&mut r, 0, 0, "wrapped");
        assert_eq!(
            r.entries[&(i32::MAX, i32::MAX.wrapping_add(4000))],
            u("wrapped")
        );
        r.extracted(&Document::default(), false, vec![], empty_tag())
            .unwrap();
        assert_eq!(r.op, Op::Failed);
        assert!(r.emitting.is_none());
    }
    #[test]
    fn formatting_reopens_after_clear_and_metadata_is_owned() {
        let mut r = parse();
        extracted(&mut r, "window", 1);
        extracted(&mut r, "center", 1);
        extracted(&mut r, "b", 1);
        extracted(&mut r, "i", 1);
        extracted(&mut r, "b", 1);
        extracted(&mut r, "b", 2);
        extracted(&mut r, "clear", 3);
        assert_eq!(r.lists.tags.len(), 2);
        assert_eq!(r.lists.tags[0].name, u("b"));
        assert_eq!(r.lists.tags[1].name, u("i"));
        assert!(r.center);
        assert_eq!(r.window.name, u("window"));
        let snapshot = r.snapshot(1).unwrap();
        extracted(&mut r, "i", 2);
        assert_ne!(snapshot, r.snapshot(1).unwrap());
    }
    #[test]
    fn input_trimming_eof_fragments_raw_units_and_protocol_preflight() {
        let mut r = Run::new(4000);
        let doc = Document::default();
        r.read(
            &doc,
            &[32, 60, 122, 62, 0, 0xd800, 32],
            &[1, 0, 0, 0, 0, 0, 1],
            false,
        )
        .unwrap();
        assert_eq!(r.op, Op::CheckStart);
        let saved = r.snapshot(0).unwrap();
        assert_eq!(r.time(&doc, 1), Err(Error::Protocol));
        assert_eq!(saved, r.snapshot(0).unwrap());
        r.check_start(1).unwrap();
        assert_eq!(r.input, vec![10, 60, 122, 62]);
        r.read(&doc, &u("discard"), &[0; 7], true).unwrap();
        assert_eq!(r.op, Op::ExtractText);
        assert_eq!(r.input, vec![10, 60, 122, 62]);
    }
    #[test]
    fn extraction_wire_preserves_nuls_and_rejects_duplicate_or_unsorted_keys() {
        let mut bytes = vec![];
        word(&mut bytes, 2).unwrap();
        units(&mut bytes, &[0, 0xd800]);
        let mut t = tag("x", 1);
        t.attributes = vec![(u("a"), vec![0, 0xffff]), (u("b"), u("c"))];
        encode_tag(&mut bytes, &t).unwrap();
        let (input, result) = decode_extraction(&bytes).unwrap();
        assert_eq!(input, vec![0, 0xd800]);
        assert_eq!(result.attributes, t.attributes);
        assert!(decode_extraction(&bytes[..bytes.len() - 1]).is_err());
        bytes.push(0);
        assert!(decode_extraction(&bytes).is_err());
        t.attributes.reverse();
        let mut bytes = vec![0; 4];
        encode_tag(&mut bytes, &t).unwrap();
        assert!(decode_extraction(&bytes).is_err());
    }
}
