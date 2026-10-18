// SPDX-License-Identifier: GPL-2.0-or-later
//! ASS/SSA header state and document mutations from cleanup STS.cpp.
//!
//! Native adapters read lines/classify whitespace, lowercase keys, perform CRT
//! numeric conversion and suffix comparison, and execute style/font actions.
//! No file, native object, or caller buffer is retained by this session.
use crate::ass_headers::{self, Headers};
use crate::document::{Document, LayoutField, MetadataField, ScriptField};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Protocol,
    UnsuppliedScreen,
    Preparation(crate::document_prepare::Error),
    Storage(crate::document::Refusal),
    CountOverflow,
}

#[derive(Default)]
pub struct Run {
    headers: Headers,
    kind: u32,
    pending: bool,
    failed: bool,
    dialogue: Option<crate::ass_dialogue::Dialogue>,
    input: crate::ass_input::Input,
    style: Option<crate::ass_style::Style>,
    style_field: u32,
}

impl Run {
    pub fn begin(document: &mut Document) -> Self {
        document.replace_metadata(MetadataField::ColorMatrix, 0); // BT601
        document.replace_metadata(MetadataField::ColorRange, 1); // TV
        Self::default()
    }

    /// Reply: entry kind, dialogue version, style version, accepted section.
    pub fn state(&self) -> [i32; 4] {
        [
            self.kind as i32,
            self.headers.version,
            self.headers.style_version,
            i32::from(self.headers.accepted && !self.failed),
        ]
    }

    pub fn classify(&mut self, lowered_key: &[u16]) -> Result<(), Error> {
        if self.pending || self.failed || !self.input.can_classify() {
            return Err(Error::Protocol);
        }
        self.kind = self.headers.classify(lowered_key);
        self.pending = matches!(self.kind, 1 | 2 | 4..=11 | 17);
        self.input.classified();
        Ok(())
    }

    pub fn read_line(&mut self, raw: &[u16], spaces: &[u8], eof: bool) -> Result<(), Error> {
        if self.pending || self.failed || !self.input.read(raw, spaces, eof) {
            return Err(Error::Protocol);
        }
        Ok(())
    }
    pub fn input_part(&self, group: u32) -> Result<alloc::vec::Vec<u8>, Error> {
        self.input.part(group).ok_or(Error::Protocol)
    }
    pub fn end_record(&mut self, success: bool) -> Result<(), Error> {
        if !self.input.can_finish() || (success && (self.pending || self.failed)) {
            return Err(Error::Protocol);
        }
        if !success {
            self.failed = true;
        }
        self.input.finish(success);
        Ok(())
    }

