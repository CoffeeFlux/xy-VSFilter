// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned DOC05 text-operation selection from pinned STS.cpp:3070–3127.
//!
//! Native adapters perform SSA/codepage conversion and tag removal using the
//! returned owned raw units, without any Rust borrow or generation guard.
//! Conversion commits text before the requested flag; getters never mutate.
//! SetStr applies the admitted LF-only helper before conditional charset
//! selection, retains CR and commits text only without changing the stored flag.
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

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WriteError {
    Entry(StyleResolutionError),
    Preparation(crate::document_prepare::Error),
}

/// Pinned SetStr entry access precedes LF-only replacement, which precedes any
/// conditional charset resolution. Equal input/stored flags still prepare text
/// but skip the resolver; this is Keep, never NoOp. Native converters remain
/// outside the loan, and the caller commits text ONLY (retaining current flag).
/// Temporary destruction and allocation/exception timing remain qualified.
pub fn plan_write(
    document: &Document,
    index: u32,
    input_unicode: bool,
    raw_units: &[u16],
) -> Result<Plan, WriteError> {
    let entry = document
        .entries()
        .get(index as usize)
        .ok_or(WriteError::Entry(StyleResolutionError::EntryIndex))?;
    let text = crate::document_prepare::replace_lf(raw_units).map_err(WriteError::Preparation)?;
    let target_unicode = entry.unicode;
    if input_unicode == target_unicode {
        return Ok(Plan {
            action: Action::Keep,
            target_unicode,
            strip_tags: false,
            has_style: false,
            charset: 0,
            text,
        });
    }
    let (has_style, charset) = match document
        .resolve_entry_style(index)
        .map_err(WriteError::Entry)?
    {
        ResolvedStyle::Null => (false, -1),
        ResolvedStyle::Value(style) => (true, style.char_set),
    };
    Ok(Plan {
        action: if target_unicode {
            Action::ToWide
        } else {
            Action::ToPacked
        },
        target_unicode,
        strip_tags: false,
        has_style,
        charset,
        text,
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
    #[test]
    fn write_plan_equal_flags_prepare_raw_units_without_style_resolution() {
        for unicode in [false, true] {
            let mut document = Document::default();
            document.add_prepared(entry(unicode), AddMode::Add).unwrap();
            document.feedback_animation(0, true).unwrap();
            let before = document.clone();
            let raw = [0xd800, 13, 10, 0, 0xdcff];
            let operation = plan_write(&document, 0, unicode, &raw).unwrap();
            assert_eq!(
                operation,
                Plan {
                    action: Action::Keep,
                    target_unicode: unicode,
                    strip_tags: false,
                    has_style: false,
                    charset: 0,
                    text: vec![0xd800, 13, 92, 78, 0, 0xdcff]
                }
            );
            assert_eq!(document, before);
            assert_eq!(
                plan_write(&document, 0, unicode, &[]).unwrap().text,
                Vec::<u16>::new()
            );
        }
    }
    #[test]
    fn write_plan_conversion_targets_stored_encoding_and_preserves_owner() {
        for (stored, input, action) in [
            (true, false, Action::ToWide),
            (false, true, Action::ToPacked),
        ] {
            let mut document = supplied();
            document.add_prepared(entry(stored), AddMode::Add).unwrap();
            document.feedback_animation(0, true).unwrap();
            let before = document.clone();
            let mut operation = plan_write(&document, 0, input, &[65, 13, 10, 66]).unwrap();
            assert_eq!(operation.action, action);
            assert_eq!(operation.target_unicode, stored);
            assert!(!operation.strip_tags);
            assert!(operation.has_style);
            assert_eq!(operation.charset, 128);
            assert_eq!(operation.text, [65, 13, 92, 78, 66]);
            operation.text[0] = 90;
            assert_eq!(document, before);
            assert_eq!(document.entries()[0].unicode, stored);
        }
    }
    #[test]
    fn write_plan_error_precedence_entry_then_tail_then_charset() {
        let mut document = Document::default();
        let unresolved = [10, 10, 0, 10, 10];
        assert_eq!(
            plan_write(&document, 0, false, &unresolved),
            Err(WriteError::Entry(StyleResolutionError::EntryIndex))
        );
        document.add_prepared(entry(true), AddMode::Add).unwrap();
        let before = document.clone();
        assert_eq!(
            plan_write(&document, 0, false, &unresolved),
            Err(WriteError::Preparation(
                crate::document_prepare::Error::UnresolvedTail
            ))
        );
        assert_eq!(
            plan_write(&document, 0, false, &[10]),
            Err(WriteError::Entry(StyleResolutionError::Uninitialized))
        );
        assert_eq!(
            plan_write(&document, 0, true, &[10]).unwrap().action,
            Action::Keep
        );
        assert_eq!(document, before);
    }
    #[test]
    fn write_plan_bounded_skipped_lf_retains_initialized_tail_and_null_tag() {
        let mut document = supplied();
        document.add_prepared(entry(true), AddMode::Add).unwrap();
        let before = document.clone();
        let operation = plan_write(&document, 0, true, &[88, 10, 10, 0, 10, 89]).unwrap();
        assert_eq!(operation.text, [88, 92, 78, 92, 78, 0, 10, 89, 0]);
        assert_eq!(document, before);
        // Safe-only present-null release model; no native dereference/history.
        document.named_styles_mut().insert(vec![65], None);
        let operation = plan_write(&document, 0, false, &[13, 10]).unwrap();
        assert_eq!(operation.action, Action::ToWide);
        assert!(!operation.has_style);
        assert_eq!(operation.charset, -1);
        assert_eq!(operation.text, [13, 92, 78]);
    }
}
