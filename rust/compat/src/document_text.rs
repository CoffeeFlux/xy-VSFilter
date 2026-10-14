// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned DOC05 text-operation selection from pinned STS.cpp:3070–3116.
//!
//! Native adapters perform SSA/codepage conversion and tag removal using the
//! returned owned raw units, without any Rust borrow or generation guard.
//! Conversion commits text before the requested flag; getters never mutate.
//! SetStr's CString LF preparation is deliberately outside this module.
//! Native allocation/partial-failure, codepage quirks and allocator reentry
//! remain qualified runtime dependencies, not certified by plan selection.
use crate::document::{Document, ResolvedStyle, StyleResolutionError};
use alloc::vec::Vec;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Mode {
    GetWideSsa = 0,
    GetWideStrip = 1,
    GetPackedSsa = 2,
    GetPackedStrip = 3,
    ConvertWide = 4,
    ConvertPacked = 5,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Action {
    Keep = 0,
    ToWide = 1,
    ToPacked = 2,
    NoOp = 3,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Plan {
    pub action: Action,
    pub target_unicode: bool,
    pub strip_tags: bool,
    pub has_style: bool,
    pub charset: i32,
    pub text: Vec<u16>,
}
/// Entry access precedes even forced style selection. Convert same-flag returns
/// before charset resolution or text cloning; getters always resolve charset.
/// Null style retains a distinct tag/-1 for the native GetCharSet assertion and
/// release behavior. NoOp's false/zero style fields are unused placeholders.
pub fn plan(document: &Document, index: u32, mode: Mode) -> Result<Plan, StyleResolutionError> {
    let entry = document
        .entries()
        .get(index as usize)
        .ok_or(StyleResolutionError::EntryIndex)?;
    let target_unicode = matches!(
        mode,
        Mode::GetWideSsa | Mode::GetWideStrip | Mode::ConvertWide
    );
    let strip_tags = matches!(mode, Mode::GetWideStrip | Mode::GetPackedStrip);
    let convert = matches!(mode, Mode::ConvertWide | Mode::ConvertPacked);
    if convert && entry.unicode == target_unicode {
        return Ok(Plan {
            action: Action::NoOp,
            target_unicode,
            strip_tags: false,
            has_style: false,
            charset: 0,
            text: Vec::new(),
        });
    }
    let (has_style, charset) = match document.resolve_entry_style(index)? {
        ResolvedStyle::Null => (false, -1),
        ResolvedStyle::Value(style) => (true, style.char_set),
    };
    let action = if entry.unicode == target_unicode {
        Action::Keep
    } else if target_unicode {
        Action::ToWide
    } else {
        Action::ToPacked
    };
    Ok(Plan {
        action,
        target_unicode,
        strip_tags,
        has_style,
        charset,
        text: entry.text.clone(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::document::{AddMode, PreparedEntry};
    use alloc::vec;
    fn entry(unicode: bool) -> PreparedEntry {
        PreparedEntry {
            text: vec![65, 0, 0xd800, 13, 10, 66],
            style: vec![65],
            actor: vec![0xdcff],
            effect: vec![88],
            unicode,
            margins: [-1, 2, 3, 4],
            layer: 0,
            start: 0,
            end: 10,
            readorder: -1,
        }
    }
    fn style(charset: i32) -> crate::document_style::StyleValue {
        let mut bytes = vec![0; 192];
        bytes[..4].copy_from_slice(&charset.to_le_bytes());
        crate::document_style::decode(&bytes).unwrap()
    }
    fn supplied() -> Document {
        let mut document = Document::default();
        document.replace_forced_default(false);
        document.replace_default_style(Some(style(128)));
        document
    }

    #[test]
    fn conversion_noop_precedes_unsupplied_resolution_and_retains_animation() {
        for (unicode, mode) in [(true, Mode::ConvertWide), (false, Mode::ConvertPacked)] {
            let mut document = Document::default();
            document.add_prepared(entry(unicode), AddMode::Add).unwrap();
            document.feedback_animation(0, true).unwrap();
            let before = document.clone();
            let operation = plan(&document, 0, mode).unwrap();
            assert_eq!(operation.action, Action::NoOp);
            assert!(operation.text.is_empty());
            assert!(!operation.has_style);
            assert_eq!(operation.charset, 0);
            assert_eq!(document, before);
            let getter = if unicode {
                Mode::GetWideSsa
            } else {
                Mode::GetPackedSsa
            };
            assert_eq!(
                plan(&document, 0, getter),
                Err(StyleResolutionError::Uninitialized)
            );
        }
    }
    #[test]
    fn six_modes_select_exact_conversion_and_strip_matrix() {
        let modes = [
            Mode::GetWideSsa,
            Mode::GetWideStrip,
            Mode::GetPackedSsa,
            Mode::GetPackedStrip,
            Mode::ConvertWide,
            Mode::ConvertPacked,
        ];
        for (unicode, actions) in [
            (
                true,
                [
                    Action::Keep,
                    Action::Keep,
                    Action::ToPacked,
                    Action::ToPacked,
                    Action::NoOp,
                    Action::ToPacked,
                ],
            ),
            (
                false,
                [
                    Action::ToWide,
                    Action::ToWide,
                    Action::Keep,
                    Action::Keep,
                    Action::ToWide,
                    Action::NoOp,
                ],
            ),
        ] {
            let mut document = supplied();
            document.add_prepared(entry(unicode), AddMode::Add).unwrap();
            let before = document.clone();
            for (mode, action) in modes.into_iter().zip(actions) {
                let operation = plan(&document, 0, mode).unwrap();
                assert_eq!(operation.action, action);
                assert_eq!(
                    operation.strip_tags,
                    matches!(mode, Mode::GetWideStrip | Mode::GetPackedStrip)
                );
                if action != Action::NoOp {
                    assert!(operation.has_style);
                    assert_eq!(operation.charset, 128);
                    assert_eq!(operation.text, entry(unicode).text);
                }
            }
            assert_eq!(document, before);
        }
    }
    #[test]
    fn entry_check_precedes_forced_fallback_and_getters_always_resolve() {
        let mut document = Document::default();
        assert_eq!(
            plan(&document, 0, Mode::GetWideSsa),
            Err(StyleResolutionError::EntryIndex)
        );
        document.replace_forced_default(true);
        document.replace_default_style(Some(style(0)));
        assert_eq!(
            plan(&document, u32::MAX, Mode::ConvertPacked),
            Err(StyleResolutionError::EntryIndex)
        );
        document.add_prepared(entry(true), AddMode::Add).unwrap();
        document.replace_default_style(None);
        assert_eq!(
            plan(&document, 0, Mode::GetWideSsa),
            Err(StyleResolutionError::Uninitialized)
        );
        assert_eq!(
            plan(&document, 0, Mode::ConvertPacked),
            Err(StyleResolutionError::Uninitialized)
        );
        assert_eq!(
            plan(&document, 0, Mode::ConvertWide).unwrap().action,
            Action::NoOp
        );
    }
    #[test]
    fn owned_raw_reply_and_null_style_tag_do_not_mutate_document() {
        let mut document = supplied();
        document.add_prepared(entry(true), AddMode::Add).unwrap();
        let before = document.clone();
        let mut operation = plan(&document, 0, Mode::GetWideSsa).unwrap();
        operation.text[2] = 0xdcff;
        operation.text.push(0xffff);
        assert_eq!(
            plan(&document, 0, Mode::GetWideSsa).unwrap().text,
            entry(true).text
        );
        assert_eq!(document, before);
        // Safe-only null model; no native dereference/assertion history executed.
        document.named_styles_mut().insert(vec![65], None);
        let operation = plan(&document, 0, Mode::GetWideStrip).unwrap();
        assert!(!operation.has_style);
        assert_eq!(operation.charset, -1);
        assert_eq!(operation.action, Action::Keep);
    }
}
