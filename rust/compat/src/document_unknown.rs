// SPDX-License-Identifier: GPL-2.0-or-later
//! Row-by-row unknown-style pass from pinned STS.cpp:2696.
//!
//! The native caller formats and displays warnings between `step` and `resume`.
//! No document reference is retained. Resume inserts the CURRENT row name into
//! suppression, then rewrites that row; heuristic state retains the pre-dialog
//! name. Native array-invalidating dialog histories are unresolved, not modeled
//! by generations or stale-plan rejection. Allocation/failure/reentry timing,
//! including map-key/node allocations and drops, is outside
//! this bounded contract. This module invokes no callbacks.
use alloc::vec::Vec;

use crate::{document::Document, document_style_map::StyleMap};

const DEFAULT: &[u16] = &[68, 101, 102, 97, 117, 108, 116];
const SENTINEL: &str = "!@dkdfjlakfkjjklezdv^5132ae";

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Step {
    Finished,
    Advanced,
    /// Full logical name, before native `%s` message formatting.
    Warning(Vec<u16>),
}
/// Owned original allocations, dropped by FFI only after admission ends.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Retired {
    pub entry_style: Option<Vec<u16>>,
    pub last_name: Option<Vec<u16>>,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Outcome {
    pub event: Step,
    pub retired: Retired,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    PendingResponse,
    NoPendingWarning,
    /// The original native row reference cannot be represented by this live
    /// index anymore. This is an unresolved native history, not a new policy.
    UnresolvedPendingRow,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Run {
    next_index: usize,
    last_name: Vec<u16>,
    last_missing: bool,
    report: bool,
    suppression: StyleMap<()>,
    pending: bool,
    finished: bool,
}
impl Default for Run {
    fn default() -> Self {
        Self::new()
    }
}
impl Run {
    pub fn new() -> Self {
        Self {
            next_index: 0,
            last_name: SENTINEL.encode_utf16().collect(),
            last_missing: false,
            report: true,
            suppression: StyleMap::new(),
            pending: false,
            finished: false,
        }
    }

    /// Observes the live count each row, preserving the native sentinel and
    /// consecutive-name shortcut. Present-null map nodes count as known.
    pub fn step(&mut self, document: &mut Document) -> Result<Outcome, Error> {
        if self.pending {
            return Err(Error::PendingResponse);
        }
        if self.finished || self.next_index >= document.entries().len() {
            self.finished = true;
            return Ok(Outcome {
                event: Step::Finished,
                retired: Retired::default(),
            });
        }
        let name = document.entries()[self.next_index].style.clone();
        let mut retired = Retired::default();
        if prefix(&self.last_name) == prefix(&name) {
            if self.last_missing {
                retired.entry_style = Some(self.rewrite(document)?);
            }
        } else {
            retired.last_name = Some(core::mem::replace(&mut self.last_name, name.clone()));
            self.last_missing = document.named_styles().get(&name).is_none();
            if self.last_missing {
                if self.suppression.get(&name).is_none() {
                    if self.report && prefix(&name) != DEFAULT {
                        self.pending = true;
                        return Ok(Outcome {
                            event: Step::Warning(name),
                            retired,
                        });
                    }
                    self.suppression.insert(name, ());
                }
                retired.entry_style = Some(self.rewrite(document)?);
            }
        }
        self.next_index += 1;
        Ok(Outcome {
            event: Step::Advanced,
            retired,
        })
    }

    /// Accepted means native MessageBox returned exactly IDOK. All other
    /// responses disable future reports while still inserting and rewriting.
    pub fn resume(&mut self, document: &mut Document, accepted: bool) -> Result<Retired, Error> {
        if !self.pending {
            return Err(Error::NoPendingWarning);
        }
        // Validate this unresolved history before altering the protocol state.
        let current_name = document
            .entries()
            .get(self.next_index)
            .ok_or(Error::UnresolvedPendingRow)?
            .style
            .clone();
        if !accepted {
            self.report = false;
        }
        self.suppression.insert(current_name, ());
        let entry_style = Some(self.rewrite(document)?);
        self.pending = false;
        self.next_index += 1;
        Ok(Retired {
            entry_style,
            last_name: None,
        })
    }

    /// Read-only bound for FFI reply preflight; not a frozen document count.
    pub fn next_index(&self) -> usize {
        self.next_index
    }

    fn rewrite(&self, document: &mut Document) -> Result<Vec<u16>, Error> {
        document
            .replace_prepared_style(self.next_index, DEFAULT.to_vec())
            .map_err(|_| Error::UnresolvedPendingRow)
    }
}

fn prefix(name: &[u16]) -> &[u16] {
    &name[..name
        .iter()
        .position(|&unit| unit == 0)
        .unwrap_or(name.len())]
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::document::{AddMode, PreparedEntry};
    use alloc::vec;

    fn units(name: &str) -> Vec<u16> {
        name.encode_utf16().collect()
    }
    fn make_document(names: &[Vec<u16>]) -> Document {
        let mut document = Document::default();
        for (index, name) in names.iter().enumerate() {
            document
                .add_prepared(
                    PreparedEntry {
                        text: vec![65, 0, 0xd800],
                        style: name.clone(),
                        actor: vec![0xdcff],
                        effect: vec![],
                        unicode: true,
                        margins: [-1, 2, 3, 4],
                        layer: 0,
                        start: index as i64,
                        end: index as i64 + 10,
                        readorder: -1,
                    },
                    AddMode::Add,
                )
                .unwrap();
        }
        document
    }

    fn event(run: &mut Run, document: &mut Document) -> Result<Step, Error> {
        run.step(document).map(|outcome| outcome.event)
    }
    #[test]
    fn native_derived_sentinel_shortcut_suppression_cancel_and_missing_default() {
        let names = [SENTINEL, "A", "A", "Known", "A", "B", "C", "Default"].map(units);
        let mut document = make_document(&names);
        // Warning pass only checks node presence, so a null Known node is safe
        // here in Rust; no native null-valued fixture is executed.
        document.named_styles_mut().insert(units("Known"), None);
        document.feedback_animation(0, true).unwrap();
        let before = document.clone();
        let mut run = Run::new();
        assert_eq!(event(&mut run, &mut document), Ok(Step::Advanced));
        assert_eq!(
            event(&mut run, &mut document),
            Ok(Step::Warning(units("A")))
        );
        assert_eq!(document.entries()[1].style, units("A"));
        run.resume(&mut document, true).unwrap();
        for _ in 0..3 {
            assert_eq!(event(&mut run, &mut document), Ok(Step::Advanced));
        }
        assert_eq!(
            event(&mut run, &mut document),
            Ok(Step::Warning(units("B")))
        );
        run.resume(&mut document, false).unwrap();
        for _ in 0..2 {
            assert_eq!(event(&mut run, &mut document), Ok(Step::Advanced));
        }
        assert_eq!(event(&mut run, &mut document), Ok(Step::Finished));
        let expected_names = [
            SENTINEL, "Default", "Default", "Known", "Default", "Default", "Default", "Default",
        ]
        .map(units);
        let mut expected = before;
        for (index, name) in expected_names.into_iter().enumerate() {
            expected.set_prepared_style(index, name).unwrap();
        }
        assert_eq!(document, expected); // complete non-style state unchanged
    }

    #[test]
    fn raw_names_use_prefix_equality_and_warning_owns_full_units() {
        let first = vec![65, 0, 0xd800];
        let alias = vec![65, 0, 0xdcff];
        let mut document = make_document(&[first.clone(), alias, units("a"), units("Default")]);
        let mut run = Run::new();
        let warning = event(&mut run, &mut document).unwrap();
        assert_eq!(warning, Step::Warning(first.clone()));
        document.set_prepared_style(0, units("changed")).unwrap();
        assert_eq!(warning, Step::Warning(first));
        run.resume(&mut document, true).unwrap();
        assert_eq!(event(&mut run, &mut document), Ok(Step::Advanced));
        assert_eq!(
            event(&mut run, &mut document),
            Ok(Step::Warning(units("a")))
        );
        run.resume(&mut document, true).unwrap();
        assert_eq!(event(&mut run, &mut document), Ok(Step::Advanced));
        assert_eq!(event(&mut run, &mut document), Ok(Step::Finished));
    }

    #[test]
    fn source_derived_resume_uses_current_key_and_keeps_pre_dialog_heuristic() {
        // Pure safe model of in-place editing, not native reentry evidence.
        let mut document = make_document(&[units("A"), units("B"), units("A")]);
        let mut run = Run::new();
        assert_eq!(
            event(&mut run, &mut document),
            Ok(Step::Warning(units("A")))
        );
        document.set_prepared_style(0, units("B")).unwrap();
        document.named_styles_mut().insert(units("A"), None);
        run.resume(&mut document, true).unwrap();
        assert_eq!(run.last_name, units("A"));
        assert!(run.last_missing);
        assert_eq!(event(&mut run, &mut document), Ok(Step::Advanced)); // B suppressed
        assert_eq!(event(&mut run, &mut document), Ok(Step::Advanced)); // A now known
        assert_eq!(document.entries()[2].style, units("A"));
    }

    #[test]
    fn source_derived_reorder_live_count_and_explicit_protocol_errors() {
        let mut document = make_document(&[units("A"), units("B")]);
        let mut run = Run::new();
        let before = document.clone();
        assert_eq!(
            run.resume(&mut document, true),
            Err(Error::NoPendingWarning)
        );
        assert_eq!(document, before);
        assert_eq!(
            event(&mut run, &mut document),
            Ok(Step::Warning(units("A")))
        );
        let pending = run.clone();
        assert_eq!(event(&mut run, &mut document), Err(Error::PendingResponse));
        assert_eq!(run, pending);
        // Reorder is safe here; this is source-derived, not a native fixture.
        document.reorder_entries(&[1, 0]).unwrap();
        run.resume(&mut document, true).unwrap();
        assert_eq!(document.entries()[0].style, DEFAULT);
        assert_eq!(document.entries()[1].style, units("A"));
        assert_eq!(event(&mut run, &mut document), Ok(Step::Advanced)); // pre-dialog A shortcut
        assert_eq!(event(&mut run, &mut document), Ok(Step::Finished));
        let mut removed = make_document(&[units("X")]);
        let mut other = Run::new();
        assert_eq!(
            event(&mut other, &mut removed),
            Ok(Step::Warning(units("X")))
        );
        removed.remove_all_entries();
        let pending = other.clone();
        assert_eq!(
            other.resume(&mut removed, false),
            Err(Error::UnresolvedPendingRow)
        );
        assert_eq!(other, pending);
    }
    #[test]
    fn retirement_moves_original_allocations_and_live_count_can_grow() {
        let mut document = make_document(&[units("A")]);
        let mut run = Run::new();
        let original_last = run.last_name.as_ptr();
        let original_style = document.entries()[0].style.as_ptr();
        let outcome = run.step(&mut document).unwrap();
        assert_eq!(outcome.event, Step::Warning(units("A")));
        assert_eq!(
            outcome.retired.last_name.as_ref().unwrap().as_ptr(),
            original_last
        );
        assert!(outcome.retired.entry_style.is_none());
        let retired = run.resume(&mut document, true).unwrap();
        assert_eq!(
            retired.entry_style.as_ref().unwrap().as_ptr(),
            original_style
        );
        assert_eq!(retired.entry_style, Some(units("A")));
        assert!(retired.last_name.is_none());
        let added = make_document(&[units("B")]).entries()[0].clone();
        document.add_prepared(added, AddMode::EntryOnly).unwrap();
        assert_eq!(run.next_index(), 1);
        assert_eq!(
            event(&mut run, &mut document),
            Ok(Step::Warning(units("B")))
        );
        run.resume(&mut document, true).unwrap();
        assert_eq!(event(&mut run, &mut document), Ok(Step::Finished));
        let added = make_document(&[units("C")]).entries()[0].clone();
        document.add_prepared(added, AddMode::EntryOnly).unwrap();
        assert_eq!(event(&mut run, &mut document), Ok(Step::Finished)); // native invocation already returned
    }
}
