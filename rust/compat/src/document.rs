// SPDX-License-Identifier: GPL-2.0-or-later
//! Persistent prepared-record subset of `CSimpleTextSubtitle` at
//! 4911cacb102bc0cbd41449793e54473265b04e62 (`STS.cpp`).
//!
//! Callers must perform native text/style preparation and its initial rejection
//! checks before supplying records (the suffix begins at STSEntry construction);
//! raw code units are retained, including unpaired surrogates and embedded NULs.
//! Raw name/path, supplied interpretation/script/render metadata and initialized
//! styles are owned here. Native map orchestration, other metadata, Add parsing,
//! full Copy orchestration, CRT sort and encoding conversion remain outside this
//! API. Allocation failure/partial native mutation and callbacks are outside this
//! API's comparison domain.
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

/// Raw document identity; CString preparation and path interpretation stay native.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum IdentityField {
    Name,
    Path,
}

/// Native interpretation enum values are stored without normalization.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum InterpretationField {
    Mode,
    SubtitleType,
    Encoding,
}
impl InterpretationField {
    fn index(self) -> usize {
        match self {
            Self::Mode => 0,
            Self::SubtitleType => 1,
            Self::Encoding => 2,
        }
    }
}
/// Supplied script metadata; values are not normalized to rendering ranges.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ScriptField {
    Width,
    Height,
    Wrap,
    Collisions,
}
impl ScriptField {
    fn index(self) -> usize {
        match self {
            Self::Width => 0,
            Self::Height => 1,
            Self::Wrap => 2,
            Self::Collisions => 3,
        }
    }
}
/// Supplied native render metadata enums; no range normalization.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MetadataField {
    PARType,
    ColorMatrix,
    ColorRange,
}
impl MetadataField {
    fn index(self) -> usize {
        match self {
            Self::PARType => 0,
            Self::ColorMatrix => 1,
            Self::ColorRange => 2,
        }
    }
}
/// The initialized/native-supplied Copy header, captured after virtual Empty.
/// Optional absence is copied explicitly, never replaced with invented defaults.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SelectedState {
    name: Vec<u16>,
    interpretation: [Option<i32>; 3],
    script: [Option<i32>; 4],
    scaled_bas: Option<bool>,
    default_style: Option<crate::document_style::StyleValue>,
    forced_default: Option<bool>,
}
/// Retired owned header payloads must be destroyed after boundary admission.
#[derive(Debug)]
pub struct RetiredSelectedState {
    pub name: Vec<u16>,
    pub default_style: Option<crate::document_style::StyleValue>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StyleRenameError {
    CountOverflow,
    InvalidPreparedValue,
}
/// Style values and replaced entry names leave admission before destruction.
/// Map key/node drops and allocations still occur inside map mutation.
#[derive(Debug)]
pub struct RetiredStyleRename {
    pub map_values: Vec<Option<crate::document_style::StyleValue>>,
    pub entry_names: Vec<Vec<u16>>,
}
fn cstring_prefix(value: &[u16]) -> &[u16] {
    &value[..value.iter().position(|&u| u == 0).unwrap_or(value.len())]
}
fn style_rename_count(
    count: usize,
    old_present: bool,
    new_present_after_remove: bool,
) -> Result<(), StyleRenameError> {
    let remaining = count
        .checked_sub(usize::from(old_present))
        .ok_or(StyleRenameError::CountOverflow)?;
    let final_count = remaining
        .checked_add(usize::from(!new_present_after_remove))
        .ok_or(StyleRenameError::CountOverflow)?;
    if count > i32::MAX as usize || final_count > i32::MAX as usize {
        Err(StyleRenameError::CountOverflow)
    } else {
        Ok(())
    }
}
/// Base Empty retirement order is named values, segments, then entries.
/// Map keys/node storage are still dropped while clearing the map; allocator
/// reentry and native allocation/deletion timing remain outside the contract.
#[derive(Debug)]
pub struct RetiredEmpty {
    pub styles: Vec<Option<crate::document_style::StyleValue>>,
    pub segments: Vec<DocumentSegment>,
    pub entries: Vec<PreparedEntry>,
}
/// Clone creates independent storage; it does not emulate native Copy callbacks
/// or the selective field assignments in native Copy.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Document {
    metadata: [Option<i32>; 3],
    forced_default: Option<bool>,
    par_bits: Option<u64>,
    script: [Option<i32>; 4],
    scaled_bas: Option<bool>,
    interpretation: [Option<i32>; 3],
    name: Vec<u16>,
    path: Vec<u16>,
    default_style: Option<crate::document_style::StyleValue>,
    named_styles: crate::document_style_map::StyleMap<Option<crate::document_style::StyleValue>>,
    entries: Vec<PreparedEntry>,
    segments: Vec<DocumentSegment>,
}
impl Document {
    /// Captures source state at the caller-selected point after native Empty.
    /// Native constructors supply every selected scalar/default before use.
    pub fn selected_state(&self) -> SelectedState {
        SelectedState {
            name: self.name.clone(),
            interpretation: self.interpretation,
            script: self.script,
            scaled_bas: self.scaled_bas,
            default_style: self.default_style.clone(),
            forced_default: self.forced_default,
        }
    }
    /// Successful-allocation header assignment only: no callbacks, named-map
    /// clearing, prepared copy, or native partial-allocation-failure emulation.
    pub fn replace_selected_state(&mut self, state: SelectedState) -> RetiredSelectedState {
        let name = core::mem::replace(&mut self.name, state.name);
        self.interpretation[0] = state.interpretation[0];
        self.interpretation[1] = state.interpretation[1];
        self.script = state.script;
        self.scaled_bas = state.scaled_bas;
        self.interpretation[2] = state.interpretation[2];
        let default_style = core::mem::replace(&mut self.default_style, state.default_style);
        self.forced_default = state.forced_default;
        RetiredSelectedState {
            name,
            default_style,
        }
    }
    pub fn metadata(&self, field: MetadataField) -> Option<i32> {
        self.metadata[field.index()]
    }
    pub fn replace_metadata(&mut self, field: MetadataField, value: i32) -> Option<i32> {
        self.metadata[field.index()].replace(value)
    }
    pub fn forced_default(&self) -> Option<bool> {
        self.forced_default
    }
    pub fn replace_forced_default(&mut self, value: bool) -> Option<bool> {
        self.forced_default.replace(value)
    }
    /// Opaque IEEE double bits: no floating-point operations occur in storage.
    pub fn par_bits(&self) -> Option<u64> {
        self.par_bits
    }
    pub fn replace_par_bits(&mut self, value: u64) -> Option<u64> {
        self.par_bits.replace(value)
    }
    pub fn script_field(&self, field: ScriptField) -> Option<i32> {
        self.script[field.index()]
    }
    pub fn replace_script_field(&mut self, field: ScriptField, value: i32) -> Option<i32> {
        self.script[field.index()].replace(value)
    }
    pub fn scaled_bas(&self) -> Option<bool> {
        self.scaled_bas
    }
    pub fn replace_scaled_bas(&mut self, value: bool) -> Option<bool> {
        self.scaled_bas.replace(value)
    }
    pub fn script_size(&self) -> Option<[i32; 2]> {
        Some([self.script[0]?, self.script[1]?])
    }
    pub fn set_script_size(&mut self, width: i32, height: i32) {
        self.script[0] = Some(width);
        self.script[1] = Some(height);
    }
    /// Absent until explicitly supplied by the native constructor/adapter.
    pub fn interpretation(&self, field: InterpretationField) -> Option<i32> {
        self.interpretation[field.index()]
    }
    pub fn replace_interpretation(
        &mut self,
        field: InterpretationField,
        value: i32,
    ) -> Option<i32> {
        self.interpretation[field.index()].replace(value)
    }
    pub fn identity(&self, field: IdentityField) -> &[u16] {
        match field {
            IdentityField::Name => &self.name,
            IdentityField::Path => &self.path,
        }
    }
    /// Returns old raw units for destruction after boundary admission ends.
    pub fn replace_identity(&mut self, field: IdentityField, value: Vec<u16>) -> Vec<u16> {
        core::mem::replace(
            match field {
                IdentityField::Name => &mut self.name,
                IdentityField::Path => &mut self.path,
            },
            value,
        )
    }
    /// Absent until the native adapter supplies an initialized style.
    pub fn default_style(&self) -> Option<&crate::document_style::StyleValue> {
        self.default_style.as_ref()
    }
    /// Transfers retired storage to the caller for destruction after admission.
    pub fn replace_default_style(
        &mut self,
        value: Option<crate::document_style::StyleValue>,
    ) -> Option<crate::document_style::StyleValue> {
        core::mem::replace(&mut self.default_style, value)
    }