    pub fn begin_style(&mut self, input: &[u16]) -> Result<(), Error> {
        if self.kind != 2 || !self.pending || self.style.is_some() {
            return Err(Error::Protocol);
        }
        let mut style = crate::ass_style::Style::new(input, self.headers.style_version);
        self.style_field = style.next_field();
        self.style = Some(style);
        Ok(())
    }
    /// Values and the remaining native GetStr/GetInt/GetFloat buffer are copied
    /// into the session. kind1=i32 bits, kind2=f64 bits, kind3=raw text.
    pub fn style_value(
        &mut self,
        kind: u32,
        bits: u64,
        text: &[u16],
        remainder: &[u16],
    ) -> Result<(), Error> {
        let style = self.style.as_mut().ok_or(Error::Protocol)?;
        let field = self.style_field;
        let expected = match field {
            1 | 2 => 3,
            3 | 12..=15 | 17 | 18 => 2,
            4..=11 | 16 | 19..=26 => 1,
            _ => return Err(Error::Protocol),
        };
        if kind != expected
            || (kind != 3 && !text.is_empty())
            || (kind == 3 && bits != 0)
            || (kind == 1 && bits > u32::MAX as u64)
        {
            return Err(Error::Protocol);
        }
        if text.len() > i32::MAX as usize
            || remainder.len() > i32::MAX as usize
            || (field == 2
                && text.len() as u64 * 2 + crate::document_style::HEADER_BYTES as u64
                    > u32::MAX as u64)
        {
            return Err(Error::CountOverflow);
        }
        let supplied = match kind {
            1 => style.integer(field, bits as u32 as i32),
            2 => style.float(field, f64::from_bits(bits)),
            3 => style.text(field, text),
            _ => unreachable!(),
        };
        if !supplied {
            return Err(Error::Protocol);
        }
        style.accept(remainder);
        self.style_field = style.next_field();
        if self.style_field == 27 {
            self.style_field = style.next_field();
        }
        if self.style_field == 0 && !style.finish() {
            return Err(Error::Protocol);
        }
        Ok(())
    }
    pub fn style_part(&self, group: u32) -> Result<alloc::vec::Vec<u8>, Error> {
        let style = self.style.as_ref().ok_or(Error::Protocol)?;
        if group == 2 && self.style_field == 0 {
            return Ok(style.result().to_vec());
        }
        let text = match group {
            0 => {
                if self.style_field == 0 {
                    &[][..]
                } else {
                    style.input()
                }
            }
            1 if self.style_field == 0 => style.name(),
            _ => return Err(Error::Protocol),
        };
        let mut bytes = alloc::vec::Vec::new();
        bytes.extend_from_slice(&self.style_field.to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());
        for unit in text {
            bytes.extend_from_slice(&unit.to_le_bytes());
        }
        Ok(bytes)
    }
    /// Native copied the final value and name; no native pointer is transferred.
    /// Release the parsing session before the existing AddStyle ownership handoff.
    pub fn end_style(&mut self) -> Result<(), Error> {
        if self.style_field != 0 || self.style.as_ref().is_none_or(|s| s.result().is_empty()) {
            return Err(Error::Protocol);
        }
        self.style = None;
        self.pending = false;
        Ok(())
    }

    pub fn begin_dialogue(&mut self, input: &[u16]) -> Result<(), Error> {
        if self.kind != 1 || !self.pending || self.dialogue.is_some() {
            return Err(Error::Protocol);
        }
        let mut dialogue = crate::ass_dialogue::Dialogue::new(input, self.headers.version);
        dialogue.step(0);
        self.dialogue = Some(dialogue);
        Ok(())
    }
    pub fn dialogue_number(&mut self, value: i32) -> Result<(), Error> {
        let dialogue = self.dialogue.as_mut().ok_or(Error::Protocol)?;
        if dialogue.complete() {
            return Err(Error::Protocol);
        }
        dialogue.step(value);
        Ok(())
    }
    /// Owned snapshot: request:u32, separator:u32, followed by raw UTF-16LE.
    /// Group 0 exposes the next token (or completion), 1 the stripped style,
    /// 4 the pre-trim text. Style/text queries require all numbers supplied.
    pub fn dialogue_part(&self, group: u32) -> Result<alloc::vec::Vec<u8>, Error> {
        let dialogue = self.dialogue.as_ref().ok_or(Error::Protocol)?;
        let text = match group {
            0 => {
                if dialogue.complete() {
                    &[][..]
                } else {
                    dialogue.string(0)
                }
            }
            1 if dialogue.complete() => dialogue.style_name(),
            4 if dialogue.complete() => dialogue.string(4),
            _ => return Err(Error::Protocol),
        };
        let mut bytes = alloc::vec::Vec::new();
        bytes.extend_from_slice(&u32::from(!dialogue.complete()).to_le_bytes());
        bytes.extend_from_slice(&u32::from(dialogue.separator).to_le_bytes());
        for unit in text {
            bytes.extend_from_slice(&unit.to_le_bytes());
        }
        Ok(bytes)
    }
    pub fn append_dialogue(
        &mut self,
        document: &mut Document,
        trimmed_text: &[u16],
        unicode: bool,
        default_style: bool,
    ) -> Result<(), Error> {
        let dialogue = self.dialogue.as_ref().ok_or(Error::Protocol)?;
        if !dialogue.complete() {
            return Err(Error::Protocol);
        }
        if let Some(entry) = dialogue
            .record(trimmed_text, unicode, default_style)
            .map_err(Error::Preparation)?
        {
            if document.entries().len() >= i32::MAX as usize {
                return Err(Error::Storage(crate::document::Refusal::EntryCountOverflow));
            }
            document
                .add_prepared(entry, crate::document::AddMode::EntryOnly)
                .map_err(Error::Storage)?;
        }
        self.dialogue = None;
        self.pending = false;
        Ok(())
    }

