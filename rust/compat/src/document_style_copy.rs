// SPDX-License-Identifier: GPL-2.0-or-later
//! Row-wise CopyStyles orchestration from pinned STS.cpp:2497–2516.
//!
//! Initialization clears destination before any source enumeration. Each later
//! step copies one current source row, advances its cursor before AddStyle, and
//! delegates classification to the shared AddStyle session. Native CRT suffix
//! generation stays outside this API. None source explicitly means self-source,
//! avoiding simultaneous mutable/shared references to one Document.
//!
//! Ordinal enumeration is admitted for a distinct unchanged source or ordinary
//! finite self append whose equal rows leave its map unchanged. Mutating native
//! POSITION histories, null source values and shared native pointers are not
//! native comparison evidence. Allocation/partial-failure and generic key/node
//! destruction timing remain qualified. Callers abort an errored run; retry
//! compatibility is not claimed and no permanent error policy is invented.
//! Prior successful rows remain committed. No callback or generation guard.
use crate::document::Document;
use crate::document_style::StyleValue;
use crate::document_style_add::{self, Action, Session};
use alloc::vec::Vec;

#[derive(Debug, Default)]
pub struct Retired {
    pub styles: Vec<Option<StyleValue>>,
    pub entry_names: Vec<Vec<u16>>,
    pub row_key: Option<Vec<u16>>,
    pub normalized_name: Option<Vec<u16>>,
    pub completed_session: Option<Session>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ErrorKind {
    PendingCandidate,
    NoPendingCandidate,
    UnresolvedNull,
    CountOverflow,
    InvalidPreparedValue,
}
/// Failure also transfers any cloned rejected payload outside admission.
#[derive(Debug)]
pub struct Error {
    pub kind: ErrorKind,
    pub retired: Retired,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Step {
    Done,
    Advanced,
    NeedSuffix(Vec<u16>),
}
#[derive(Debug)]
pub struct Outcome {
    pub event: Step,
    pub retired: Retired,
}
#[derive(Debug)]
pub struct Submit {
    pub applied: bool,
    pub retired: Retired,
}
#[derive(Debug)]
pub struct Run {
    append: bool,
    initialized: bool,
    next_index: usize,
    pending: Option<Session>,
    finished: bool,
}
impl Run {
    pub fn new(append: bool) -> Self {
        Self {
            append,
            initialized: false,
            next_index: 0,
            pending: None,
            finished: false,
        }
    }
    pub fn initialized(&self) -> bool {
        self.initialized
    }
    pub fn next_index(&self) -> usize {
        self.next_index
    }
    /// First call initializes only and never reads source. Later calls process
    /// at most one source row, keeping reply/retirement capacity bounded.
    // Inline retirement avoids a new fallible Box allocation after mutation.
    #[allow(clippy::result_large_err)]
    pub fn step(
        &mut self,
        destination: &mut Document,
        source: Option<&Document>,
    ) -> Result<Outcome, Error> {
        if !self.initialized {
            let styles = if self.append {
                Vec::new()
            } else {
                destination.named_styles_mut().clear()
            };
            self.initialized = true;
            return Ok(Outcome {
                event: Step::Advanced,
                retired: Retired {
                    styles,
                    ..Retired::default()
                },
            });
        }
        if self.pending.is_some() {
            return Err(error(ErrorKind::PendingCandidate));
        }
        if self.finished {
            return Ok(Outcome {
                event: Step::Done,
                retired: Retired::default(),
            });
        }
        let source = source.unwrap_or(destination);
        let Some((key, value)) = source.named_styles().iter().nth(self.next_index) else {
            self.finished = true;
            return Ok(Outcome {
                event: Step::Done,
                retired: Retired::default(),
            });
        };
        let Some(value) = value.as_ref() else {
            return Err(error(ErrorKind::UnresolvedNull));
        };
        // Advance before AddStyle; callers abort rather than retry on error.
        self.next_index = self
            .next_index
            .checked_add(1)
            .ok_or_else(|| error(ErrorKind::CountOverflow))?;
        let key = key.to_vec();
        let mut incoming = Some(Some(value.copy_owned()));
        let mut retired = Retired {
            styles: Vec::with_capacity(1),
            row_key: Some(key),
            ..Retired::default()
        };
        let result = document_style_add::begin(
            destination,
            retired.row_key.as_ref().expect("row key staged"),
            &mut incoming,
        );
        let outcome = match result {
            Ok(outcome) => outcome,
            Err(cause) => {
                if let Some(value) = incoming.take() {
                    retired.styles.push(value);
                }
                return Err(Error {
                    kind: add_error(cause),
                    retired,
                });
            }
        };
        if let Some(value) = outcome.retired.discarded_incoming {
            retired.styles.push(Some(value));
        }
        debug_assert!(outcome.retired.relocation.is_none());
        let event = match outcome.action {
            Action::Done => {
                retired.normalized_name = Some(outcome.normalized_name);
                Step::Advanced
            }
            Action::Conflict(run) => {
                self.pending = Some(run);
                Step::NeedSuffix(outcome.normalized_name)
            }
        };
        Ok(Outcome { event, retired })
    }
    // Completed Session ownership leaves admission without allocating a Box.
    #[allow(clippy::result_large_err)]
    pub fn submit(
        &mut self,
        destination: &mut Document,
        candidate: &[u16],
    ) -> Result<Submit, Error> {
        let run = self
            .pending
            .as_mut()
            .ok_or_else(|| error(ErrorKind::NoPendingCandidate))?;
        let result = run
            .submit(destination, candidate)
            .map_err(|cause| error(add_error(cause)))?;
        let mut retired = match result.retired {
            Some(retired) => Retired {
                styles: retired.map_values,
                entry_names: retired.entry_names,
                ..Retired::default()
            },
            None => Retired::default(),
        };
        if result.applied {
            retired.completed_session = self.pending.take();
        }
        Ok(Submit {
            applied: result.applied,
            retired,
        })
    }
}
fn error(kind: ErrorKind) -> Error {
    Error {
        kind,
        retired: Retired::default(),
    }
}
fn add_error(error: document_style_add::Error) -> ErrorKind {
    match error {
        document_style_add::Error::UnresolvedNull => ErrorKind::UnresolvedNull,
        document_style_add::Error::CountOverflow => ErrorKind::CountOverflow,
        document_style_add::Error::InvalidPreparedValue | document_style_add::Error::Finished => {
            ErrorKind::InvalidPreparedValue
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::document::{AddMode, PreparedEntry};
    use alloc::vec;
    fn name(value: &str) -> Vec<u16> {
        value.encode_utf16().collect()
    }
    fn value(id: u8) -> StyleValue {
        let mut wire = vec![0; 192];
        wire[0] = id;
        let mut value = crate::document_style::decode(&wire).unwrap();
        value.font_name = vec![70, 0, 0xd800 + u16::from(id)];
        value.font_size = if id % 2 == 0 { 0.0 } else { -0.0 };
        value
    }
    fn add(document: &mut Document, key: &[u16], id: u8) {
        document
            .named_styles_mut()
            .insert(key.to_vec(), Some(value(id)));
    }
    fn entry(document: &mut Document, style: &[u16]) {
        document
            .add_prepared(
                PreparedEntry {
                    text: vec![65, 0, 0xd800],
                    style: style.to_vec(),
                    actor: vec![0xdcff],
                    effect: vec![],
                    unicode: true,
                    margins: [-1, 2, 3, 4],
                    layer: 0,
                    start: 0,
                    end: 10,
                    readorder: -1,
                },
                AddMode::Add,
            )
            .unwrap();
        document.feedback_animation(0, true).unwrap();
    }
    fn step(run: &mut Run, dest: &mut Document, source: Option<&Document>) -> Step {
        run.step(dest, source).unwrap().event
    }

    #[test]
    fn distinct_replace_clears_before_read_and_copies_rows_independently() {
        let mut source = Document::default();
        add(&mut source, &name("A"), 1);
        add(&mut source, &name("B"), 2);
        let mut dest = Document::default();
        add(&mut dest, &name("Old"), 3);
        entry(&mut dest, &name("Old"));
        let records = dest.entries().to_vec();
        let segments = dest.segments().to_vec();
        let before = source.clone();
        let mut run = Run::new(false);
        assert!(!run.initialized());
        let initialized = run.step(&mut dest, Some(&source)).unwrap();
        assert_eq!(initialized.event, Step::Advanced);
        assert_eq!(initialized.retired.styles, [Some(value(3))]);
        assert!(dest.named_styles().count() == 0);
        assert_eq!(run.next_index(), 0);
        assert_eq!(step(&mut run, &mut dest, Some(&source)), Step::Advanced);
        assert_eq!(step(&mut run, &mut dest, Some(&source)), Step::Advanced);
        assert_eq!(step(&mut run, &mut dest, Some(&source)), Step::Done);
        assert_eq!(source, before);
        assert_eq!(dest.entries(), records);
        assert_eq!(dest.segments(), segments);
        for (key, value) in source.named_styles().iter() {
            assert_eq!(dest.named_styles().get(key), Some(value));
        }
        add(&mut dest, &name("A"), 9);
        assert_eq!(source, before);
    }

    #[test]
    fn distinct_append_routes_equal_conflict_missing_in_source_order() {
        let mut source = Document::default();
        for (key, id) in [("Equal", 1), ("Conflict", 2), ("Missing", 3)] {
            add(&mut source, &name(key), id);
        }
        let mut dest = Document::default();
        add(&mut dest, &name("Equal"), 1);
        add(&mut dest, &name("Conflict"), 4);
        add(&mut dest, &name("Conflict2"), 5);
        entry(&mut dest, &name("Conflict"));
        let before = source.clone();
        let segments = dest.segments().to_vec();
        let mut run = Run::new(true);
        assert_eq!(step(&mut run, &mut dest, Some(&source)), Step::Advanced);
        let mut seen = Vec::new();
        for _ in 0..3 {
            let index = run.next_index();
            let key = source.named_styles().iter().nth(index).unwrap().0.to_vec();
            seen.push(key.clone());
            let result = run.step(&mut dest, Some(&source)).unwrap();
            match result.event {
                Step::NeedSuffix(normalized) => {
                    assert_eq!(normalized, name("Conflict"));
                    assert_eq!(
                        run.step(&mut dest, Some(&source)).unwrap_err().kind,
                        ErrorKind::PendingCandidate
                    );
                    let snapshot = dest.clone();
                    assert!(!run.submit(&mut dest, &name("Conflict2")).unwrap().applied);
                    assert_eq!(dest, snapshot);
                    let applied = run.submit(&mut dest, &name("Conflict3")).unwrap();
                    assert!(applied.applied);
                    assert_eq!(applied.retired.entry_names, [name("Conflict")]);
                    assert!(applied.retired.completed_session.is_some());
                }
                Step::Advanced => {}
                Step::Done => panic!("early end"),
            }
        }
        assert_eq!(
            seen,
            source
                .named_styles()
                .iter()
                .map(|(key, _)| key.to_vec())
                .collect::<Vec<_>>()
        );
        assert_eq!(step(&mut run, &mut dest, Some(&source)), Step::Done);
        assert_eq!(
            dest.named_styles().get(&name("Conflict")),
            Some(&Some(value(2)))
        );
        assert_eq!(
            dest.named_styles().get(&name("Conflict3")),
            Some(&Some(value(4)))
        );
        assert_eq!(dest.entries()[0].style, name("Conflict3"));
        assert_eq!(dest.segments(), segments);
        assert_eq!(source, before);
    }

    #[test]
    fn ordinary_self_append_and_self_replace_do_not_alias_references() {
        let mut document = Document::default();
        for (key, id) in [("Default", 1), ("A", 2), ("B", 3)] {
            add(&mut document, &name(key), id);
        }
        entry(&mut document, &name("A"));
        let before = document.clone();
        let mut run = Run::new(true);
        assert_eq!(step(&mut run, &mut document, None), Step::Advanced);
        for _ in 0..3 {
            let result = run.step(&mut document, None).unwrap();
            assert_eq!(result.event, Step::Advanced);
            assert_eq!(result.retired.styles.len(), 1);
            assert!(result.retired.row_key.is_some());
            assert!(result.retired.normalized_name.is_some());
        }
        assert_eq!(step(&mut run, &mut document, None), Step::Done);
        assert_eq!(document, before);
        let mut replace = Run::new(false);
        let result = replace.step(&mut document, None).unwrap();
        assert_eq!(result.retired.styles.len(), 3);
        assert_eq!(replace.next_index(), 0);
        assert_eq!(step(&mut replace, &mut document, None), Step::Done);
        assert!(document.named_styles().count() == 0);
        assert_eq!(document.entries(), before.entries());
        assert_eq!(document.segments(), before.segments());
    }

    #[test]
    fn stored_empty_source_key_normalizes_and_conflicts_without_bulk_clone() {
        let mut source = Document::default();
        add(&mut source, &[0, 65], 1);
        add(&mut source, &name("Default"), 2);
        let before = source.clone();
        let mut dest = Document::default();
        let mut run = Run::new(false);
        assert_eq!(step(&mut run, &mut dest, Some(&source)), Step::Advanced);
        for _ in 0..2 {
            match step(&mut run, &mut dest, Some(&source)) {
                Step::Advanced => {}
                Step::NeedSuffix(key) => {
                    assert_eq!(key, name("Default"));
                    assert!(run.submit(&mut dest, &name("Default2")).unwrap().applied);
                }
                Step::Done => panic!("early end"),
            }
        }
        assert_eq!(step(&mut run, &mut dest, Some(&source)), Step::Done);
        assert_eq!(dest.named_styles().count(), 2);
        assert!(dest.named_styles().get(&[]).is_none());
        assert!(dest.named_styles().get(&name("Default")).is_some());
        assert!(dest.named_styles().get(&name("Default2")).is_some());
        assert_eq!(source, before);
    }

    #[test]
    fn source_null_and_rejected_copied_value_have_explicit_error_retirement() {
        let mut source = Document::default();
        source.named_styles_mut().insert(name("Null"), None);
        let mut dest = Document::default();
        let mut run = Run::new(true);
        assert_eq!(step(&mut run, &mut dest, Some(&source)), Step::Advanced);
        let before = dest.clone();
        let error = run.step(&mut dest, Some(&source)).unwrap_err();
        assert_eq!(error.kind, ErrorKind::UnresolvedNull);
        assert!(error.retired.styles.is_empty());
        assert_eq!(dest, before);
        add(&mut source, &name("Null"), 1);
        dest.named_styles_mut().insert(name("Null"), None);
        let before = dest.clone();
        let error = run.step(&mut dest, Some(&source)).unwrap_err();
        assert_eq!(error.kind, ErrorKind::UnresolvedNull);
        assert_eq!(error.retired.styles, [Some(value(1))]);
        assert_eq!(error.retired.row_key, Some(name("Null")));
        assert_eq!(dest, before);
        assert_eq!(run.next_index(), 1); // abort, not retry
        assert_eq!(
            run.submit(&mut dest, &name("Other")).unwrap_err().kind,
            ErrorKind::NoPendingCandidate
        );
    }
    #[test]
    fn replacement_retains_destination_bin_history_instead_of_cloning_source_map() {
        let mut source = Document::default();
        add(&mut source, &name("A"), 1);
        add(&mut source, &name("B"), 2);
        let mut dest = Document::default();
        for key in 0..128u16 {
            add(&mut dest, &[71, key + 1], 3);
        }
        let bins = dest.named_styles().bin_count();
        assert!(bins > source.named_styles().bin_count());
        let mut run = Run::new(false);
        assert_eq!(step(&mut run, &mut dest, Some(&source)), Step::Advanced);
        assert_eq!(dest.named_styles().bin_count(), bins);
        for _ in 0..2 {
            assert_eq!(step(&mut run, &mut dest, Some(&source)), Step::Advanced);
        }
        assert_eq!(step(&mut run, &mut dest, Some(&source)), Step::Done);
        assert_eq!(dest.named_styles().bin_count(), bins);
        for (key, value) in source.named_styles().iter() {
            assert_eq!(dest.named_styles().get(key), Some(value));
        }
    }
}