    pub fn named_styles(
        &self,
    ) -> &crate::document_style_map::StyleMap<Option<crate::document_style::StyleValue>> {
        &self.named_styles
    }
    pub fn named_styles_mut(
        &mut self,
    ) -> &mut crate::document_style_map::StyleMap<Option<crate::document_style::StyleValue>> {
        &mut self.named_styles
    }
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

    /// Swap only prepared storage; all default, named and future metadata fields
    /// remain at their respective owners. The caller retires the swapped-out
    /// vectors after any registry admission ends.
    pub fn swap_prepared_storage(&mut self, other: &mut Document) {
        core::mem::swap(&mut self.segments, &mut other.segments);
        core::mem::swap(&mut self.entries, &mut other.entries);
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
    /// Base Empty only: caller retains native virtual dispatch/Deinit. No
    /// callback, default reset, or unrelated metadata reset occurs here.
    pub fn empty(&mut self) -> RetiredEmpty {
        self.set_script_size(0, 0);
        let styles = self.named_styles.clear();
        let segments = core::mem::take(&mut self.segments);
        let entries = core::mem::take(&mut self.entries);
        RetiredEmpty {
            styles,
            segments,
            entries,
        }
    }
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
    /// Prepared conflict relocation only. Native classification/equality/CRT
    /// naming precede this operation; incoming AddStyle insertion follows it.
    /// No stale-plan reclassification or candidate-absence assertion is added.
    pub fn commit_style_rename(
        &mut self,
        old: &[u16],
        new: &[u16],
        previous: &mut Option<crate::document_style::StyleValue>,
    ) -> Result<RetiredStyleRename, StyleRenameError> {
        if previous.is_none() {
            return Err(StyleRenameError::InvalidPreparedValue);
        }
        if old.len() > i32::MAX as usize || new.len() > i32::MAX as usize {
            return Err(StyleRenameError::CountOverflow);
        }
        let old_present = self.named_styles.get(old).is_some();
        let alias = cstring_prefix(old) == cstring_prefix(new);
        let new_present_after_remove =
            self.named_styles.get(new).is_some() && !(alias && old_present);
        style_rename_count(
            self.named_styles.count(),
            old_present,
            new_present_after_remove,
        )?;
        let matches = self
            .entries
            .iter()
            .filter(|entry| cstring_prefix(&entry.style) == cstring_prefix(old))
            .count();
        let key = new.to_vec();
        let mut retired = RetiredStyleRename {
            map_values: Vec::with_capacity(2),
            entry_names: Vec::with_capacity(matches),
        };
        let previous = previous
            .take()
            .ok_or(StyleRenameError::InvalidPreparedValue)?;
        if let Some(value) = self.named_styles.remove(old) {
            retired.map_values.push(value);
        }
        if let Some(value) = self.named_styles.insert(key, Some(previous)) {
            retired.map_values.push(value);
        }
        for entry in &mut self.entries {
            if cstring_prefix(&entry.style) == cstring_prefix(old) {
                let replacement = new.to_vec();
                retired
                    .entry_names
                    .push(core::mem::replace(&mut entry.style, replacement));
            }
        }
        Ok(retired)
    }
    /// The prepared entry-rename suffix of pinned native AddStyle uses Unicode
    /// CString equality: compare raw u16 units only through the first NUL on
    /// each side, without case folding or surrogate interpretation. The entire
    /// supplied replacement is stored; comparison does not shorten storage.
    /// Native style-map lookup, duplicate-style naming and preparation of name3
    /// remain caller responsibilities. No segment rebuild or callback occurs.
    /// Windows primitive evidence establishes the A-NUL-B versus A case; wider
    /// raw-unit cases require the corresponding actual-ATL differential probes.
    pub fn rename_style_cstring(&mut self, old: &[u16], new: &[u16]) {
        let prefix = |units: &[u16]| {
            units
                .iter()
                .position(|&unit| unit == 0)
                .unwrap_or(units.len())
        };
        let old = &old[..prefix(old)];
        for entry in &mut self.entries {
            if &entry.style[..prefix(&entry.style)] == old {
                entry.style = new.to_vec();
            }
        }
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
    fn cstring_rename_ignores_post_nul_units_but_keeps_complete_replacement() {
        let mut d = Document::default();
        let styles = [
            vec![65, 0, 66],
            vec![65],
            vec![65, 0, 67],
            vec![97],
            vec![65, 66],
            vec![65, 0xd800],
        ];
        for style in &styles {
            let mut record = entry(0, 10, -1);
            record.style = style.clone();
            d.add_prepared(record, AddMode::EntryOnly).unwrap();
        }
        d.rebuild_segments();
        d.feedback_animation(0, true).unwrap();
        let segments = d.segments().to_vec();
        let replacement = [78, 0, 0xdfff];
        d.rename_style_cstring(&[65, 0, 90], &replacement);
        for index in 0..3 {
            assert_eq!(d.entries()[index].style, replacement);
        }
        for (index, style) in styles.iter().enumerate().skip(3) {
            assert_eq!(&d.entries()[index].style, style);
        }
        assert_eq!(d.segments(), segments);
        // The existing exact-unit helper remains a separate, narrower contract.
        d.rename_style(&[78], &[1]);
        assert_eq!(d.entries()[0].style, replacement);
    }

    #[test]
    fn cstring_rename_compares_empty_and_unpaired_surrogate_prefixes_as_units() {
        let mut d = Document::default();
        for style in [
            vec![],
            vec![0, 0xd800],
            vec![0xd800],
            vec![0xd800, 0, 0xdc00],
            vec![0xdc00],
            vec![0xd800, 1],
        ] {
            let mut record = entry(0, 10, -1);
            record.style = style;
            d.add_prepared(record, AddMode::EntryOnly).unwrap();
        }
        d.rename_style_cstring(&[0, 77], &[0xdcff, 0, 0xd800]);
        assert_eq!(d.entries()[0].style, [0xdcff, 0, 0xd800]);
        assert_eq!(d.entries()[1].style, [0xdcff, 0, 0xd800]);
        d.rename_style_cstring(&[0xd800, 0, 42], &[]);
        assert!(d.entries()[2].style.is_empty());
        assert!(d.entries()[3].style.is_empty());
        assert_eq!(d.entries()[4].style, [0xdc00]);
        assert_eq!(d.entries()[5].style, [0xd800, 1]);
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
    #[test]
    fn default_style_clone_and_storage_copy_have_distinct_contracts() {
        use crate::document_style::{decode, HEADER_BYTES};
        let mut bytes = alloc::vec![0u8;HEADER_BYTES];
        bytes[72..80].copy_from_slice(&0x7ff0000000000042u64.to_le_bytes());
        let first = decode(&bytes).unwrap();
        bytes[72..80].copy_from_slice(&(-0.0f64).to_bits().to_le_bytes());
        let second = decode(&bytes).unwrap();
        let mut source = Document::default();
        assert!(source.default_style().is_none());
        assert!(source.replace_default_style(Some(first.clone())).is_none());
        assert_eq!(source.clone().default_style(), Some(&first));
        let mut destination = Document::default();
        destination.replace_default_style(Some(second.clone()));
        destination.copy_storage_from(&source);
        assert_eq!(destination.default_style(), Some(&second));
        destination.remove_all_entries();
        destination.clear_entries_and_segments();
        assert_eq!(destination.default_style(), Some(&second));
        assert_eq!(destination.replace_default_style(None), Some(second));
        assert_eq!(source.default_style(), Some(&first));
    }
    #[test]
    fn named_style_clone_and_storage_operations_are_independent() {
        let mut source = Document::default();
        let mut destination = Document::default();
        source
            .named_styles_mut()
            .insert(alloc::vec![65, 0, 0xd800], None);
        destination.named_styles_mut().insert(alloc::vec![66], None);
        let expected = destination.named_styles().clone();
        assert_eq!(source.clone().named_styles(), source.named_styles());
        destination.copy_storage_from(&source);
        destination.remove_all_entries();
        destination.clear_entries_and_segments();
        assert_eq!(destination.named_styles(), &expected);
        let retired = source.named_styles_mut().clear();
        assert_eq!(retired, alloc::vec![None]);
        assert_eq!(destination.named_styles(), &expected);
    }
    #[test]
    fn raw_identity_clone_replace_and_prepared_storage_are_independent() {
        let mut source = Document::default();
        let mut destination = Document::default();
        assert!(source.identity(IdentityField::Name).is_empty());
        assert!(source.identity(IdentityField::Path).is_empty());
        let raw = alloc::vec![65, 0, 0xd800, 0xdcff, 0xffff];
        assert!(source
            .replace_identity(IdentityField::Name, raw.clone())
            .is_empty());
        source.replace_identity(IdentityField::Path, alloc::vec![80, 0, 66]);
        let cloned = source.clone();
        destination.replace_identity(IdentityField::Name, alloc::vec![90]);
        destination.copy_storage_from(&source);
        destination.clear_entries_and_segments();
        destination.remove_all_entries();
        assert_eq!(destination.identity(IdentityField::Name), &[90]);
        assert!(destination.identity(IdentityField::Path).is_empty());
        assert_eq!(
            source.replace_identity(IdentityField::Name, alloc::vec![]),
            raw
        );
        assert_eq!(
            cloned.identity(IdentityField::Name),
            &[65, 0, 0xd800, 0xdcff, 0xffff]
        );
        assert_eq!(source.identity(IdentityField::Path), &[80, 0, 66]);
    }
    #[test]
    fn supplied_interpretation_bits_clone_and_storage_retention() {
        let mut source = Document::default();
        let mut destination = Document::default();
        for field in [
            InterpretationField::Mode,
            InterpretationField::SubtitleType,
            InterpretationField::Encoding,
        ] {
            assert_eq!(source.interpretation(field), None);
            assert_eq!(source.replace_interpretation(field, i32::MIN), None);
            destination.replace_interpretation(field, i32::MAX);
        }
        let cloned = source.clone();
        source.replace_interpretation(InterpretationField::Mode, -1);
        destination.copy_storage_from(&source);
        destination.clear_entries_and_segments();
        destination.remove_all_entries();
        for field in [
            InterpretationField::Mode,
            InterpretationField::SubtitleType,
            InterpretationField::Encoding,
        ] {
            assert_eq!(cloned.interpretation(field), Some(i32::MIN));
            assert_eq!(destination.interpretation(field), Some(i32::MAX));
        }
        assert_eq!(source.interpretation(InterpretationField::Mode), Some(-1));
    }
    #[test]
    fn supplied_script_fields_and_size_have_independent_retention() {
        let mut source = Document::default();
        let mut destination = Document::default();
        assert_eq!(source.script_size(), None);
        assert_eq!(source.scaled_bas(), None);
        for field in [
            ScriptField::Width,
            ScriptField::Height,
            ScriptField::Wrap,
            ScriptField::Collisions,
        ] {
            assert_eq!(source.script_field(field), None);
            source.replace_script_field(field, i32::MIN);
            destination.replace_script_field(field, i32::MAX);
        }
        source.replace_scaled_bas(true);
        destination.replace_scaled_bas(false);
        let cloned = source.clone();
        source.set_script_size(-3, 4);
        assert_eq!(source.script_size(), Some([-3, 4]));
        assert_eq!(source.script_field(ScriptField::Wrap), Some(i32::MIN));
        destination.copy_storage_from(&source);
        destination.clear_entries_and_segments();
        destination.remove_all_entries();
        assert_eq!(destination.script_size(), Some([i32::MAX, i32::MAX]));
        assert_eq!(destination.scaled_bas(), Some(false));
        assert_eq!(cloned.script_size(), Some([i32::MIN, i32::MIN]));
        assert_eq!(cloned.scaled_bas(), Some(true));
        source.set_script_size(0, 0);
        assert_eq!(source.script_field(ScriptField::Collisions), Some(i32::MIN));
        assert_eq!(source.scaled_bas(), Some(true));
    }
    #[test]
    fn supplied_render_metadata_bits_clone_and_storage_retention() {
        let mut source = Document::default();
        let mut destination = Document::default();
        assert_eq!(source.par_bits(), None);
        assert_eq!(source.forced_default(), None);
        for field in [
            MetadataField::PARType,
            MetadataField::ColorMatrix,
            MetadataField::ColorRange,
        ] {
            assert_eq!(source.metadata(field), None);
            source.replace_metadata(field, i32::MIN);
            destination.replace_metadata(field, i32::MAX);
        }
        source.replace_forced_default(true);
        destination.replace_forced_default(false);
        destination.replace_par_bits(0x3ff0000000000000);
        for bits in [
            0u64,
            1 << 63,
            1,
            u64::MAX,
            0x7ff0000000000000,
            0xfff0000000000000,
            0x7ff0000000000001,
            0x7ff8000000000042,
        ] {
            source.replace_par_bits(bits);
            let cloned = source.clone();
            source.replace_par_bits(!bits);
            assert_eq!(cloned.par_bits(), Some(bits));
            destination.copy_storage_from(&source);
            destination.remove_all_entries();
            destination.clear_entries_and_segments();
            assert_eq!(destination.par_bits(), Some(0x3ff0000000000000));
            assert_eq!(destination.forced_default(), Some(false));
            assert_eq!(cloned.forced_default(), Some(true));
            for field in [
                MetadataField::PARType,
                MetadataField::ColorMatrix,
                MetadataField::ColorRange,
            ] {
                assert_eq!(destination.metadata(field), Some(i32::MAX));
                assert_eq!(cloned.metadata(field), Some(i32::MIN));
            }
        }
    }
    #[test]
    fn selected_state_copies_only_header_and_retires_owned_payloads() {
        let mut source = Document::default();
        let mut destination = Document::default();
        let raw = alloc::vec![65, 0, 0xd800, 0xdcff];
        source.replace_identity(IdentityField::Name, raw.clone());
        source.replace_identity(IdentityField::Path, alloc::vec![83]);
        destination.replace_identity(IdentityField::Name, alloc::vec![68]);
        destination.replace_identity(IdentityField::Path, alloc::vec![80, 0, 0xd800]);
        for field in [
            InterpretationField::Mode,
            InterpretationField::SubtitleType,
            InterpretationField::Encoding,
        ] {
            source.replace_interpretation(field, i32::MIN);
            destination.replace_interpretation(field, i32::MAX);
        }
        for field in [
            ScriptField::Width,
            ScriptField::Height,
            ScriptField::Wrap,
            ScriptField::Collisions,
        ] {
            source.replace_script_field(field, -5);
            destination.replace_script_field(field, 7);
        }
        source.replace_scaled_bas(true);
        source.replace_forced_default(true);
        destination.replace_scaled_bas(false);
        destination.replace_forced_default(false);
        let mut wire = alloc::vec![0u8;crate::document_style::HEADER_BYTES];
        wire[72..80].copy_from_slice(&0x7ff0000000000042u64.to_le_bytes());
        let first = crate::document_style::decode(&wire).unwrap();
        wire[72..80].copy_from_slice(&(-0.0f64).to_bits().to_le_bytes());
        let old_default = crate::document_style::decode(&wire).unwrap();
        source.replace_default_style(Some(first.clone()));
        destination.replace_default_style(Some(old_default.clone()));
        destination.replace_par_bits(0x3ff0000000000000);
        source.replace_par_bits(0);
        for field in [
            MetadataField::PARType,
            MetadataField::ColorMatrix,
            MetadataField::ColorRange,
        ] {
            destination.replace_metadata(field, 9);
            source.replace_metadata(field, 3);
        }
        for key in 0..65 {
            destination
                .named_styles_mut()
                .insert(alloc::vec![key], None);
        }
        destination
            .add_prepared(entry(0, 10, -1), AddMode::Add)
            .unwrap();
        destination.feedback_animation(0, true).unwrap();
        destination.remove_all_entries();
        let before = destination.clone();
        let selected = source.selected_state();
        let retired = destination.replace_selected_state(selected.clone());
        assert_eq!(retired.name, alloc::vec![68]);
        assert_eq!(retired.default_style, Some(old_default));
        assert_eq!(destination.selected_state(), selected);
        assert_eq!(destination.default_style(), Some(&first));
        assert_eq!(
            destination.identity(IdentityField::Path),
            before.identity(IdentityField::Path)
        );
        assert_eq!(destination.par_bits(), before.par_bits());
        assert_eq!(destination.metadata, before.metadata);
        assert_eq!(destination.named_styles(), before.named_styles());
        assert_eq!(destination.entries(), before.entries());
        assert_eq!(destination.segments(), before.segments());
        source.replace_identity(IdentityField::Name, alloc::vec![90]);
        source.replace_default_style(None);
        assert_eq!(destination.identity(IdentityField::Name), raw);
        assert_eq!(destination.default_style(), Some(&first));
        let absent = Document::default().selected_state();
        destination.replace_selected_state(absent.clone());
        assert_eq!(destination.selected_state(), absent);
        assert_eq!(destination.named_styles(), before.named_styles());
    }
    fn rename_value(id: u8) -> crate::document_style::StyleValue {
        let mut bytes = alloc::vec![0u8;crate::document_style::HEADER_BYTES];
        bytes[36] = id;
        crate::document_style::decode(&bytes).unwrap()
    }
    #[test]
    fn prepared_style_rename_overwrite_alias_missing_and_retirement() {
        for (old, new, old_present) in [
            (alloc::vec![65], alloc::vec![66, 0, 0xd800], true),
            (alloc::vec![65, 0, 0xd800], alloc::vec![65, 0, 0xdcff], true),
            (alloc::vec![65], alloc::vec![66], false),
            (alloc::vec![0, 0xd800], alloc::vec![0, 0xdcff], true),
        ] {
            let mut d = Document::default();
            if old_present {
                d.named_styles_mut()
                    .insert(old.clone(), Some(rename_value(1)));
            }
            let alias = cstring_prefix(&old) == cstring_prefix(&new);
            if !alias {
                d.named_styles_mut()
                    .insert(new.clone(), Some(rename_value(2)));
            }
            let mut record = entry(0, 10, -1);
            record.style = old.clone();
            d.add_prepared(record, AddMode::Add).unwrap();
            d.feedback_animation(0, true).unwrap();
            let segments = d.segments().to_vec();
            let mut pending = Some(rename_value(3));
            let retired = d.commit_style_rename(&old, &new, &mut pending).unwrap();
            assert!(pending.is_none());
            assert_eq!(d.named_styles().count(), 1);
            assert_eq!(d.named_styles().get(&new), Some(&Some(rename_value(3))));
            assert_eq!(d.entries()[0].style, new);
            assert_eq!(retired.entry_names, alloc::vec![old.clone()]);
            assert_eq!(d.segments(), segments);
            let expected = if old_present && !alias {
                alloc::vec![Some(rename_value(1)), Some(rename_value(2))]
            } else if old_present {
                alloc::vec![Some(rename_value(1))]
            } else {
                alloc::vec![Some(rename_value(2))]
            };
            assert_eq!(retired.map_values, expected);
        }
    }
    #[test]
    fn prepared_style_rename_shrinks_on_remove_before_reinsert() {
        let mut d = Document::default();
        let old = alloc::vec![5000];
        let new = alloc::vec![6000];
        d.named_styles_mut()
            .insert(old.clone(), Some(rename_value(1)));
        for key in 1..200 {
            d.named_styles_mut()
                .insert(alloc::vec![key], Some(rename_value(2)));
        }
        let bins = d.named_styles().bin_count();
        assert!(bins >= 68);
        let threshold = (0.25f32 * bins as f32) as usize;
        for key in 1..200 {
            if d.named_styles().count() == threshold {
                break;
            }
            d.named_styles_mut().remove(&[key]);
        }
        assert_eq!(d.named_styles().bin_count(), bins);
        assert_eq!(d.named_styles().count(), threshold);
        d.commit_style_rename(&old, &new, &mut Some(rename_value(3)))
            .unwrap();
        assert_eq!(d.named_styles().count(), threshold);
        assert!(d.named_styles().bin_count() < bins);
    }
    #[test]
    fn prepared_style_rename_preflight_rejects_without_consumption() {
        let mut d = Document::default();
        d.named_styles_mut()
            .insert(alloc::vec![65], Some(rename_value(1)));
        let before = d.clone();
        let mut none = None;
        assert_eq!(
            d.commit_style_rename(&[65], &[66], &mut none).unwrap_err(),
            StyleRenameError::InvalidPreparedValue
        );
        assert_eq!(d, before);
        let max = i32::MAX as usize;
        assert_eq!(
            style_rename_count(max, false, false),
            Err(StyleRenameError::CountOverflow)
        );
        assert_eq!(style_rename_count(max, true, false), Ok(()));
        assert_eq!(style_rename_count(max, false, true), Ok(()));
        assert_eq!(
            style_rename_count(usize::MAX, true, true),
            Err(StyleRenameError::CountOverflow)
        );
    }
    #[test]
    fn base_empty_retires_stale_storage_and_preserves_full_other_state() {
        let mut d = Document::default();
        d.replace_identity(IdentityField::Name, alloc::vec![65, 0, 0xd800]);
        d.replace_identity(IdentityField::Path, alloc::vec![80, 0, 0xdcff]);
        for field in [
            InterpretationField::Mode,
            InterpretationField::SubtitleType,
            InterpretationField::Encoding,
        ] {
            d.replace_interpretation(field, -7);
        }
        for field in [
            ScriptField::Width,
            ScriptField::Height,
            ScriptField::Wrap,
            ScriptField::Collisions,
        ] {
            d.replace_script_field(field, -9);
        }
        d.replace_scaled_bas(true);
        d.replace_forced_default(true);
        d.replace_par_bits(0x7ff0000000000042);
        for field in [
            MetadataField::PARType,
            MetadataField::ColorMatrix,
            MetadataField::ColorRange,
        ] {
            d.replace_metadata(field, -11);
        }
        d.replace_default_style(Some(rename_value(13)));
        for key in 0..65 {
            d.named_styles_mut()
                .insert(alloc::vec![key], Some(rename_value(key as u8)));
        }
        d.add_prepared(entry(0, 10, -1), AddMode::Add).unwrap();
        d.feedback_animation(0, true).unwrap();
        d.remove_all_entries();
        let before = d.clone();
        let bins = d.named_styles().bin_count();
        let retired = d.empty();
        assert_eq!(
            retired.styles,
            before
                .named_styles()
                .iter()
                .map(|(_, v)| v.clone())
                .collect::<Vec<_>>()
        );
        assert_eq!(retired.segments, before.segments());
        assert!(retired.segments[0].animated);
        assert!(retired.entries.is_empty());
        let mut expected = before.clone();
        expected.script[0] = Some(0);
        expected.script[1] = Some(0);
        expected.named_styles.clear();
        expected.segments.clear();
        expected.entries.clear();
        assert_eq!(d, expected);
        assert_eq!(d.named_styles().bin_count(), bins);
        let again = d.empty();
        assert!(again.styles.is_empty() && again.segments.is_empty() && again.entries.is_empty());
        assert_eq!(d, expected);
        // Selected self-Copy after Empty is unchanged; storage self-copy is empty.
        let selected = d.selected_state();
        d.replace_selected_state(selected);
        assert_eq!(d, expected);
        let raw = entry(0, 10, 0);
        d.add_prepared(raw.clone(), AddMode::EntryOnly).unwrap();
        let retired = d.empty();
        assert_eq!(retired.entries, alloc::vec![raw]);
        assert_eq!(d, expected);
    }
}
