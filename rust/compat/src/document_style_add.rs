// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned AddStyle classification and commit protocol from pinned STS.cpp:2741.
//! Native code retains digit classification, suffix parsing, CRT formatting and
//! original temporary-pointer deletion. No callback or reference crosses calls.
//! Allocation/partial-failure and map-key/node destruction timing remain outside
//! this bounded contract; native null dereferences/shared-pointee histories are
//! unresolved rather than emulated. No generations or reclassification policy.
use crate::document::{Document, RetiredStyleRename, StyleRenameError};
use crate::document_style::StyleValue;
use alloc::vec::Vec;

const DEFAULT: &[u16] = &[68, 101, 102, 97, 117, 108, 116];
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    InvalidPreparedValue,
    UnresolvedNull,
    CountOverflow,
    Finished,
}
#[derive(Debug, Default)]
pub struct Retired {
    pub relocation: Option<RetiredStyleRename>,
    pub discarded_incoming: Option<StyleValue>,
}
// Inline outcome avoids introducing a fallible Box allocation after commit.
#[allow(clippy::large_enum_variant)]
#[derive(Debug)]
pub enum Action {
    Done,
    Conflict(Session),
}
#[derive(Debug)]
pub struct Begin {
    pub action: Action,
    pub normalized_name: Vec<u16>,
    pub retired: Retired,
}
#[derive(Clone, Debug, PartialEq)]
pub struct Session {
    name: Vec<u16>,
    previous: Option<StyleValue>,
    incoming: Option<StyleValue>,
    finished: bool,
}
#[derive(Debug)]
pub struct Submit {
    pub applied: bool,
    pub retired: Option<RetiredStyleRename>,
}

