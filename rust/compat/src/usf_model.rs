// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned USF parsed state and ordered DOM traversal from cleanup 67da07cd.
//! Nodes are temporary integers. COM access and Windows scalar conversions are
//! supplied between admitted calls; no native pointer or Unicode conversion is stored.
use crate::ass_document::Error;
use alloc::{format, vec::Vec};

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;
    struct Node {
        name: &'static str,
        child: i32,
        next: i32,
        attrs: Vec<(&'static str, &'static str)>,
        text: Units,
    }
    fn node(name: &'static str, child: i32, next: i32) -> Node {
        Node {
            name,
            child,
            next,
            attrs: Vec::new(),
            text: Vec::new(),
        }
    }
    fn drive(s: &mut State, nodes: &[Node]) -> bool {
        for _ in 0..2000 {
            let p = s.parse.as_ref().unwrap();
            let Some(q) = p.pending.as_ref() else {
                return p.found;
            };
            let op = q.op;
            let n = nodes.get(q.node as usize);
            let (value, text) = match op {
                1 => (0, units(n.unwrap().name)),
                2 => (n.unwrap().child, Vec::new()),
                3 => (n.unwrap().next, Vec::new()),
                4 => (
                    0,
                    n.unwrap()
                        .attrs
                        .iter()
                        .find(|(key, _)| eq(&q.text, key))
                        .map(|(_, v)| units(v))
                        .unwrap_or_default(),
                ),
                5 | 6 => (0, n.unwrap().text.clone()),
                7 | 8 => (
                    alloc::string::String::from_utf16(&q.text)
                        .unwrap()
                        .parse()
                        .unwrap_or(0),
                    Vec::new(),
                ),
                9 => (0x80112233u32 as i32, Vec::new()),
                10 => (
                    i32::from(!eq(&q.text, "Default") && !eq(&q.text, "default")),
                    Vec::new(),
                ),
                _ => panic!("unexpected service"),
            };
            let before = s.request().unwrap();
            assert_eq!(s.step(99, value, &text), Err(Error::Protocol));
            assert_eq!(s.request().unwrap(), before);
            s.step(op, value, &text).unwrap();
        }
        panic!("traversal did not terminate")
    }
    #[test]
    fn metadata_survives_empty_and_wrong_root_loads_and_keyframes_ignore_children() {
        let mut nodes = vec![
            node("", 0, 0),
            node("usfsubtitles", 2, 0),
            node("metadata", 3, 5),
            node("title", 0, 4),
            node("language", 0, 0),
            node("effects", 6, 0),
            node("effect", 7, 0),
            node("keyframes", 8, 0),
            node("keyframe", 9, 0),
            node("fontstyle", 0, 0),
        ];
        nodes[3].text = vec![65, 0, 0xd800];
        nodes[4].text = units("English");
        nodes[4].attrs.push(("code", "en"));
        nodes[6].attrs.push(("name", "fade"));
        nodes[8].attrs.push(("position", "50"));
        nodes[9].attrs.push(("color", "red"));
        let mut state = State::default();
        state.begin().unwrap();
        assert_eq!(state.begin(), Err(Error::Protocol));
        assert!(drive(&mut state, &nodes));
        assert_eq!(state.model.metadata[0], [65, 0, 0xd800]);
        assert_eq!(state.model.metadata[6], units("en"));
        assert_eq!(state.model.effects[0].keyframes[0].name, units("50"));
        assert!(state.model.effects[0].keyframes[0]
            .font
            .iter()
            .all(Vec::is_empty));
        let snapshot = state.model.snapshot().unwrap();
        state.begin().unwrap();
        assert!(!drive(&mut state, &[node("", 0, 0), node("wrong", 0, 0)]));
        assert!(state.model.effects.is_empty());
        assert_eq!(state.model.metadata[0], [65, 0, 0xd800]);
        state.begin().unwrap();
        assert!(drive(
            &mut state,
            &[node("", 0, 0), node("usfsubtitles", 0, 0)]
        ));
        assert_eq!(state.model.metadata[7], units("English"));
        assert_ne!(snapshot, state.model.snapshot().unwrap());
    }
    #[test]
    fn inline_text_retains_raw_units_entity_xml_and_same_order_resets() {
        let mut nodes = vec![
            node("", 0, 0),
            node("usfsubtitles", 2, 0),
            node("subtitles", 3, 0),
            node("subtitle", 4, 0),
            node("text", 5, 0),
            node("font", 6, 7),
            node("#text", 0, 0),
            node("k", 8, 9),
            node("#text", 0, 0),
            node("br", 10, 0),
            node("#text", 0, 0),
        ];
        nodes[3].attrs = vec![("start", "-2"), ("duration", "5")];
        nodes[5].attrs = vec![("face", "Arial"), ("size", "20"), ("color", "red")];
        nodes[6].text = units(" A\r\n  &amp;\t B ");
        nodes[6].text.push(0xd800);
        nodes[7].attrs.push(("t", "-25"));
        nodes[8].text = units("ignored");
        nodes[10].text = units("ignored");
        let mut s = State::default();
        s.begin().unwrap();
        assert!(drive(&mut s, &nodes));
        let t = &s.model.texts[0];
        assert_eq!((t.start, t.stop), (-2, 3));
        assert_eq!(t.style, units("Default"));
        let mut expected = units("{\\fnArial}{\\fs20}{\\1c&H80112233&} A &amp;\t B ");
        expected.push(0xd800);
        expected.extend(units("{\\fn}{\\fs}{\\1c}{\\kf-2}\\N"));
        assert_eq!(t.text, expected);
    }
    #[test]
    fn first_default_inherits_only_into_nondefaults_and_replaces_whole_font_blocks() {
        let mut nodes = vec![
            node("", 0, 0),
            node("usfsubtitles", 2, 0),
            node("styles", 3, 0),
            node("style", 4, 5),
            node("fontstyle", 0, 0),
            node("style", 6, 7),
            node("fontstyle", 0, 0),
            node("style", 8, 0),
            node("fontstyle", 0, 9),
            node("fontstyle", 0, 0),
        ];
        nodes[3].attrs.push(("name", "default"));
        nodes[4].attrs = vec![("size", "20"), ("face", "First")];
        nodes[5].attrs.push(("name", "Default"));
        nodes[6].attrs.push(("size", "30"));
        nodes[7].attrs.push(("name", "Named"));
        nodes[8].attrs.push(("face", "Discarded"));
        nodes[9].attrs.push(("color", "red"));
        let mut s = State::default();
        s.begin().unwrap();
        assert!(drive(&mut s, &nodes));
        assert!(s.model.styles[1].font[0].is_empty());
        assert_eq!(s.model.styles[1].font[1], units("30"));
        assert_eq!(s.model.styles[2].font[0], units("First"));
        assert_eq!(s.model.styles[2].font[1], units("20"));
        assert_eq!(s.model.styles[2].font[2], units("red"));
    }
}

