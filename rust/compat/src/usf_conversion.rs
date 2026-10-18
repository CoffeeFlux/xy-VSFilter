// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned CUSFSubtitles::ConvertToSTS orchestration at cleanup 67da07cd.
//! XML loading/parsing and scalar Windows conversions remain native services.
use crate::ass_document::Error;
use crate::document::{Document, IdentityField, InterpretationField, PreparedEntry, ScriptField};
use crate::document_style::StyleValue;
use alloc::{format, vec::Vec};

type Units = Vec<u16>;
#[derive(Default)]
struct RawStyle {
    name: Units,
    // face,size,color[4],weight,italic,underline,alpha,outline,shadow,wrap
    font: [Units; 13],
    // alignment,relative-to,horizontal-margin,vertical-margin,rotate[3]
    pal: [Units; 7],
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;
    fn put(b: &mut Vec<u8>, s: &[u16]) {
        b.extend_from_slice(&(s.len() as u32).to_le_bytes());
        append_text(b, s);
    }
    fn packet(styles: &[RawStyle], texts: &[RawText]) -> Vec<u8> {
        let mut b = Vec::new();
        put(&mut b, &ascii("English"));
        b.extend_from_slice(&(styles.len() as u32).to_le_bytes());
        b.extend_from_slice(&(texts.len() as u32).to_le_bytes());
        for s in styles {
            put(&mut b, &s.name);
            for v in s.font.iter().chain(s.pal.iter()) {
                put(&mut b, v);
            }
        }
        for t in texts {
            b.extend_from_slice(&t.start.to_le_bytes());
            b.extend_from_slice(&t.stop.to_le_bytes());
            for v in [&t._effect, &t.style, &t.text]
                .into_iter()
                .chain(t.pal.iter())
            {
                put(&mut b, v);
            }
        }
        b
    }
    fn native_integer(s: &[u16]) -> i32 {
        alloc::string::String::from_utf16(prefix(s))
            .unwrap()
            .trim()
            .parse()
            .unwrap_or(0)
    }
    fn reply(r: &Request) -> i32 {
        match r.op {
            1 => i32::from(
                prefix(&r.a)
                    .iter()
                    .map(|&u| if (65..=90).contains(&u) { u + 32 } else { u })
                    .ne(prefix(&r.b).iter().copied()),
            ),
            2 => native_integer(&r.a),
            3 => 0x80112233u32 as i32,
            4 => r.arg / 10,
            5 => 7,
            6 | 7 => 0,
            _ => panic!("unexpected native request"),
        }
    }
    #[test]
    fn conversion_preserves_alpha_cast_prefix_order_live_dimensions_and_raw_times() {
        let mut s = RawStyle {
            name: ascii("Named"),
            ..Default::default()
        };
        s.font[6] = ascii("0");
        s.font[9] = ascii("300");
        s.font[12] = ascii("no");
        s.pal[2] = ascii("10%");
        let mut t = RawText {
            start: 1,
            stop: 5,
            style: ascii("Named"),
            text: vec![65, 0, 0xd800],
            _effect: ascii("ignored"),
            ..Default::default()
        };
        t.pal[0] = ascii("TopLeft");
        t.pal[2] = ascii("10%");
        for (i, v) in [10, -2, 3].into_iter().enumerate() {
            t.pal[i + 4] = ascii(&format!("{v}"));
        }
        let mut bytes = packet(&[s], &[t]);
        let mut run = Run::decode(&bytes).unwrap();
        bytes.fill(0xa5);
        let mut doc = Document::default();
        run.begin(&mut doc).unwrap();
        assert_eq!(doc.script_field(ScriptField::Width), Some(640));
        let mut seen_style = false;
        let mut seen_entry = false;
        for _ in 0..100 {
            let Some(request) = run.request.as_ref() else {
                break;
            };
            let op = request.op;
            let value = reply(request);
            if op == 6 {
                let s = crate::document_style::decode(&run.snapshot(1).unwrap()).unwrap();
                assert_eq!(s.font_weight, 400);
                assert_eq!(s.alpha, [183; 4]);
                assert_eq!(s.colors, [0x112233; 4]);
                assert_eq!(s.margins, [64, 20, 64, 20]);
                seen_style = true;
                doc.replace_script_field(ScriptField::Width, 1280);
            }
            if op == 7 {
                let e = crate::document_wire::decode_entry(&run.snapshot(2).unwrap()).unwrap();
                let mut expected = ascii("{\\q2}{\\ry3}{\\rx-2}{\\rz10}{\\an7}");
                expected.extend([65, 0, 0xd800]);
                assert_eq!(e.text, expected);
                assert_eq!(e.margins, [128, 0, 128, 0]);
                assert_eq!((e.start, e.end), (1, 5));
                assert!(e.effect.is_empty());
                seen_entry = true;
            }
            let before = run.snapshot(0).unwrap();
            assert_eq!(run.step(&mut doc, 0, 0), Err(Error::Protocol));
            if matches!(op, 6 | 7) {
                assert_eq!(run.step(&mut doc, op, 1), Err(Error::Protocol));
            }
            assert_eq!(run.snapshot(0).unwrap(), before);
            run.step(&mut doc, op, value).unwrap();
        }
        assert!(seen_style && seen_entry && run.request.is_none());
    }
    #[test]
    fn later_default_wrap_wins_but_named_caption_uses_first_nonempty_exact_match() {
        let mut styles = Vec::new();
        for (name, wrap) in [
            ("Default", "no"),
            ("default", "auto"),
            ("Named", ""),
            ("Named", "no"),
            ("Named", "auto"),
        ] {
            let mut s = RawStyle {
                name: ascii(name),
                ..Default::default()
            };
            s.font[12] = ascii(wrap);
            styles.push(s);
        }
        let texts = [
            RawText {
                style: ascii("Named"),
                text: ascii("A"),
                ..Default::default()
            },
            RawText {
                style: ascii("named"),
                text: ascii("B"),
                ..Default::default()
            },
        ];
        let mut r = Run::decode(&packet(&styles, &texts)).unwrap();
        let mut d = Document::default();
        r.begin(&mut d).unwrap();
        let mut results = Vec::new();
        for _ in 0..500 {
            let Some(q) = r.request.as_ref() else {
                break;
            };
            let op = q.op;
            let v = reply(q);
            if op == 7 {
                results.push(
                    crate::document_wire::decode_entry(&r.snapshot(2).unwrap())
                        .unwrap()
                        .text,
                );
            }
            r.step(&mut d, op, v).unwrap();
        }
        assert_eq!(d.script_field(ScriptField::Wrap), Some(1));
        assert_eq!(results, vec![ascii("{\\q2}A"), ascii("B")]);
    }
    #[test]
    fn malformed_import_packets_are_rejected_before_any_run_exists() {
        let bytes = packet(&[RawStyle::default()], &[RawText::default()]);
        for end in 0..bytes.len() {
            assert!(Run::decode(&bytes[..end]).is_err());
        }
        let mut extra = bytes.clone();
        extra.push(0);
        assert!(Run::decode(&extra).is_err());
        let mut huge = packet(&[], &[]);
        let offset = 4 + "English".len() * 2;
        huge[offset..offset + 4].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(Run::decode(&huge), Err(Error::CountOverflow)));
    }
}
#[derive(Default)]
struct RawText {
    start: i32,
    stop: i32,
    _effect: Units,
    style: Units,
    text: Units,
    pal: [Units; 7],
}
struct Reader<'a> {
    bytes: &'a [u8],
    offset: usize,
}
impl Reader<'_> {
    fn word(&mut self) -> Result<u32, Error> {
        let end = self.offset.checked_add(4).ok_or(Error::CountOverflow)?;
        let raw = self.bytes.get(self.offset..end).ok_or(Error::Protocol)?;
        self.offset = end;
        Ok(u32::from_le_bytes(raw.try_into().unwrap()))
    }
    fn count(&mut self) -> Result<usize, Error> {
        let n = self.word()?;
        if n > i32::MAX as u32 {
            return Err(Error::CountOverflow);
        }
        Ok(n as usize)
    }
    fn text(&mut self) -> Result<Units, Error> {
        let n = self.count()?;
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
}
fn ascii(s: &str) -> Units {
    s.encode_utf16().collect()
}
fn prefix(s: &[u16]) -> &[u16] {
    &s[..s.iter().position(|&u| u == 0).unwrap_or(s.len())]
}
fn append_text(bytes: &mut Vec<u8>, s: &[u16]) {
    bytes.extend(s.iter().flat_map(|u| u.to_le_bytes()));
}
#[derive(Clone, Copy)]
enum Number {
    Outline,
    Shadow,
    Size,
    Weight,
    Angle(usize),
    TextAngle(usize),
}
#[derive(Clone, Copy)]
enum Action {
    DefaultStyle,
    WrapNo(bool),
    WrapAuto(bool),
    StyleMargin(usize),
    StyleAlign,
    RelativeWindow,
    RelativeVideo,
    Number(Number),
    Color(usize),
    Alpha(usize, u32),
    Weight(usize),
    Bool(bool),
    StyleEmit,
    TextAlign,
    TextMargin(usize),
    TextDefault,
    TextEmit,
}
struct Request {
    op: u32,
    arg: i32,
    a: Units,
    b: Units,
    action: Action,
}
pub struct Run {
    language: Units,
    styles: Vec<RawStyle>,
    texts: Vec<RawText>,
    text_phase: bool,
    index: usize,
    stage: usize,
    style: StyleValue,
    margins: [i32; 4],
    wrap: Units,
    request: Option<Request>,
    entry: Option<PreparedEntry>,
}
impl Run {
    /// Exact input packet: language string; style/text counts; each style's
    /// name,13 font and7 position strings; each text's start/stop:i32,
    /// effect/style/text and7 position strings. Strings are count:u32 + raw u16.
    pub fn decode(bytes: &[u8]) -> Result<Self, Error> {
        let mut r = Reader { bytes, offset: 0 };
        let language = r.text()?;
        let styles_count = r.count()?;
        let texts_count = r.count()?;
        // Each record has fixed minimum overhead. Reject impossible counts
        // before growing vectors; no count-controlled allocation from a short packet.
        let minimum = styles_count
            .checked_mul(84)
            .and_then(|n| texts_count.checked_mul(48).and_then(|m| n.checked_add(m)))
            .ok_or(Error::CountOverflow)?;
        if minimum > bytes.len() - r.offset {
            return Err(Error::Protocol);
        }
        let mut styles = Vec::new();
        let mut texts = Vec::new();
        for _ in 0..styles_count {
            let mut s = RawStyle {
                name: r.text()?,
                ..Default::default()
            };
            for v in &mut s.font {
                *v = r.text()?;
            }
            for v in &mut s.pal {
                *v = r.text()?;
            }
            styles.push(s);
        }
        for _ in 0..texts_count {
            let mut t = RawText {
                start: r.word()? as i32,
                stop: r.word()? as i32,
                _effect: r.text()?,
                style: r.text()?,
                text: r.text()?,
                ..Default::default()
            };
            for v in &mut t.pal {
                *v = r.text()?;
            }
            texts.push(t);
        }
        if r.offset != bytes.len() {
            return Err(Error::Protocol);
        }
        Ok(Self {
            language,
            styles,
            texts,
            text_phase: false,
            index: 0,
            stage: 0,
            style: crate::ass_style_value::Builder::default().value,
            margins: [0; 4],
            wrap: Vec::new(),
            request: None,
            entry: None,
        })
    }
    pub fn begin(&mut self, doc: &mut Document) -> Result<(), Error> {
        doc.replace_identity(IdentityField::Name, self.language.clone());
        doc.replace_interpretation(InterpretationField::Encoding, 1); // CTextFile::UTF8
        doc.replace_script_field(ScriptField::Width, 640);
        doc.replace_script_field(ScriptField::Height, 480);
        doc.replace_scaled_bas(true);
        doc.replace_script_field(ScriptField::Wrap, 1);
        self.pump(doc)
    }
    fn request(&mut self, op: u32, arg: i32, a: Units, b: Units, action: Action) {
        self.request = Some(Request {
            op,
            arg,
            a,
            b,
            action,
        });
    }
    fn compare(&mut self, a: Units, b: &str, action: Action) {
        self.request(1, 0, a, ascii(b), action);
    }
    fn integer(&mut self, a: Units, target: Number) {
        self.request(2, 0, a, Vec::new(), Action::Number(target));
    }
    fn prepend(&mut self, tag: Units) {
        let text = &mut self.texts[self.index].text;
        let old = core::mem::take(text);
        *text = tag;
        text.extend(old);
    }
    fn wrap_result(&mut self, doc: &mut Document, text: bool, value: i32) -> Result<(), Error> {
        if text {
            if doc.script_field(ScriptField::Wrap).ok_or(Error::Protocol)? != value {
                self.prepend(ascii(&format!("{{\\q{value}}}")));
            }
        } else {
            doc.replace_script_field(ScriptField::Wrap, value);
        }
        Ok(())
    }
    fn pump(&mut self, doc: &Document) -> Result<(), Error> {
        while self.request.is_none() {
            if !self.text_phase {
                if self.index == self.styles.len() {
                    self.text_phase = true;
                    self.index = 0;
                    self.stage = 0;
                    continue;
                }
                let s = &self.styles[self.index];
                let stage = self.stage;
                self.stage += 1;
                match stage {
                    0 => {
                        self.style = crate::ass_style_value::Builder::default().value;
                        self.compare(s.name.clone(), "Default", Action::DefaultStyle);
                    }
                    1 | 2 => {
                        let axis = stage - 1;
                        let v = s.pal[axis + 2].clone();
                        if !v.is_empty() {
                            let size = doc
                                .script_field(if axis == 0 {
                                    ScriptField::Width
                                } else {
                                    ScriptField::Height
                                })
                                .ok_or(Error::UnsuppliedScreen)?;
                            self.request(4, size, v, Vec::new(), Action::StyleMargin(axis));
                        }
                    }
                    3 => self.request(5, 0, s.pal[0].clone(), Vec::new(), Action::StyleAlign),
                    4 => {
                        if !s.pal[1].is_empty() {
                            self.compare(s.pal[1].clone(), "window", Action::RelativeWindow);
                        }
                    }
                    5 | 6 => {
                        let field = stage + 5;
                        if !s.font[field].is_empty() {
                            self.integer(
                                s.font[field].clone(),
                                if stage == 5 {
                                    Number::Outline
                                } else {
                                    Number::Shadow
                                },
                            );
                        }
                    }
                    7..=10 => {
                        let i = stage - 7;
                        self.request(3, 0, s.font[i + 2].clone(), Vec::new(), Action::Color(i));
                    }
                    11 => {
                        if !s.font[0].is_empty() {
                            self.style.font_name = s.font[0].clone();
                        }
                    }
                    12 => {
                        if !s.font[1].is_empty() {
                            self.integer(s.font[1].clone(), Number::Size);
                        }
                    }
                    13 => {
                        if !s.font[6].is_empty() {
                            self.compare(s.font[6].clone(), "normal", Action::Weight(0));
                        }
                    }
                    14 => {
                        if self.style.font_weight == 0 {
                            self.style.font_weight = 400;
                        }
                    }
                    15 | 16 => {
                        let i = stage - 8;
                        if !s.font[i].is_empty() {
                            self.compare(s.font[i].clone(), "yes", Action::Bool(stage == 15));
                        }
                    }
                    17..=19 => {
                        let i = stage - 17;
                        if !s.pal[i + 4].is_empty() {
                            self.integer(s.pal[i + 4].clone(), Number::Angle(i));
                        }
                    }
                    20 => {
                        self.style.char_set = 1;
                        self.request(6, 0, s.name.clone(), Vec::new(), Action::StyleEmit);
                    }
                    _ => return Err(Error::Protocol),
                }
            } else {
                if self.index == self.texts.len() {
                    return Ok(());
                }
                let t = &self.texts[self.index];
                let stage = self.stage;
                self.stage += 1;
                match stage {
                    0 => {
                        self.margins = [0; 4];
                        if !t.pal[0].is_empty() {
                            self.request(5, 0, t.pal[0].clone(), Vec::new(), Action::TextAlign);
                        }
                    }
                    1 | 2 => {
                        let axis = stage - 1;
                        let v = t.pal[axis + 2].clone();
                        if !v.is_empty() {
                            let size = doc
                                .script_field(if axis == 0 {
                                    ScriptField::Width
                                } else {
                                    ScriptField::Height
                                })
                                .ok_or(Error::UnsuppliedScreen)?;
                            self.request(4, size, v, Vec::new(), Action::TextMargin(axis));
                        }
                    }
                    3..=5 => {
                        let i = stage - 3;
                        self.integer(t.pal[i + 4].clone(), Number::TextAngle(i));
                    }
                    6 => self.compare(t.style.clone(), "Default", Action::TextDefault),
                    7 => {
                        self.entry = Some(PreparedEntry {
                            text: t.text.clone(),
                            style: t.style.clone(),
                            actor: Vec::new(),
                            effect: Vec::new(),
                            unicode: true,
                            start: i64::from(t.start),
                            end: i64::from(t.stop),
                            margins: self.margins,
                            layer: 0,
                            readorder: 0,
                        });
                        self.request(7, 0, Vec::new(), Vec::new(), Action::TextEmit);
                    }
                    _ => return Err(Error::Protocol),
                }
            }
        }
        Ok(())
    }
    pub fn step(&mut self, doc: &mut Document, command: u32, value: i32) -> Result<(), Error> {
        let r = self.request.as_ref().ok_or(Error::Protocol)?;
        if r.op != command || (matches!(command, 6 | 7) && value != 0) {
            return Err(Error::Protocol);
        }
        let action = r.action;
        self.request = None;
        match action {
            Action::DefaultStyle => {
                if value == 0 && !self.styles[self.index].font[12].is_empty() {
                    self.wrap = self.styles[self.index].font[12].clone();
                    self.compare(self.wrap.clone(), "no", Action::WrapNo(false));
                }
            }
            Action::WrapNo(text) => {
                if value == 0 {
                    self.wrap_result(doc, text, 2)?;
                } else {
                    self.compare(self.wrap.clone(), "auto", Action::WrapAuto(text));
                }
            }
            Action::WrapAuto(text) => self.wrap_result(doc, text, 1)?,
            Action::StyleMargin(axis) => {
                self.style.margins[axis] = value;
                self.style.margins[axis + 2] = value;
            }
            Action::StyleAlign => self.style.screen_alignment = value,
            Action::RelativeWindow => {
                if value == 0 {
                    self.style.relative_to = 0;
                } else {
                    self.compare(
                        self.styles[self.index].pal[1].clone(),
                        "video",
                        Action::RelativeVideo,
                    );
                }
            }
            Action::RelativeVideo => self.style.relative_to = i32::from(value == 0),
            Action::Number(target) => match target {
                Number::Outline => {
                    self.style.outline_width_x = f64::from(value);
                    self.style.outline_width_y = f64::from(value);
                }
                Number::Shadow => {
                    self.style.shadow_depth_x = f64::from(value);
                    self.style.shadow_depth_y = f64::from(value);
                }
                Number::Size => self.style.font_size = f64::from(value),
                Number::Weight => self.style.font_weight = value,
                Number::Angle(i) => match i {
                    0 => self.style.font_angle_z = f64::from(value),
                    1 => self.style.font_angle_x = f64::from(value),
                    _ => self.style.font_angle_y = f64::from(value),
                },
                Number::TextAngle(i) => {
                    if value != 0 {
                        self.prepend(ascii(&format!("{{\\r{}{value}}}", ["z", "x", "y"][i])));
                    }
                }
            },
            Action::Color(i) => self.request(
                2,
                0,
                self.styles[self.index].font[9].clone(),
                Vec::new(),
                Action::Alpha(i, value as u32),
            ),
            Action::Alpha(i, color) => {
                let a = color >> 24;
                let percent = u32::from(value as u8).min(100);
                self.style.colors[i] = color & 0xffffff;
                self.style.alpha[i] = (a + (255 - a) * percent / 100) as u8;
            }
            Action::Weight(i) => {
                if value == 0 {
                    self.style.font_weight = [400, 700, 300, 600][i];
                } else if i < 3 {
                    self.compare(
                        self.styles[self.index].font[6].clone(),
                        ["normal", "bold", "lighter", "bolder"][i + 1],
                        Action::Weight(i + 1),
                    );
                } else {
                    self.integer(self.styles[self.index].font[6].clone(), Number::Weight);
                }
            }
            Action::Bool(italic) => {
                if italic {
                    self.style.italic = value == 0;
                } else {
                    self.style.underline = value == 0;
                }
            }
            Action::StyleEmit => {
                self.index += 1;
                self.stage = 0;
            }
            Action::TextAlign => self.prepend(ascii(&format!("{{\\an{value}}}"))),
            Action::TextMargin(axis) => {
                self.margins[axis] = value;
                self.margins[axis + 2] = value;
            }
            Action::TextDefault => {
                if value != 0 {
                    if let Some(s) = self.styles.iter().find(|s| {
                        prefix(&s.name) == prefix(&self.texts[self.index].style)
                            && !s.font[12].is_empty()
                    }) {
                        self.wrap = s.font[12].clone();
                        self.compare(self.wrap.clone(), "no", Action::WrapNo(true));
                    }
                }
            }
            Action::TextEmit => {
                self.index += 1;
                self.stage = 0;
                self.entry = None;
            }
        }
        self.pump(doc)
    }
    pub fn snapshot(&self, group: u32) -> Result<Vec<u8>, Error> {
        match group {
            0 => {
                let mut b = Vec::new();
                let (op, arg, a, c) = self.request.as_ref().map_or((8, 0, &[][..], &[][..]), |r| {
                    (r.op, r.arg, &r.a[..], &r.b[..])
                });
                for n in [
                    op,
                    arg as u32,
                    self.index as u32,
                    a.len() as u32,
                    c.len() as u32,
                ] {
                    b.extend_from_slice(&n.to_le_bytes());
                }
                append_text(&mut b, a);
                append_text(&mut b, c);
                Ok(b)
            }
            1 if self.request.as_ref().is_some_and(|r| r.op == 6) => {
                crate::document_style::encode(&self.style).map_err(|_| Error::Protocol)
            }
            2 if self.request.as_ref().is_some_and(|r| r.op == 7) => {
                crate::document_wire::encode_entry(self.entry.as_ref().ok_or(Error::Protocol)?)
                    .map_err(|_| Error::Protocol)
            }
            _ => Err(Error::Protocol),
        }
    }
}