/// Outer pending Option is consumed only on success; inner None is null style.
/// Logical empty names normalize to Default, but leading-NUL nonempty names do
/// not. Present-null and existing-value/null-incoming comparisons are unresolved.
pub fn begin(
    document: &mut Document,
    name: &[u16],
    incoming: &mut Option<Option<StyleValue>>,
) -> Result<Begin, Error> {
    if incoming.is_none() {
        return Err(Error::InvalidPreparedValue);
    }
    if name.len() > i32::MAX as usize {
        return Err(Error::CountOverflow);
    }
    let normalized_name = if name.is_empty() {
        DEFAULT.to_vec()
    } else {
        name.to_vec()
    };
    let mut retired = Retired::default();
    let action = match document.named_styles().get(&normalized_name) {
        None => {
            if document.named_styles().count() >= i32::MAX as usize {
                return Err(Error::CountOverflow);
            }
            let value = incoming.take().ok_or(Error::InvalidPreparedValue)?;
            // Classification and insertion share admission; there is no callback.
            let old = document
                .named_styles_mut()
                .insert(normalized_name.clone(), value);
            debug_assert!(old.is_none());
            Action::Done
        }
        Some(previous) => {
            let previous = previous.as_ref().ok_or(Error::UnresolvedNull)?;
            let value = incoming
                .as_ref()
                .and_then(Option::as_ref)
                .ok_or(Error::UnresolvedNull)?;
            if previous.native_compatible_eq(value) {
                retired.discarded_incoming = incoming.take().ok_or(Error::InvalidPreparedValue)?;
                Action::Done
            } else {
                let previous = Some(previous.clone());
                let value = incoming.take().ok_or(Error::InvalidPreparedValue)?;
                Action::Conflict(Session {
                    name: normalized_name.clone(),
                    previous,
                    incoming: value,
                    finished: false,
                })
            }
        }
    };
    Ok(Begin {
        action,
        normalized_name,
        retired,
    })
}
impl Session {
    /// Occupied candidates, including null nodes and aliases of old key, do not
    /// advance this session or mutate the document. Native CRT supplies next.
    pub fn submit(&mut self, document: &mut Document, candidate: &[u16]) -> Result<Submit, Error> {
        if self.finished {
            return Err(Error::Finished);
        }
        if candidate.len() > i32::MAX as usize {
            return Err(Error::CountOverflow);
        }
        if document.named_styles().get(candidate).is_some() {
            return Ok(Submit {
                applied: false,
                retired: None,
            });
        }
        let retired = document
            .commit_style_conflict(
                &self.name,
                candidate,
                &mut self.previous,
                &mut self.incoming,
            )
            .map_err(|error| match error {
                StyleRenameError::CountOverflow => Error::CountOverflow,
                StyleRenameError::InvalidPreparedValue => Error::InvalidPreparedValue,
            })?;
        self.finished = true;
        Ok(Submit {
            applied: true,
            retired: Some(retired),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::document::{AddMode, PreparedEntry};
    use alloc::vec;
    fn value(id: u8) -> StyleValue {
        let mut wire = vec![0; 192];
        wire[0] = id;
        crate::document_style::decode(&wire).unwrap()
    }
    fn name(value: &str) -> Vec<u16> {
        value.encode_utf16().collect()
    }
    fn add_entry(document: &mut Document, style: Vec<u16>) {
        document
            .add_prepared(
                PreparedEntry {
                    text: vec![65, 0, 0xd800],
                    style,
                    actor: vec![0xdcff],
                    effect: vec![88],
                    unicode: true,
                    margins: [-1, 2, 3, 4],
                    layer: 2,
                    start: 0,
                    end: 10,
                    readorder: -1,
                },
                AddMode::Add,
            )
            .unwrap();
        document.feedback_animation(0, true).unwrap();
    }
    fn session(outcome: Begin) -> Session {
        match outcome.action {
            Action::Conflict(run) => run,
            Action::Done => panic!("expected unequal conflict"),
        }
    }

    #[test]
    fn logical_empty_normalization_and_missing_null_are_distinct() {
        let mut document = Document::default();
        let mut incoming = Some(Some(value(1)));
        let outcome = begin(&mut document, &[], &mut incoming).unwrap();
        assert!(matches!(outcome.action, Action::Done));
        assert_eq!(outcome.normalized_name, DEFAULT);
        assert!(incoming.is_none());
        let raw = vec![0, 0xd800, 65];
        let mut incoming = Some(None);
        let outcome = begin(&mut document, &raw, &mut incoming).unwrap();
        assert_eq!(outcome.normalized_name, raw);
        assert_eq!(document.named_styles().count(), 2);
        assert!(matches!(document.named_styles().get(&[]), Some(None)));
        let before = document.clone();
        let mut incoming = Some(Some(value(2)));
        assert!(matches!(
            begin(&mut document, &[0, 66], &mut incoming),
            Err(Error::UnresolvedNull)
        ));
        assert_eq!(document, before);
        assert!(incoming.is_some());
        let mut incoming = Some(None);
        assert!(matches!(
            begin(&mut document, DEFAULT, &mut incoming),
            Err(Error::UnresolvedNull)
        ));
        assert_eq!(document, before);
        assert_eq!(incoming, Some(None));
    }

    #[test]
    fn native_equality_discards_new_value_preserving_old_map_and_records() {
        let mut document = Document::default();
        let mut first = value(1);
        first.font_name = vec![65, 0, 66];
        first.font_size = 0.0;
        document
            .named_styles_mut()
            .insert(name("A"), Some(first.clone()));
        add_entry(&mut document, name("A"));
        let before = document.clone();
        let original_wire = crate::document_style::encode(
            document
                .named_styles()
                .get(&name("A"))
                .unwrap()
                .as_ref()
                .unwrap(),
        )
        .unwrap();
        let mut equal = first;
        equal.font_name = vec![65, 0, 90];
        equal.font_size = -0.0;
        let mut incoming = Some(Some(equal.clone()));
        let outcome = begin(&mut document, &name("A"), &mut incoming).unwrap();
        assert!(matches!(outcome.action, Action::Done));
        assert_eq!(outcome.retired.discarded_incoming, Some(equal));
        assert_eq!(document, before);
        assert_eq!(
            crate::document_style::encode(
                document
                    .named_styles()
                    .get(&name("A"))
                    .unwrap()
                    .as_ref()
                    .unwrap()
            )
            .unwrap(),
            original_wire
        );
    }

    #[test]
    fn conflict_collision_and_atomic_tail_match_native_operation_order() {
        let mut document = Document::default();
        document
            .named_styles_mut()
            .insert(name("A"), Some(value(1)));
        document.named_styles_mut().insert(name("A2"), None);
        add_entry(&mut document, vec![65, 0, 0xd800]);
        let mut incoming = Some(Some(value(2)));
        let mut run = session(begin(&mut document, &name("A"), &mut incoming).unwrap());
        let before = document.clone();
        let original = run.clone();
        for candidate in [name("A"), vec![65, 0, 99], name("A2")] {
            let outcome = run.submit(&mut document, &candidate).unwrap();
            assert!(!outcome.applied);
            assert_eq!(document, before);
            assert_eq!(run, original);
        }
        let mut expected = before.clone();
        expected.named_styles_mut().remove(&name("A"));
        expected
            .named_styles_mut()
            .insert(name("A3"), Some(value(1)));
        expected.rename_style_cstring(&name("A"), &name("A3"));
        expected
            .named_styles_mut()
            .insert(name("A"), Some(value(2)));
        let outcome = run.submit(&mut document, &name("A3")).unwrap();
        assert!(outcome.applied);
        let retired = outcome.retired.unwrap();
        assert_eq!(retired.entry_names, [vec![65, 0, 0xd800]]);
        assert_eq!(document, expected);
        assert_eq!(
            document
                .named_styles()
                .iter()
                .map(|(key, _)| key.to_vec())
                .collect::<Vec<_>>(),
            expected
                .named_styles()
                .iter()
                .map(|(key, _)| key.to_vec())
                .collect::<Vec<_>>()
        );
        let before = document.clone();
        assert!(matches!(
            run.submit(&mut document, &name("A4")),
            Err(Error::Finished)
        ));
        assert_eq!(document, before);
    }

    #[test]
    fn raw_full_name_and_source_derived_absent_old_alias_commit() {
        let mut document = Document::default();
        let raw = vec![82, 0, 66, 57];
        document
            .named_styles_mut()
            .insert(name("R"), Some(value(1)));
        let mut incoming = Some(Some(value(2)));
        let outcome = begin(&mut document, &raw, &mut incoming).unwrap();
        assert_eq!(outcome.normalized_name, raw);
        let mut run = session(outcome);
        // Pure safe source-derived model, not a native callback/reentry fixture.
        document.named_styles_mut().remove(&name("R"));
        let outcome = run.submit(&mut document, &[82, 0, 90]).unwrap();
        assert!(outcome.applied);
        assert_eq!(document.named_styles().count(), 1);
        assert_eq!(
            document.named_styles().get(&name("R")),
            Some(&Some(value(2)))
        );
        assert_eq!(outcome.retired.unwrap().map_values, [Some(value(1))]);
    }

    #[test]
    fn final_count_and_invalid_inputs_are_preflighted() {
        use crate::document::{style_conflict_count, StyleRenameError};
        let max = i32::MAX as usize;
        assert_eq!(
            style_conflict_count(max, true, false, false),
            Err(StyleRenameError::CountOverflow)
        );
        assert_eq!(style_conflict_count(max, true, false, true), Ok(()));
        assert_eq!(
            style_conflict_count(0, true, false, false),
            Err(StyleRenameError::CountOverflow)
        );
        let mut document = Document::default();
        let before = document.clone();
        let mut incoming = None;
        assert!(matches!(
            begin(&mut document, &name("A"), &mut incoming),
            Err(Error::InvalidPreparedValue)
        ));
        assert_eq!(document, before);
        let mut previous = Some(value(1));
        let mut incoming = None;
        assert!(matches!(
            document.commit_style_conflict(&name("A"), &name("A2"), &mut previous, &mut incoming),
            Err(StyleRenameError::InvalidPreparedValue)
        ));
        assert_eq!(document, before);
        assert!(previous.is_some());
    }
}