pub(crate) type Units = Vec<u16>;
#[derive(Clone, Default)]
pub(crate) struct RawStyle {
    pub name: Units,
    // face,size,color[4],weight,italic,underline,alpha,outline,shadow,wrap
    pub font: [Units; 13],
    pub pal: [Units; 7],
}
#[derive(Clone, Default)]
pub(crate) struct RawText {
    pub start: i32,
    pub stop: i32,
    pub effect: Units,
    pub style: Units,
    pub text: Units,
    pub pal: [Units; 7],
}
#[derive(Default)]
struct Effect {
    name: Units,
    keyframes: Vec<RawStyle>,
}
#[derive(Default)]
pub struct Model {
    // title,date,comment,author(name,email,url),language(code,text),extension(code,text)
    pub(crate) metadata: [Units; 10],
    pub(crate) styles: Vec<RawStyle>,
    effects: Vec<Effect>,
    pub(crate) texts: Vec<RawText>,
}
fn units(s: &str) -> Units {
    s.encode_utf16().collect()
}
fn eq(s: &[u16], ascii: &str) -> bool {
    s.iter().copied().eq(ascii.bytes().map(u16::from))
}
fn word(b: &mut Vec<u8>, n: usize) -> Result<(), Error> {
    let n = u32::try_from(n).map_err(|_| Error::CountOverflow)?;
    if n > i32::MAX as u32 {
        return Err(Error::CountOverflow);
    }
    b.extend(n.to_le_bytes());
    Ok(())
}
fn string(b: &mut Vec<u8>, s: &[u16]) -> Result<(), Error> {
    word(b, s.len())?;
    b.extend(s.iter().flat_map(|u| u.to_le_bytes()));
    Ok(())
}
fn style(b: &mut Vec<u8>, s: &RawStyle) -> Result<(), Error> {
    string(b, &s.name)?;
    for v in s.font.iter().chain(s.pal.iter()) {
        string(b, v)?;
    }
    Ok(())
}
impl Model {
    pub fn snapshot(&self) -> Result<Vec<u8>, Error> {
        let mut b = Vec::new();
        for v in &self.metadata {
            string(&mut b, v)?;
        }
        word(&mut b, self.styles.len())?;
        for s in &self.styles {
            style(&mut b, s)?;
        }
        word(&mut b, self.effects.len())?;
        for e in &self.effects {
            string(&mut b, &e.name)?;
            word(&mut b, e.keyframes.len())?;
            for k in &e.keyframes {
                style(&mut b, k)?;
            }
        }
        word(&mut b, self.texts.len())?;
        for t in &self.texts {
            b.extend(t.start.to_le_bytes());
            b.extend(t.stop.to_le_bytes());
            for v in [&t.effect, &t.style, &t.text]
                .into_iter()
                .chain(t.pal.iter())
            {
                string(&mut b, v)?;
            }
        }
        Ok(b)
    }
    pub fn replace_converted_text(&mut self, index: usize, text: &[u16]) -> Result<(), Error> {
        self.texts.get_mut(index).ok_or(Error::Protocol)?.text = text.to_vec();
        Ok(())
    }
}

