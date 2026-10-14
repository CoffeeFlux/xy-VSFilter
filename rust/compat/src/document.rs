// SPDX-License-Identifier: GPL-2.0-or-later
//! Persistent prepared-record subset of `CSimpleTextSubtitle` at
//! 4911cacb102bc0cbd41449793e54473265b04e62 (`STS.cpp`).
//!
//! Callers must perform native text/style preparation and its initial rejection
//! checks before supplying records (the suffix begins at STSEntry construction);
//! raw code units are retained, including unpaired surrogates and embedded NULs.
//! This is not the Add parser, full Copy, style/metadata owner, CRT sort, encoding
//! conversion, or a production integration. Allocation failure/partial native
//! mutation and callbacks are outside this API's comparison domain.
use alloc::{vec, vec::Vec};

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PreparedEntry {
    pub text: Vec<u16>,
    pub style: Vec<u16>,
    pub actor: Vec<u16>,
    pub effect: Vec<u16>,
    pub unicode: bool,
    /// Left, top, right, bottom, without normalization.
    pub margins: [i32; 4],
    pub layer: i32,
    pub start: i64,
    pub end: i64,
    pub readorder: i32,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DocumentSegment {
    pub start: i64,
    pub end: i64,
    pub entries: Vec<usize>,
    pub animated: bool,
}
impl DocumentSegment {
    fn new(start: i64, end: i64, entries: Vec<usize>) -> Self {
        Self {
            start,
            end,
            entries,
            animated: false,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AddMode {
    Add,
    EntryOnly,
    WebVtt,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Refusal {
    /// Retained segments refer beyond the entry array. Native access is unsafe.
    StaleEntryIndex,
    /// Native WebVTT Add dereferences the end iterator in this history.
    WebVttEndIterator,
    /// Native entry counts and segment entry indices are signed 32-bit values.
    EntryCountOverflow,
    SegmentIndex,
    EntryIndex,
    InvalidPermutation,
    TimeCountMismatch,
    /// Selected timeline count exceeds the native signed-midpoint domain.
    TimelineCountOverflow,
}

/// Native search outputs are distinct: an empty segment timeline returns index
/// zero with found false; NextSegment may return count, ContainingSegment -1.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct QueryResult {
    pub index: i32,
    pub found: bool,
    /// Selected entry/segment array length, not active member count.
    pub count: i32,
}

/// Clone creates independent storage; it does not emulate native Copy callbacks
/// or copying the document's still-native styles and metadata.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Document {
    entries: Vec<PreparedEntry>,
    segments: Vec<DocumentSegment>,
}
impl Document {
    pub fn entries(&self) -> &[PreparedEntry] {
        &self.entries
    }
    pub fn segments(&self) -> &[DocumentSegment] {
        &self.segments
    }
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    /// Executes the pinned search request sequence in TIME mode. Entry starts
    /// need not be sorted: the native last-entry shortcut and binary search
    /// quirks remain observable. Segment queries never dereference member entry
    /// indices, so retained stale segments remain queryable after entry removal.
    /// Frame/fps translation and native signed midpoint overflow for enormous
    /// arrays are outside this bounded time-query contract.
    pub fn query_time(
        &self,
        mode: crate::timeline_search::Mode,
        time: i64,
    ) -> Result<QueryResult, Refusal> {
        use crate::timeline_search::{Mode, Search};
        let len = if mode == Mode::Entry {
            self.entries.len()
        } else {
            self.segments.len()
        };
        // Native midpoint addition must remain representable as signed int.
        if len > (i32::MAX as usize / 2) + 1 {
            return Err(Refusal::TimelineCountOverflow);
        }
        let count = len as i32;
        let mut search = Search::new(mode, time, count);
        while let Some((start, index)) = search.request() {
            // Native Translate* returns -1 for an out-of-range request.
            let value = if mode == Mode::Entry {
                self.entries
                    .get(index as usize)
                    .map(|e| if start { e.start } else { e.end })
            } else {
                self.segments
                    .get(index as usize)
                    .map(|s| if start { s.start } else { s.end })
            }
            .unwrap_or(-1);
            search.supply(value);
        }
        let (index, found) = search.result();
        Ok(QueryResult {
            index,
            found,
            count,
        })
    }

    /// Successful `None` denotes a native rejection or WebVTT duplicate.
    /// Refusals leave the owner unchanged. Stale indices are checked before
    /// appending, even if appending might accidentally make one valid: native
    /// stale-array histories have no established safe compatibility contract.
    pub fn add_prepared(
        &mut self,
        mut entry: PreparedEntry,
        mode: AddMode,
    ) -> Result<Option<usize>, Refusal> {
        if entry.start > entry.end || (mode != AddMode::EntryOnly && entry.start == entry.end) {
            return Ok(None);
        }
        let n = self.entries.len();
        let default_order = i32::try_from(n).map_err(|_| Refusal::EntryCountOverflow)?;
        if entry.readorder < 0 {
            entry.readorder = default_order;
        }
        if mode == AddMode::EntryOnly {
            self.entries.push(entry);
            return Ok(Some(n));
        }
        if self
            .segments
            .iter()
            .flat_map(|s| &s.entries)
            .any(|&e| e >= n)
        {
            return Err(Refusal::StaleEntryIndex);
        }
        let start = entry.start;
        let end = entry.end;
        if self.segments.is_empty() {
            self.entries.push(entry);
            self.segments
                .push(DocumentSegment::new(start, end, vec![n]));
            return Ok(Some(n));
        }
        let mut i = self.segments.partition_point(|s| s.start < start);
        if mode == AddMode::WebVtt {
            let segment = self.segments.get(i).ok_or(Refusal::WebVttEndIterator)?;
            if start == segment.start && end == segment.end {
                return Ok(None);
            }
        }
        self.entries.push(entry);
        if i > 0 && self.segments[i - 1].end > start {
            let prefix = DocumentSegment::new(
                self.segments[i - 1].start,
                start,
                self.segments[i - 1].entries.clone(),
            );
            self.segments[i - 1].start = start;
            self.segments.insert(i - 1, prefix);
        } else if i < self.segments.len() && start < self.segments[i].start {
            self.segments.insert(
                i,
                DocumentSegment::new(start, end.min(self.segments[i].start), vec![n]),
            );
            i += 1;
        }
        let mut last_end = i64::MAX;
        while i < self.segments.len() && self.segments[i].start < end {
            if last_end < self.segments[i].start {
                let gap_end = self.segments[i].start;
                self.segments
                    .insert(i, DocumentSegment::new(last_end, gap_end, vec![n]));
                last_end = gap_end;
            } else {
                if end < self.segments[i].end {
                    let suffix = DocumentSegment::new(
                        end,
                        self.segments[i].end,
                        self.segments[i].entries.clone(),
                    );
                    self.segments[i].end = end;
                    self.segments.insert(i + 1, suffix);
                }
                let order = self.entries[n].readorder;
                let members = &mut self.segments[i].entries;
                if members
                    .last()
                    .is_none_or(|&old| order >= self.entries[old].readorder)
                {
                    members.push(n);
                } else if let Some(position) = members
                    .iter()
                    .position(|&old| order < self.entries[old].readorder)
                {
                    members.insert(position, n);
                }
                last_end = self.segments[i].end;
            }
            i += 1;
        }
        // With nonempty segments and start < end, insertion/splitting above
        // guarantees i > 0, including insertion before the first segment.
        let previous_end = self.segments[i - 1].end;
        if end > previous_end {
            self.segments.insert(
                i,
                DocumentSegment::new(start.max(previous_end), end, vec![n]),
            );
        }
        Ok(Some(n))
    }

    /// Stores caller-prepared code units without normalization or conversion.
    /// Native SetStr preparation uses None and retains the stored Unicode flag;
    /// ConvertUnicode's changed branch supplies Some(target) after conversion.
    /// Conversion/no-op decisions, charset lookup and errors remain the adapter's
    /// responsibility. Neither operation rebuilds segments or resets animation.
    pub fn set_prepared_text(
        &mut self,
        index: usize,
        text: Vec<u16>,
        unicode: Option<bool>,
    ) -> Result<(), Refusal> {
        let entry = self.entries.get_mut(index).ok_or(Refusal::EntryIndex)?;
        entry.text = text;
        if let Some(unicode) = unicode {
            entry.unicode = unicode;
        }
        Ok(())
    }

    /// Stores already-prepared style code units, without normalization or a
    /// segment rebuild. Native style-map decisions remain with the caller.
    pub fn set_prepared_style(&mut self, index: usize, style: Vec<u16>) -> Result<(), Refusal> {
        self.entries
            .get_mut(index)
            .ok_or(Refusal::EntryIndex)?
            .style = style;
        Ok(())
    }

    /// Publishes one already-evaluated endpoint without rebuilding segments.
    /// Reversed/equal times and stale segment boundaries are deliberately kept.
    pub fn set_entry_time(&mut self, index: usize, start: bool, value: i64) -> Result<(), Refusal> {
        let entry = self.entries.get_mut(index).ok_or(Refusal::EntryIndex)?;
        if start {
            entry.start = value;
        } else {
            entry.end = value;
        }
        Ok(())
    }

    /// Successful storage-only copy in native segment-then-entry order. This
    /// retains stale indices and animation, with independent owned payloads.
    /// It does not emulate native Copy's Empty/styles/metadata/callback order or
    /// allocation-failure partial state. Native self-Copy must Empty first; a
    /// registry must handle identical handles without aliasing mutable borrows.
    pub fn copy_storage_from(&mut self, source: &Document) {
        self.segments = source.segments.clone();
        self.entries = source.entries.clone();
    }

    /// Accepts the native CRT's computed ordering as original storage indices.
    /// This does not select a Rust sorting algorithm or establish CRT equal-key
    /// permutations. Payloads move without cloning; readorder stays unchanged.
    /// As in native Sort, even the identity permutation rebuilds segments.
    pub fn reorder_entries(&mut self, order: &[usize]) -> Result<(), Refusal> {
        if order.len() != self.entries.len() {
            return Err(Refusal::InvalidPermutation);
        }
        let mut seen = vec![false; self.entries.len()];
        for &index in order {
            let slot = seen.get_mut(index).ok_or(Refusal::InvalidPermutation)?;
            if *slot {
                return Err(Refusal::InvalidPermutation);
            }
            *slot = true;
        }
        let mut slots: Vec<_> = core::mem::take(&mut self.entries)
            .into_iter()
            .map(Some)
            .collect();
        self.entries = order
            .iter()
            .map(|&index| {
                slots[index]
                    .take()
                    .expect("permutation validated before mutation")
            })
            .collect();
        self.rebuild_segments();
        Ok(())
    }

    /// Installs already-evaluated times in storage order and rebuilds segments.
    /// Reversed and equal endpoints are retained. The adapter owns mode guards,
    /// actual CRT arithmetic, partial native failure behavior and mode publication
    /// before the native rebuild callback. This owner invokes no callbacks.
    pub fn replace_entry_times(&mut self, times: &[(i64, i64)]) -> Result<(), Refusal> {
        if times.len() != self.entries.len() {
            return Err(Refusal::TimeCountMismatch);
        }
        for (entry, &(start, end)) in self.entries.iter_mut().zip(times) {
            entry.start = start;
            entry.end = end;
        }
        self.rebuild_segments();
        Ok(())
    }

    /// Rebuild uses storage order rather than readorder; animation resets.
    pub fn rebuild_segments(&mut self) {
        let times: Vec<_> = self.entries.iter().map(|e| (e.start, e.end)).collect();
        self.segments = crate::segments::create(&times)
            .into_iter()
            .map(|s| DocumentSegment::new(s.start, s.end, s.entries))
            .collect();
    }
    /// Deliberately retains segments, including stale indices and animation.
    pub fn remove_all_entries(&mut self) {
        self.entries.clear();
    }
    /// Only this subset's storage; not native Empty's metadata/style reset.
    pub fn clear_entries_and_segments(&mut self) {
        self.entries.clear();
        self.segments.clear();
    }
    pub fn feedback_animation(&mut self, index: usize, animated: bool) -> Result<(), Refusal> {
        self.segments
            .get_mut(index)
            .ok_or(Refusal::SegmentIndex)?
            .animated |= animated;
        Ok(())
    }
    /// MicroDVD's last-stored-entry correction does not rebuild segments, nor
    /// clamp the corrected end to the entry's start.
    pub fn correct_last_end(&mut self, max_end: i64) -> Option<i64> {
        let entry = self.entries.last_mut()?;
        entry.end = entry.end.min(max_end);
        Some(entry.end)
    }
    /// Exact code-unit comparison; no case folding or style-map ownership.
    /// This is not yet a substitute for native CString comparison when a
    /// style name contains NUL; that comparison contract requires ATL evidence.
    pub fn rename_style(&mut self, old: &[u16], new: &[u16]) {
        for entry in &mut self.entries {
            if entry.style == old {
                entry.style = new.to_vec();
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn entry(start: i64, end: i64, readorder: i32) -> PreparedEntry {
        PreparedEntry {
            text: vec![0xd800, 0],
            style: vec![42],
            actor: vec![0xdc00],
            effect: vec![],
            unicode: false,
            margins: [-1, 2, 3, 4],
            layer: -8,
            start,
            end,
            readorder,
        }
    }
    #[test]
    fn lifecycle_preserves_stale_segments_and_independent_storage() {
        let mut d = Document::default();
        d.add_prepared(entry(0, 10, -1), AddMode::Add).unwrap();
        d.feedback_animation(0, true).unwrap();
        let copy = d.clone();
        d.correct_last_end(-1);
        assert_eq!(d.segments()[0].end, 10);
        d.rename_style(&[42], &[0xd800]);
        assert_eq!(copy.entries()[0].style, [42]);
        d.remove_all_entries();
        assert!(d.is_empty());
        assert!(d.segments()[0].animated);
        assert_eq!(
            d.add_prepared(entry(0, 1, -1), AddMode::Add),
            Err(Refusal::StaleEntryIndex)
        );
        d.rebuild_segments();
        assert!(d.segments().is_empty());
        d.add_prepared(entry(3, 3, -1), AddMode::EntryOnly).unwrap();
        assert_eq!(d.entries()[0].readorder, 0);
        d.clear_entries_and_segments();
        assert_eq!(d, Document::default());
    }
    #[test]
    fn split_flags_reset_only_on_new_segments_and_rebuild_uses_storage_order() {
        let mut d = Document::default();
        d.add_prepared(entry(0, 10, 9), AddMode::Add).unwrap();
        d.feedback_animation(0, true).unwrap();
        d.add_prepared(entry(2, 8, 1), AddMode::Add).unwrap();
        assert_eq!(
            d.segments().iter().map(|s| s.animated).collect::<Vec<_>>(),
            [false, true, false]
        );
        assert_eq!(d.segments()[1].entries, [1, 0]);
        d.feedback_animation(1, false).unwrap();
        assert!(d.segments()[1].animated);
        d.rebuild_segments();
        assert_eq!(d.segments()[1].entries, [0, 1]);
        assert!(d.segments().iter().all(|s| !s.animated));
    }
    #[test]
    fn prepared_empty_text_and_zero_duration_have_distinct_suffix_behavior() {
        let mut d = Document::default();
        let mut e = entry(0, 10, -1);
        e.text.clear();
        assert_eq!(d.add_prepared(e, AddMode::Add), Ok(Some(0)));
        assert_eq!(d.add_prepared(entry(4, 4, -1), AddMode::Add), Ok(None));
        assert_eq!(
            d.add_prepared(entry(4, 4, -1), AddMode::EntryOnly),
            Ok(Some(1))
        );
        assert_eq!(d.entries()[1].readorder, 1);
        d.rebuild_segments();
        assert_eq!(
            d.segments()
                .iter()
                .map(|s| (s.start, s.end))
                .collect::<Vec<_>>(),
            [(0, 4), (4, 10)]
        );
        assert!(d.segments().iter().all(|s| s.entries == [0]));
    }

    #[test]
    fn time_queries_preserve_empty_gap_endpoint_and_stale_segment_results() {
        use crate::timeline_search::Mode::{ContainingSegment, Entry, NextSegment};
        let mut d = Document::default();
        assert_eq!(
            d.query_time(Entry, 0).unwrap(),
            QueryResult {
                index: -1,
                found: false,
                count: 0
            }
        );
        assert_eq!(
            d.query_time(ContainingSegment, 0).unwrap(),
            QueryResult {
                index: 0,
                found: false,
                count: 0
            }
        );
        d.add_prepared(entry(0, 2, -1), AddMode::Add).unwrap();
        d.add_prepared(entry(4, 6, -1), AddMode::Add).unwrap();
        for (mode, time, index, found) in [
            (NextSegment, -1, 0, true),
            (ContainingSegment, -1, -1, false),
            (ContainingSegment, 0, 0, true),
            (NextSegment, 2, 1, true),
            (ContainingSegment, 2, -1, false),
            (ContainingSegment, 4, 1, true),
            (NextSegment, 6, 2, false),
            (ContainingSegment, 6, -1, false),
        ] {
            assert_eq!(
                d.query_time(mode, time).unwrap(),
                QueryResult {
                    index,
                    found,
                    count: 2
                }
            );
        }
        d.remove_all_entries();
        assert_eq!(
            d.query_time(ContainingSegment, 4).unwrap(),
            QueryResult {
                index: 1,
                found: true,
                count: 2
            }
        );
        assert_eq!(d.query_time(Entry, 4).unwrap().index, -1);
    }

    #[test]
    fn entry_queries_keep_native_nonmonotone_and_equal_start_shortcuts() {
        use crate::timeline_search::Mode::Entry;
        let mut d = Document::default();
        for start in [0, 0, 10, -2] {
            d.add_prepared(entry(start, start, -1), AddMode::EntryOnly)
                .unwrap();
        }
        // Native tests last entry first, despite earlier entries at time zero.
        assert_eq!(
            d.query_time(Entry, 0).unwrap(),
            QueryResult {
                index: 3,
                found: true,
                count: 4
            }
        );
        d.clear_entries_and_segments();
        for start in [0, 0, 10] {
            d.add_prepared(entry(start, start, -1), AddMode::EntryOnly)
                .unwrap();
        }
        assert_eq!(d.query_time(Entry, 0).unwrap().index, 0);
        assert_eq!(d.query_time(Entry, 10).unwrap().index, 2);
        assert_eq!(d.query_time(Entry, -1).unwrap().index, -1);
    }

    #[test]
    fn prepared_text_handoffs_retain_timeline_and_set_flag_only_when_requested() {
        let mut d = Document::default();
        d.add_prepared(entry(0, 10, -1), AddMode::Add).unwrap();
        d.feedback_animation(0, true).unwrap();
        let segments = d.segments().to_vec();
        d.set_prepared_text(0, vec![0, 0xdc00, 10], None).unwrap();
        assert_eq!(d.entries()[0].text, [0, 0xdc00, 10]);
        assert!(!d.entries()[0].unicode);
        d.set_prepared_text(0, vec![], Some(true)).unwrap();
        assert!(d.entries()[0].text.is_empty());
        assert!(d.entries()[0].unicode);
        assert_eq!(d.segments(), segments);
        let before = d.clone();
        assert_eq!(
            d.set_prepared_text(1, vec![1], Some(false)),
            Err(Refusal::EntryIndex)
        );
        assert_eq!(d, before);
    }

    #[test]
    fn reorder_moves_payload_allocations_and_rebuilds_storage_order() {
        let mut d = Document::default();
        d.add_prepared(entry(0, 10, 9), AddMode::Add).unwrap();
        d.add_prepared(entry(0, 10, 1), AddMode::Add).unwrap();
        d.feedback_animation(0, true).unwrap();
        let text_ptr = d.entries()[0].text.as_ptr();
        let actor_ptr = d.entries()[0].actor.as_ptr();
        d.reorder_entries(&[1, 0]).unwrap();
        assert_eq!(d.entries()[1].text.as_ptr(), text_ptr);
        assert_eq!(d.entries()[1].actor.as_ptr(), actor_ptr);
        assert_eq!(
            d.entries().iter().map(|e| e.readorder).collect::<Vec<_>>(),
            [1, 9]
        );
        assert_eq!(d.segments()[0].entries, [0, 1]);
        assert!(!d.segments()[0].animated);
        d.feedback_animation(0, true).unwrap();
        d.reorder_entries(&[0, 1]).unwrap();
        assert!(!d.segments()[0].animated);
    }

    #[test]
    fn invalid_permutations_and_time_counts_leave_complete_owner_unchanged() {
        let mut d = Document::default();
        d.add_prepared(entry(0, 10, -1), AddMode::Add).unwrap();
        d.add_prepared(entry(2, 8, -1), AddMode::Add).unwrap();
        d.feedback_animation(1, true).unwrap();
        let before = d.clone();
        for order in [&[][..], &[0][..], &[0, 0][..], &[0, 2][..], &[0, 1, 2][..]] {
            assert_eq!(d.reorder_entries(order), Err(Refusal::InvalidPermutation));
            assert_eq!(d, before);
        }
        assert_eq!(
            d.replace_entry_times(&[(1, 2)]),
            Err(Refusal::TimeCountMismatch)
        );
        assert_eq!(d, before);
        d.remove_all_entries();
        let stale = d.clone();
        assert_eq!(d.reorder_entries(&[0]), Err(Refusal::InvalidPermutation));
        assert_eq!(d, stale);
        assert_eq!(
            d.replace_entry_times(&[(1, 2)]),
            Err(Refusal::TimeCountMismatch)
        );
        assert_eq!(d, stale);
        d.reorder_entries(&[]).unwrap();
        assert!(d.segments().is_empty());
    }

    #[test]
    fn supplied_times_retain_reversed_zero_and_extreme_endpoints() {
        let mut d = Document::default();
        for _ in 0..3 {
            d.add_prepared(entry(0, 10, -1), AddMode::EntryOnly)
                .unwrap();
        }
        d.rebuild_segments();
        d.feedback_animation(0, true).unwrap();
        let times = [(i64::MIN, i64::MAX), (4, 4), (8, 6)];
        d.replace_entry_times(&times).unwrap();
        assert_eq!(
            d.entries()
                .iter()
                .map(|e| (e.start, e.end))
                .collect::<Vec<_>>(),
            times
        );
        assert_eq!(
            d.segments()
                .iter()
                .map(|s| (s.start, s.end))
                .collect::<Vec<_>>(),
            [(i64::MIN, 4), (4, 6), (6, 8), (8, i64::MAX)]
        );
        assert!(d.segments().iter().all(|s| s.entries == [0] && !s.animated));
    }

    #[test]
    fn direct_storage_edits_and_copy_preserve_unrebuilt_state() {
        let mut source = Document::default();
        source.add_prepared(entry(0, 10, 9), AddMode::Add).unwrap();
        source.feedback_animation(0, true).unwrap();
        source.set_prepared_style(0, vec![0, 0xd800]).unwrap();
        source.set_entry_time(0, true, i64::MAX).unwrap();
        source.set_entry_time(0, false, i64::MIN).unwrap();
        assert_eq!(source.segments()[0].start, 0);
        assert_eq!(source.segments()[0].end, 10);
        let unchanged = source.clone();
        assert_eq!(
            source.set_prepared_style(1, vec![]),
            Err(Refusal::EntryIndex)
        );
        assert_eq!(source.set_entry_time(1, true, 1), Err(Refusal::EntryIndex));
        assert_eq!(source, unchanged);
        let mut destination = Document::default();
        destination.copy_storage_from(&source);
        assert_eq!(destination, source);
        source.set_prepared_style(0, vec![1]).unwrap();
        assert_eq!(destination.entries()[0].style, [0, 0xd800]);
        source.remove_all_entries();
        destination.copy_storage_from(&source);
        assert!(destination.is_empty());
        assert_eq!(destination.segments()[0].entries, [0]);
        assert!(destination.segments()[0].animated);
        source.clear_entries_and_segments();
        assert_eq!(destination.segments().len(), 1);
    }

    #[test]
    fn webvtt_unresolved_end_iterator_refuses_without_mutation() {
        let mut d = Document::default();
        d.add_prepared(entry(0, 10, -1), AddMode::WebVtt).unwrap();
        assert_eq!(d.add_prepared(entry(0, 10, -1), AddMode::WebVtt), Ok(None));
        let before = d.clone();
        assert_eq!(
            d.add_prepared(entry(20, 30, -1), AddMode::WebVtt),
            Err(Refusal::WebVttEndIterator)
        );
        assert_eq!(d, before);
    }
}
