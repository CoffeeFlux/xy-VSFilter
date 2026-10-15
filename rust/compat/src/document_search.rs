// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned translated-query session over the admitted native search algorithm.
//!
//! The native adapter supplies requested Translate* results in order. Count is
//! frozen, values are live, and no document borrow survives construction. This
//! imposes no interpretation validation or generation guard: invalid native
//! modes still follow the adapter's existing zero translation. Array-invalidating
//! histories and native arithmetic failures are not certified by this protocol.
use crate::document::{Document, Refusal};
pub use crate::timeline_search::Mode;
use crate::timeline_search::Search;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct State {
    pub pending: bool,
    pub start: bool,
    pub request_index: i32,
    pub result_index: i32,
    pub found: bool,
    pub count: u32,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Finished,
}
pub struct Run {
    search: Search,
    count: u32,
}
impl Run {
    pub fn new(document: &Document, mode: Mode, time: i64) -> Result<Self, Refusal> {
        let count = if mode == Mode::Entry {
            document.entries().len()
        } else {
            document.segments().len()
        };
        Self::with_count(mode, time, count)
    }
    fn with_count(mode: Mode, time: i64, count: usize) -> Result<Self, Refusal> {
        if count > (i32::MAX as usize / 2) + 1 {
            return Err(Refusal::TimelineCountOverflow);
        }
        Ok(Self {
            search: Search::new(mode, time, count as i32),
            count: count as u32,
        })
    }
    pub fn state(&self) -> State {
        let request = self.search.request();
        let (result_index, found) = self.search.result();
        let (start, request_index) = request.unwrap_or((false, 0));
        State {
            pending: request.is_some(),
            start,
            request_index,
            result_index,
            found,
            count: self.count,
        }
    }
    pub fn supply(&mut self, value: i64) -> Result<State, Error> {
        if self.search.request().is_none() {
            return Err(Error::Finished);
        }
        self.search.supply(value);
        Ok(self.state())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::document::{AddMode, PreparedEntry};
    use alloc::{vec, vec::Vec};
    fn document(rows: &[(i64, i64)]) -> Document {
        let mut d = Document::default();
        for &(start, end) in rows {
            d.add_prepared(
                PreparedEntry {
                    text: vec![],
                    style: vec![],
                    actor: vec![],
                    effect: vec![],
                    unicode: true,
                    margins: [0; 4],
                    layer: 0,
                    start,
                    end,
                    readorder: -1,
                },
                AddMode::Add,
            )
            .unwrap();
        }
        d
    }
    fn execute(mode: Mode, time: i64, rows: &[(i64, i64)]) -> (Vec<(bool, i32)>, State) {
        let mut run = Run::with_count(mode, time, rows.len()).unwrap();
        let mut requests = Vec::new();
        while run.state().pending {
            let state = run.state();
            requests.push((state.start, state.request_index));
            let row = rows[state.request_index as usize];
            run.supply(if state.start { row.0 } else { row.1 }).unwrap();
        }
        let state = run.state();
        assert!(!state.start);
        assert_eq!(state.request_index, 0);
        assert_eq!(run.supply(0), Err(Error::Finished));
        (requests, state)
    }
    #[test]
    fn exact_translation_order_for_all_modes() {
        let rows = [(0, 2), (4, 6), (8, 10)];
        let (requests, state) = execute(Mode::Entry, 4, &rows);
        assert_eq!(requests, [(true, 2), (true, 1), (true, 0)]);
        assert_eq!((state.result_index, state.found), (1, true));
        let (requests, state) = execute(Mode::NextSegment, 3, &rows);
        assert_eq!(requests, [(false, 2), (false, 0), (false, 1)]);
        assert_eq!((state.result_index, state.found), (1, true));
        let (requests, state) = execute(Mode::ContainingSegment, 3, &rows);
        assert_eq!(requests, [(false, 2), (false, 0), (false, 1), (true, 1)]);
        assert_eq!((state.result_index, state.found), (-1, false));
    }
    #[test]
    fn nonmonotone_last_shortcut_and_equal_previous_requests() {
        let (requests, state) = execute(Mode::Entry, 5, &[(20, 21), (30, 31), (4, 6)]);
        assert_eq!(requests, [(true, 2)]);
        assert_eq!((state.result_index, state.found), (2, true));
        let (requests, state) = execute(Mode::Entry, 4, &[(4, 5), (4, 5), (4, 5), (8, 9)]);
        assert_eq!(requests, [(true, 3), (true, 1), (true, 0)]);
        assert_eq!((state.result_index, state.found), (0, true));
    }
    #[test]
    fn endpoints_gaps_and_empty_results_match_document_queries() {
        let d = document(&[(0, 2), (4, 6), (8, 10)]);
        for mode in [Mode::Entry, Mode::NextSegment, Mode::ContainingSegment] {
            for time in [-1, 0, 1, 2, 3, 4, 6, 8, 10, 11] {
                let rows: Vec<_> = if mode == Mode::Entry {
                    d.entries().iter().map(|e| (e.start, e.end)).collect()
                } else {
                    d.segments().iter().map(|s| (s.start, s.end)).collect()
                };
                let (_, state) = execute(mode, time, &rows);
                let expected = d.query_time(mode, time).unwrap();
                assert_eq!(
                    (state.result_index, state.found),
                    (expected.index, expected.found)
                );
            }
            let state = Run::new(&Document::default(), mode, 0).unwrap().state();
            assert_eq!(state.count, 0);
            assert!(!state.pending);
            assert_eq!(state.result_index, if mode == Mode::Entry { -1 } else { 0 });
            assert!(!state.found);
        }
    }
    #[test]
    fn freezes_selected_count_but_accepts_live_values_without_interpretation() {
        let mut d = document(&[(0, 2), (4, 6), (8, 10)]);
        let mut run = Run::new(&d, Mode::Entry, 5).unwrap();
        assert_eq!(run.state().request_index, 2);
        d.remove_all_entries();
        // Supply a changed translation; no stored snapshot is substituted.
        let state = run.supply(3).unwrap();
        assert_eq!((state.result_index, state.found, state.count), (2, true, 3));
        assert_eq!(Run::new(&d, Mode::Entry, 0).unwrap().state().count, 0);
        assert_eq!(Run::new(&d, Mode::NextSegment, 0).unwrap().state().count, 3);
    }
    #[test]
    fn midpoint_bound_without_enormous_allocation() {
        let limit = (i32::MAX as usize / 2) + 1;
        assert!(Run::with_count(Mode::Entry, 0, limit).is_ok());
        assert!(matches!(
            Run::with_count(Mode::Entry, 0, limit + 1),
            Err(Refusal::TimelineCountOverflow)
        ));
    }
}