// The order of font getters is significant: italic is queried before weight,
// despite their order in the detached packet and conversion model.
const FONT: [(&str, usize); 13] = [
    ("face", 0),
    ("size", 1),
    ("color", 2),
    ("back-color", 3),
    ("outline-color", 4),
    ("shadow-color", 5),
    ("italic", 7),
    ("weight", 6),
    ("underline", 8),
    ("alpha", 9),
    ("outline-level", 10),
    ("shadow-level", 11),
    ("wrap", 12),
];
const PAL: [&str; 7] = [
    "alignment",
    "relative-to",
    "horizontal-margin",
    "vertical-margin",
    "rotate-z",
    "rotate-x",
    "rotate-y",
];
#[derive(Clone, Copy)]
enum Visitor {
    Find,
    Root(usize),
    Container(usize),
    Meta,
    Author,
    Style(usize),
    Effect(usize),
    Keys(usize),
    Key(usize, usize),
    Subtitle(i32, i32),
    Inline(usize),
}
#[derive(Clone, Copy)]
enum Slot {
    Meta(usize),
    StyleName(usize),
    StyleFont(usize, usize),
    StylePal(usize, usize),
    EffectName(usize),
    Key(usize, usize),
    Times(usize),
    TextStyle(usize),
    TextEffect(usize),
    TextPal(usize, usize),
    Font(usize, usize),
}
enum Task {
    Visit(u32, Visitor),
    Children(u32, Visitor),
    Next(u32, Visitor),
    Attr(u32, &'static str, Slot),
    Text(u32, usize),
    Times(u32),
    Finish(usize),
    Font(usize),
    Color(usize, usize),
    Inherit(usize, Option<usize>),
}
enum Action {
    Name(u32, Visitor),
    Child(Visitor),
    Assign(Slot),
    Xml(usize),
    Start(u32),
    Stop(u32, i32, bool),
    Karaoke(usize),
    KaraokeValue(usize),
    Color(usize, usize),
    Inherit(usize, Option<usize>),
}
struct Request {
    op: u32,
    node: u32,
    text: Units,
    action: Action,
}
#[derive(Default)]
struct Frame {
    parent: Option<usize>,
    target: usize,
    prefix: Units,
    body: Units,
    postfix: Units,
    font: [Units; 13],
}
#[derive(Default)]
struct Parse {
    tasks: Vec<Task>,
    pending: Option<Request>,
    found: bool,
    times: [Units; 3],
    frames: Vec<Frame>,
}
#[derive(Default)]
pub struct State {
    pub model: Model,
    parse: Option<Parse>,
}
/// Detached request for platform drivers; contains no borrowed model state.
pub struct ServiceRequest {
    pub op: u32,
    pub node: u32,
    pub accepted: bool,
    pub text: Vec<u16>,
}
impl State {
    pub fn service_request(&self) -> Result<ServiceRequest, Error> {
        let p = self.parse.as_ref().ok_or(Error::Protocol)?;
        Ok(match &p.pending {
            Some(q) => ServiceRequest {
                op: q.op,
                node: q.node,
                accepted: false,
                text: q.text.clone(),
            },
            None => ServiceRequest {
                op: 11,
                node: 0,
                accepted: p.found,
                text: Vec::new(),
            },
        })
    }
    /// Called only after MSXML load succeeds. A failed load never enters here.
    pub fn begin(&mut self) -> Result<(), Error> {
        if self.parse.as_ref().is_some_and(|p| p.pending.is_some()) {
            return Err(Error::Protocol);
        }
        self.model.styles.clear();
        self.model.effects.clear();
        self.model.texts.clear();
        let mut p = Parse::default();
        p.tasks.push(Task::Visit(1, Visitor::Find));
        p.pump(&mut self.model);
        self.parse = Some(p);
        Ok(())
    }
    pub fn ready(&self) -> bool {
        self.parse.as_ref().is_none_or(|p| p.pending.is_none())
    }
    pub fn step(&mut self, op: u32, value: i32, text: &[u16]) -> Result<(), Error> {
        let p = self.parse.as_mut().ok_or(Error::Protocol)?;
        let q = p.pending.as_ref().ok_or(Error::Protocol)?;
        if q.op != op
            || (matches!(op, 1 | 4 | 5 | 6) && value != 0)
            || (!matches!(op, 1 | 4 | 5 | 6) && !text.is_empty())
            || (matches!(op, 2 | 3) && value < 0)
        {
            return Err(Error::Protocol);
        }
        let q = p.pending.take().unwrap();
        p.reply(&mut self.model, q.action, value, text);
        p.pump(&mut self.model);
        Ok(())
    }
    /// op,node,result,text-length:u32; raw request text. Done is op11, result0/1.
    pub fn request(&self) -> Result<Vec<u8>, Error> {
        let p = self.parse.as_ref().ok_or(Error::Protocol)?;
        let mut b = Vec::new();
        let (op, node, result, text) = match &p.pending {
            Some(q) => (q.op, q.node, 0, q.text.as_slice()),
            None => (11, 0, u32::from(p.found), &[][..]),
        };
        b.extend(op.to_le_bytes());
        b.extend(node.to_le_bytes());
        b.extend(result.to_le_bytes());
        string(&mut b, text)?;
        Ok(b)
    }
}
impl Parse {
    fn ask(&mut self, op: u32, node: u32, text: Units, action: Action) {
        self.pending = Some(Request {
            op,
            node,
            text,
            action,
        });
    }
    fn children(&mut self, node: u32, v: Visitor) {
        self.tasks.push(Task::Children(node, v));
    }
    fn attr(&mut self, node: u32, name: &'static str, slot: Slot) {
        self.tasks.push(Task::Attr(node, name, slot));
    }
    fn font(&mut self, node: u32, index: usize, inline: bool) {
        for &(name, i) in FONT.iter().rev() {
            self.attr(
                node,
                name,
                if inline {
                    Slot::Font(index, i)
                } else {
                    Slot::StyleFont(index, i)
                },
            );
        }
    }
    fn pal(&mut self, node: u32, index: usize, text: bool) {
        for (i, &name) in PAL.iter().enumerate().rev() {
            self.attr(
                node,
                name,
                if text {
                    Slot::TextPal(index, i)
                } else {
                    Slot::StylePal(index, i)
                },
            );
        }
    }
    fn frame(&mut self, node: u32, parent: Option<usize>, target: usize) {
        let i = self.frames.len();
        self.frames.push(Frame {
            parent,
            target,
            ..Default::default()
        });
        self.tasks.push(Task::Visit(node, Visitor::Inline(i)));
    }
    fn pump(&mut self, m: &mut Model) {
        while self.pending.is_none() {
            let Some(task) = self.tasks.pop() else {
                break;
            };
            match task {
                Task::Visit(_, Visitor::Find)
                | Task::Children(_, Visitor::Find)
                | Task::Next(_, Visitor::Find)
                    if self.found => {}
                Task::Visit(node, v) => self.ask(1, node, Vec::new(), Action::Name(node, v)),
                Task::Children(node, v) => self.ask(2, node, Vec::new(), Action::Child(v)),
                Task::Next(node, v) => self.ask(3, node, Vec::new(), Action::Child(v)),
                Task::Attr(node, name, slot) => {
                    self.ask(4, node, units(name), Action::Assign(slot))
                }
                Task::Text(node, i) => self.ask(5, node, Vec::new(), Action::Assign(Slot::Meta(i))),
                Task::Times(node) => {
                    if !(self.times[0].is_empty()
                        || self.times[1].is_empty() && self.times[2].is_empty())
                    {
                        self.ask(7, 0, self.times[0].clone(), Action::Start(node));
                    }
                }
                Task::Finish(i) => {
                    let f = &mut self.frames[i];
                    let mut out = core::mem::take(&mut f.prefix);
                    out.append(&mut f.body);
                    out.append(&mut f.postfix);
                    if let Some(parent) = f.parent {
                        self.frames[parent].body.extend(out);
                    } else {
                        m.texts[f.target].text = out;
                    }
                }
                Task::Font(i) => {
                    let f = &mut self.frames[i];
                    for (field, tag) in [(0, "fn"), (1, "fs"), (10, "bord"), (11, "shad")] {
                        if !f.font[field].is_empty() {
                            f.prefix.extend(units(&format!("{{\\{tag}")));
                            f.prefix.extend_from_slice(&f.font[field]);
                            f.prefix.push(125);
                            f.postfix.extend(units(&format!("{{\\{tag}}}")));
                        }
                    }
                    for color in (0..4).rev() {
                        self.tasks.push(Task::Color(i, color));
                    }
                }
                Task::Color(i, color) => {
                    let raw = &self.frames[i].font[color + 2];
                    if !raw.is_empty() {
                        self.ask(9, 0, raw.clone(), Action::Color(i, color));
                    }
                }
                Task::Inherit(i, default) => {
                    if i < m.styles.len() {
                        self.ask(10, 0, m.styles[i].name.clone(), Action::Inherit(i, default));
                    } else {
                        for t in &mut m.texts {
                            if t.style.is_empty() {
                                t.style = units("Default");
                            }
                        }
                    }
                }
            }
        }
    }
    fn assign(&mut self, m: &mut Model, slot: Slot, text: &[u16]) {
        let out = match slot {
            Slot::Meta(i) => &mut m.metadata[i],
            Slot::StyleName(i) => &mut m.styles[i].name,
            Slot::StyleFont(i, j) => &mut m.styles[i].font[j],
            Slot::StylePal(i, j) => &mut m.styles[i].pal[j],
            Slot::EffectName(i) => &mut m.effects[i].name,
            Slot::Key(i, j) => &mut m.effects[i].keyframes[j].name,
            Slot::Times(i) => &mut self.times[i],
            Slot::TextStyle(i) => &mut m.texts[i].style,
            Slot::TextEffect(i) => &mut m.texts[i].effect,
            Slot::TextPal(i, j) => &mut m.texts[i].pal[j],
            Slot::Font(i, j) => &mut self.frames[i].font[j],
        };
        *out = text.to_vec();
    }
    fn reply(&mut self, m: &mut Model, a: Action, value: i32, text: &[u16]) {
        match a {
            Action::Name(node, v) => self.visit(m, node, v, text),
            Action::Child(v) => {
                if value != 0 {
                    let node = value as u32;
                    self.tasks.push(Task::Next(node, v));
                    if let Visitor::Inline(parent) = v {
                        let target = self.frames[parent].target;
                        self.frame(node, Some(parent), target);
                    } else {
                        self.tasks.push(Task::Visit(node, v));
                    }
                }
            }
            Action::Assign(slot) => self.assign(m, slot, text),
            Action::Xml(i) => {
                let out = &mut self.frames[i].body;
                for &u in text {
                    if u == 13 {
                        continue;
                    }
                    let u = if u == 10 { 32 } else { u };
                    if u != 32 || out.last() != Some(&32) {
                        out.push(u);
                    }
                }
            }
            Action::Start(node) => {
                let duration = self.times[1].is_empty();
                let raw = self.times[if duration { 2 } else { 1 }].clone();
                self.ask(7, 0, raw, Action::Stop(node, value, duration));
            }
            Action::Stop(node, start, duration) => self.tasks.push(Task::Visit(
                node,
                Visitor::Subtitle(
                    start,
                    if duration {
                        start.wrapping_add(value)
                    } else {
                        value
                    },
                ),
            )),
            Action::Karaoke(i) => self.ask(8, 0, text.to_vec(), Action::KaraokeValue(i)),
            Action::KaraokeValue(i) => self.frames[i]
                .body
                .extend(units(&format!("{{\\kf{}}}", value / 10))),
            Action::Color(i, color) => {
                self.frames[i].prefix.extend(units(&format!(
                    "{{\\{}c&H{:06x}&}}",
                    color + 1,
                    value as u32
                )));
                self.frames[i]
                    .postfix
                    .extend(units(&format!("{{\\{}c}}", color + 1)));
            }
            Action::Inherit(i, None) => {
                self.tasks.push(if value == 0 {
                    Task::Inherit(0, Some(i))
                } else {
                    Task::Inherit(i + 1, None)
                });
            }
            Action::Inherit(i, Some(default)) => {
                if value != 0 {
                    let def = m.styles[default].clone();
                    let s = &mut m.styles[i];
                    for (v, d) in s
                        .font
                        .iter_mut()
                        .chain(s.pal.iter_mut())
                        .zip(def.font.iter().chain(def.pal.iter()))
                    {
                        if v.is_empty() {
                            *v = d.clone();
                        }
                    }
                }
                self.tasks.push(Task::Inherit(i + 1, Some(default)));
            }
        }
    }
    fn visit(&mut self, m: &mut Model, node: u32, v: Visitor, name: &[u16]) {
        match v {
            Visitor::Find => {
                if eq(name, "usfsubtitles") {
                    self.found = true;
                    self.tasks.push(Task::Inherit(0, None));
                    for pass in (0..4).rev() {
                        self.children(node, Visitor::Root(pass));
                    }
                } else {
                    self.children(node, v);
                }
            }
            Visitor::Root(pass) => {
                if eq(name, ["metadata", "styles", "effects", "subtitles"][pass]) {
                    if pass == 0 {
                        self.tasks.push(Task::Visit(node, Visitor::Meta));
                    } else {
                        self.children(node, Visitor::Container(pass));
                    }
                }
            }
            Visitor::Container(pass) => {
                if !eq(name, ["", "style", "effect", "subtitle"][pass]) {
                    return;
                }
                match pass {
                    1 => {
                        let i = m.styles.len();
                        m.styles.push(RawStyle::default());
                        self.tasks.push(Task::Visit(node, Visitor::Style(i)));
                    }
                    2 => {
                        let i = m.effects.len();
                        m.effects.push(Effect::default());
                        self.tasks.push(Task::Visit(node, Visitor::Effect(i)));
                    }
                    3 => {
                        self.tasks.push(Task::Times(node));
                        for (i, key) in ["start", "stop", "duration"].into_iter().enumerate().rev()
                        {
                            self.attr(node, key, Slot::Times(i));
                        }
                    }
                    _ => unreachable!(),
                }
            }
            Visitor::Meta => {
                if eq(name, "author") {
                    self.children(node, Visitor::Author);
                    return;
                }
                self.children(node, v);
                if let Some(i) = ["title", "date", "comment"]
                    .iter()
                    .position(|s| eq(name, s))
                {
                    self.tasks.push(Task::Text(node, i));
                } else if eq(name, "language") || eq(name, "languageext") {
                    let i = if eq(name, "language") { 6 } else { 8 };
                    self.attr(node, "code", Slot::Meta(i));
                    self.tasks.push(Task::Text(node, i + 1));
                }
            }
            Visitor::Author => {
                if let Some(i) = ["name", "email", "url"].iter().position(|s| eq(name, s)) {
                    self.tasks.push(Task::Text(node, i + 3));
                }
            }
            Visitor::Style(i) => {
                if eq(name, "fontstyle") {
                    self.font(node, i, false);
                } else if eq(name, "position") {
                    self.pal(node, i, false);
                } else {
                    self.children(node, v);
                    if eq(name, "style") {
                        self.attr(node, "name", Slot::StyleName(i));
                    }
                }
            }
            Visitor::Effect(i) => {
                if eq(name, "keyframes") {
                    self.children(node, Visitor::Keys(i));
                } else {
                    self.children(node, v);
                    if eq(name, "effect") {
                        self.attr(node, "name", Slot::EffectName(i));
                    }
                }
            }
            Visitor::Keys(i) => {
                if eq(name, "keyframe") {
                    let j = m.effects[i].keyframes.len();
                    m.effects[i].keyframes.push(RawStyle::default());
                    self.tasks.push(Task::Visit(node, Visitor::Key(i, j)));
                }
            }
            // The reference does not recurse into keyframe font/position children.
            Visitor::Key(i, j) => {
                if eq(name, "keyframe") {
                    self.attr(node, "position", Slot::Key(i, j));
                }
            }
            Visitor::Subtitle(start, stop) => {
                if eq(name, "text") || eq(name, "karaoke") {
                    let i = m.texts.len();
                    m.texts.push(RawText {
                        start,
                        stop,
                        ..Default::default()
                    });
                    self.frame(node, None, i);
                    self.pal(node, i, true);
                    self.attr(node, "effect", Slot::TextEffect(i));
                    self.attr(node, "style", Slot::TextStyle(i));
                } else {
                    self.children(node, v);
                }
            }
            Visitor::Inline(i) => {
                self.tasks.push(Task::Finish(i));
                if eq(name, "k") {
                    self.ask(4, node, units("t"), Action::Karaoke(i));
                    return;
                }
                if eq(name, "br") {
                    self.frames[i].body = units("\\N");
                    return;
                }
                if eq(name, "#text") {
                    self.ask(6, node, Vec::new(), Action::Xml(i));
                    return;
                }
                self.children(node, v);
                if let Some(tag) = ["b", "i", "u"].into_iter().find(|s| eq(name, s)) {
                    self.frames[i].prefix = units(&format!("{{\\{tag}1}}"));
                    self.frames[i].postfix = units(&format!("{{\\{tag}}}"));
                } else if eq(name, "font") {
                    self.tasks.push(Task::Font(i));
                    self.font(node, i, true);
                }
            }
        }
    }
}