    /// Preserve partial header effects, including the entire size reset when
    /// either numeric axis fails. Error replies are terminal parser failures.
    pub fn number(&mut self, document: &mut Document, value: Option<i32>) -> Result<(), Error> {
        if !self.pending || !(4..=8).contains(&self.kind) {
            return Err(Error::Protocol);
        }
        let Some(value) = value else {
            match self.kind {
                4 | 5 => document.set_script_size(0, 0),
                6 | 7 => document.set_layout_size(0, 0),
                8 => {
                    document.replace_script_field(ScriptField::Wrap, 1);
                }
                _ => unreachable!(),
            }
            self.failed = true;
            self.pending = false;
            return Ok(());
        };
        match self.kind {
            4 | 5 => {
                let (own, other, special, fallback, multiplier, divisor) = if self.kind == 4 {
                    (ScriptField::Width, ScriptField::Height, 1280, 1024, 3, 4)
                } else {
                    (ScriptField::Height, ScriptField::Width, 1024, 1280, 4, 3)
                };
                // Validate before mutation. An unsupplied native scalar is an
                // unresolved outcome, not zero or an invented aspect ratio.
                let previous = document
                    .script_field(other)
                    .ok_or(Error::UnsuppliedScreen)?;
                document.replace_script_field(own, value);
                if previous <= 0 {
                    let inferred = if value == special {
                        fallback
                    } else {
                        value.wrapping_mul(multiplier) / divisor
                    };
                    document.replace_script_field(other, inferred);
                }
            }
            6 => {
                document.replace_layout_field(LayoutField::Width, value);
            }
            7 => {
                document.replace_layout_field(LayoutField::Height, value);
            }
            8 => {
                document.replace_script_field(ScriptField::Wrap, value);
            }
            _ => unreachable!(),
        }
        self.pending = false;
        Ok(())
    }

    pub fn script_version(&mut self, detected: i32) -> Result<(), Error> {
        if !self.pending || self.kind != 9 || !matches!(detected, 0 | 4..=6) {
            return Err(Error::Protocol);
        }
        self.headers.script_version(detected);
        self.pending = false;
        Ok(())
    }

