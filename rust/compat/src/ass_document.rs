// SPDX-License-Identifier: GPL-2.0-or-later
//! ASS/SSA header state and document mutations from cleanup STS.cpp.
//!
//! Native adapters still read/trim/lowercase code units, perform CRT numeric
//! conversion and suffix comparison, and execute dialogue/style/font actions.
//! No file, native object, or caller buffer is retained by this session.
use crate::ass_headers::{self, Headers};
use crate::document::{Document, LayoutField, MetadataField, ScriptField};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Protocol,
    UnsuppliedScreen,
    Preparation(crate::document_prepare::Error),
    Storage(crate::document::Refusal),
}

#[derive(Default)]
pub struct Run {
    headers: Headers,
    kind: u32,
    pending: bool,
    failed: bool,
    dialogue: Option<crate::ass_dialogue::Dialogue>,
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
        if self.pending || self.failed {
            return Err(Error::Protocol);
        }
        self.kind = self.headers.classify(lowered_key);
        self.pending = matches!(self.kind, 1 | 4..=11 | 17);
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