    pub fn text(&mut self, document: &mut Document, lowered_value: &[u16]) -> Result<(), Error> {
        if !self.pending || !matches!(self.kind, 10 | 11 | 17) {
            return Err(Error::Protocol);
        }
        match self.kind {
            10 => {
                document.replace_script_field(
                    ScriptField::Collisions,
                    ass_headers::value(lowered_value, 0) as i32,
                );
            }
            11 => {
                document.replace_scaled_bas(ass_headers::value(lowered_value, 1) != 0);
            }
            17 => {
                let pair = match ass_headers::value(lowered_value, 2) {
                    1 => Some((3, 2)), // AUTO/AUTO
                    2 => Some((0, 1)),
                    3 => Some((1, 1)),
                    4 => Some((2, 1)),
                    5 => Some((0, 0)),
                    6 => Some((1, 0)),
                    7 => Some((2, 0)),
                    _ => None, // unrecognized values retain both prior fields
                };
                if let Some((matrix, range)) = pair {
                    document.replace_metadata(MetadataField::ColorMatrix, matrix);
                    document.replace_metadata(MetadataField::ColorRange, range);
                }
            }
            _ => unreachable!(),
        }
        self.pending = false;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn owned_input_requires_classification_and_completed_record_before_next_read() {
        let mut doc = Document::default();
        let mut run = Run::begin(&mut doc);
        let line = units("[Events]");
        run.read_line(&line, &alloc::vec![0;line.len()], false)
            .unwrap();
        assert_eq!(run.end_record(true), Err(Error::Protocol));
        classify(&mut run, "[events]");
        assert_eq!(run.classify(&units("dialogue")), Err(Error::Protocol));
        assert_eq!(run.read_line(&[], &[], true), Err(Error::Protocol));
        run.end_record(true).unwrap();
        let line = units("PlayResX: bad");
        let spaces = alloc::vec![0;line.len()];
        run.read_line(&line, &spaces, true).unwrap();
        classify(&mut run, "playresx");
        assert_eq!(run.end_record(true), Err(Error::Protocol));
        run.number(&mut doc, None).unwrap();
        assert_eq!(run.end_record(true), Err(Error::Protocol));
        run.end_record(false).unwrap();
        assert_eq!(&run.input_part(0).unwrap()[..4], &3u32.to_le_bytes());
        assert_eq!(run.state()[3], 0);
        assert_eq!(doc.script_size(), Some([0, 0]));
        assert_eq!(run.read_line(&[], &[], true), Err(Error::Protocol));
    }

    #[test]
    fn typed_style_values_require_order_and_owned_results_survive_session_release() {
        for version in 3..=6 {
            let mut doc = Document::default();
            let mut run = Run::begin(&mut doc);
            if version >= 4 {
                classify(&mut run, "scripttype");
                run.script_version(version).unwrap();
            }
            classify(&mut run, "style");
            run.begin_style(&units("native remainder")).unwrap();
            assert_eq!(run.end_style(), Err(Error::Protocol));
            assert_eq!(run.style_part(2), Err(Error::Protocol));
            assert_eq!(run.classify(&units("[events]")), Err(Error::Protocol));
            let before = run.style_part(0).unwrap();
            assert_eq!(run.style_value(1, 99, &[], &[]), Err(Error::Protocol));
            assert_eq!(run.style_part(0).unwrap(), before);
            loop {
                let part = run.style_part(0).unwrap();
                let field = u32::from_le_bytes(part[..4].try_into().unwrap());
                if field == 0 {
                    break;
                }
                assert_ne!(field, 27);
                let mut raw = alloc::vec![42, 65, 0, 0xd800];
                match field {
                    1 | 2 => run.style_value(3, 0, &raw, &units("rest")).unwrap(),
                    3 | 12..=15 | 17 | 18 => run
                        .style_value(2, (-0.0f64).to_bits(), &[], &units("rest"))
                        .unwrap(),
                    _ => run
                        .style_value(1, u32::MAX.into(), &[], &units("rest"))
                        .unwrap(),
                }
                raw.fill(9);
            }
            let value = run.style_part(2).unwrap();
            let name = run.style_part(1).unwrap();
            run.end_style().unwrap();
            assert_eq!(run.style_part(2), Err(Error::Protocol));
            classify(&mut run, "[events]");
            let decoded = crate::document_style::decode(&value).unwrap();
            assert_eq!(decoded.font_name, [42, 65, 0, 0xd800]);
            assert_eq!(&name[8..], &[65, 0, 0, 0, 0, 0xd8]);
            assert_eq!(decoded.font_size.to_bits(), (-0.0f64).to_bits());
            assert_eq!(
                decoded.font_scale_x.to_bits(),
                if version >= 5 { 0 } else { 100f64.to_bits() }
            );
            assert_eq!(decoded.char_set, 1);
        }
    }
    fn units(text: &str) -> alloc::vec::Vec<u16> {
        text.encode_utf16().collect()
    }
    fn classify(run: &mut Run, text: &str) {
        run.classify(&units(text)).unwrap();
    }

    #[test]
    fn independent_versions_acceptance_and_failed_numeric_resets() {
        for (key, kind) in [
            ("playresx", 4),
            ("playresy", 5),
            ("layoutresx", 6),
            ("layoutresy", 7),
            ("wrapstyle", 8),
        ] {
            let mut doc = Document::default();
            doc.set_script_size(640, 480);
            doc.set_layout_size(1920, 1080);
            doc.replace_script_field(ScriptField::Wrap, 2);
            let mut run = Run::begin(&mut doc);
            classify(&mut run, "scripttype");
            run.script_version(6).unwrap();
            classify(&mut run, "[v4 styles]");
            assert_eq!(run.state(), [12, 6, 4, 1]);
            classify(&mut run, key);
            run.number(&mut doc, None).unwrap();
            assert_eq!(run.state(), [kind, 6, 4, 0]);
            assert_eq!(
                doc.script_size(),
                Some(if kind <= 5 { [0, 0] } else { [640, 480] })
            );
            assert_eq!(
                doc.layout_size(),
                Some(if kind == 6 || kind == 7 {
                    [0, 0]
                } else {
                    [1920, 1080]
                })
            );
            assert_eq!(
                doc.script_field(ScriptField::Wrap),
                Some(if kind == 8 { 1 } else { 2 })
            );
            assert_eq!(run.classify(&units("[events]")), Err(Error::Protocol));
        }
    }

    #[test]
    fn resolution_updates_use_live_other_axis_and_windows_integer_order() {
        for (key, value, expected) in [
            ("playresx", 1280, [1280, 1024]),
            ("playresy", 1024, [1280, 1024]),
            ("playresx", -7, [-7, -5]),
            ("playresy", -7, [-9, -7]),
            ("playresx", i32::MAX, [i32::MAX, 536870911]),
            ("playresy", i32::MAX, [-1, i32::MAX]),
        ] {
            let mut doc = Document::default();
            doc.set_script_size(0, 0);
            let mut run = Run::begin(&mut doc);
            classify(&mut run, key);
            run.number(&mut doc, Some(value)).unwrap();
            assert_eq!(doc.script_size(), Some(expected));
        }
        let mut doc = Document::default();
        doc.set_script_size(0, 0);
        let mut run = Run::begin(&mut doc);
        classify(&mut run, "playresx");
        doc.replace_script_field(ScriptField::Height, 777);
        run.number(&mut doc, Some(1280)).unwrap();
        assert_eq!(doc.script_size(), Some([1280, 777]));
    }

    #[test]
    fn invalid_protocol_is_atomic_and_unresolved_can_be_supplied() {
        let mut doc = Document::default();
        let mut run = Run::begin(&mut doc);
        classify(&mut run, "playresx");
        let before = doc.clone();
        assert_eq!(run.number(&mut doc, Some(10)), Err(Error::UnsuppliedScreen));
        assert_eq!(doc, before);
        assert_eq!(run.text(&mut doc, &units("yes")), Err(Error::Protocol));
        assert_eq!(run.classify(&units("[events]")), Err(Error::Protocol));
        assert_eq!(run.script_version(5), Err(Error::Protocol));
        assert_eq!(doc, before);
        doc.set_script_size(0, 0);
        run.number(&mut doc, Some(10)).unwrap();
        assert_eq!(doc.script_size(), Some([10, 7]));
    }

    #[test]
    fn text_values_retain_prefix_substring_and_nul_behavior() {
        let mut doc = Document::default();
        let mut run = Run::begin(&mut doc);
        for (value, pair) in [
            ("none", [3, 2]),
            ("tv.601junk", [0, 1]),
            ("tv.709", [1, 1]),
            ("tv.2020", [2, 1]),
            ("pc.601", [0, 0]),
            ("pc.709", [1, 0]),
            ("pc.2020", [2, 0]),
            ("unknown", [2, 0]),
            ("none\0tv.601", [3, 2]),
        ] {
            classify(&mut run, "ycbcr matrix");
            run.text(&mut doc, &units(value)).unwrap();
            assert_eq!(
                [
                    doc.metadata(MetadataField::ColorMatrix),
                    doc.metadata(MetadataField::ColorRange)
                ],
                pair.map(Some)
            );
        }
        classify(&mut run, "collisions");
        run.text(&mut doc, &units("unreversed")).unwrap();
        assert_eq!(doc.script_field(ScriptField::Collisions), Some(1));
        classify(&mut run, "scaledborderandshadow");
        run.text(&mut doc, &units("no\0yes")).unwrap();
        assert_eq!(doc.scaled_bas(), Some(false));
        assert_eq!(run.state()[3], 0); // values alone never accept an ASS file
    }

    #[test]
    fn dialogue_commit_preserves_prefix_effect_zero_duration_and_entry_only_timeline() {
        let mut doc = Document::default();
        let mut run = Run::begin(&mut doc);
        classify(&mut run, "scripttype");
        run.script_version(6).unwrap();
        classify(&mut run, "[events]");
        for (text, expected) in [("  shared body  ", "shared body"), ("   ", "")] {
            classify(&mut run, "dialogue");
            let body = units(&alloc::format!(
                "9,0:00:01.20,0:00:01.20,**dEfAuLt,Actor,1,2,3,4,shared,{text}"
            ));
            run.begin_dialogue(&body).unwrap();
            assert_eq!(run.dialogue_part(1), Err(Error::Protocol));
            assert_eq!(
                run.append_dialogue(&mut doc, &units(expected), true, true),
                Err(Error::Protocol)
            );
            for number in [9, 0, 0, 1, 20, 0, 0, 1, 20, 1, 2, 3, 4] {
                run.dialogue_number(number).unwrap();
            }
            assert_eq!(run.dialogue_number(999), Err(Error::Protocol));
            assert_eq!(
                &run.dialogue_part(1).unwrap()[8..],
                &units("dEfAuLt")
                    .iter()
                    .flat_map(|u| u.to_le_bytes())
                    .collect::<alloc::vec::Vec<_>>()
            );
            assert_eq!(run.classify(&units("[events]")), Err(Error::Protocol));
            run.append_dialogue(&mut doc, &units(expected), true, true)
                .unwrap();
            assert_eq!(
                run.append_dialogue(&mut doc, &units(expected), true, true),
                Err(Error::Protocol)
            );
        }
        assert_eq!(doc.entries().len(), 1);
        assert!(doc.segments().is_empty());
        let entry = &doc.entries()[0];
        assert_eq!(
            (entry.start, entry.end, entry.layer, entry.readorder),
            (12_000_000, 12_000_000, 9, 0)
        );
        assert_eq!(entry.margins, [1, 3, 2, 4]);
        assert_eq!(entry.style, units("Default"));
        // Effect comparison is against pre-trim text, so the leading spaces
        // make this one different even though trimmed text starts with shared.
        assert_eq!(entry.effect, units("shared"));
        assert_eq!(entry.text, units("shared body"));
        assert_eq!(entry.actor, units("Actor"));
        assert!(entry.unicode);
    }

    #[test]
    fn dialogue_prefix_effect_legacy_marked_and_safe_machine_time_arithmetic() {
        for version in 3..=6 {
            for (effect, text, expected_effect) in [
                ("same", "same tail", ""),
                ("same tail", "same", ""),
                ("effect", "text", "effect"),
                ("", "text", ""),
            ] {
                let mut doc = Document::default();
                let mut run = Run::begin(&mut doc);
                if version >= 4 {
                    classify(&mut run, "scripttype");
                    run.script_version(version).unwrap();
                }
                classify(&mut run, "dialogue");
                let first = if version <= 4 { "Marked=9" } else { "9" };
                let bottom = if version >= 6 { "4," } else { "" };
                run.begin_dialogue(&units(&alloc::format!(
                    "{first},0:00:01.20,0:00:01.20,***,Actor,1,2,3,{bottom}{effect},{text}"
                )))
                .unwrap();
                for number in [9, 0, 0, 1, 20, 0, 0, 1, 20, 1, 2, 3] {
                    run.dialogue_number(number).unwrap();
                }
                if version >= 6 {
                    run.dialogue_number(4).unwrap();
                }
                run.append_dialogue(&mut doc, &units(text), false, false)
                    .unwrap();
                let entry = &doc.entries()[0];
                assert_eq!(entry.effect, units(expected_effect));
                assert_eq!(entry.style, units("Default"));
                assert_eq!(entry.layer, if version <= 4 { 0 } else { 9 });
                assert_eq!(entry.margins[3], if version >= 6 { 4 } else { 3 });
            }
        }
    }
}
