// SPDX-License-Identifier: GPL-2.0-or-later
//! The segment-mutation tail of `CSimpleTextSubtitle::Add`. Return edits in
//! native order so the adapter retains native array identity, insertion order,
//! animation flags and partially applied edits if a native allocation throws.
use crate::segments::Segment;
use alloc::{vec, vec::Vec};

#[derive(Debug, PartialEq, Eq)]
pub enum Edit {
    SplitStart(usize, i64),
    SplitEnd(usize, i64),
    Insert(usize, i64, i64, usize),
    AppendEntry(usize, usize),
    InsertEntry(usize, usize, usize),
}

pub fn plan(
    mut segments: Vec<Segment>,
    readorders: &[i32],
    start: i64,
    end: i64,
    entry: usize,
    mut i: usize,
) -> Option<Vec<Edit>> {
    if segments.is_empty() || i > segments.len() || start >= end {
        return None;
    }
    let order = *readorders.get(entry)?;
    if segments
        .iter()
        .flat_map(|s| &s.entries)
        .any(|&e| e >= readorders.len())
    {
        return None;
    }
    let mut edits = Vec::new();
    if i > 0 && segments[i - 1].end > start {
        let prefix = Segment {
            start: segments[i - 1].start,
            end: start,
            entries: segments[i - 1].entries.clone(),
        };
        segments[i - 1].start = start;
        edits.push(Edit::SplitStart(i - 1, start));
        segments.insert(i - 1, prefix);
    } else if i < segments.len() && start < segments[i].start {
        let prefix = Segment {
            start,
            end: end.min(segments[i].start),
            entries: vec![entry],
        };
        edits.push(Edit::Insert(i, prefix.start, prefix.end, entry));
        segments.insert(i, prefix);
        i += 1;
    }
    let mut last_end = i64::MAX;
    while i < segments.len() && segments[i].start < end {
        if last_end < segments[i].start {
            let gap = Segment {
                start: last_end,
                end: segments[i].start,
                entries: vec![entry],
            };
            last_end = segments[i].start;
            edits.push(Edit::Insert(i, gap.start, gap.end, entry));
            segments.insert(i, gap);
        } else {
            if end < segments[i].end {
                let suffix = Segment {
                    start: end,
                    end: segments[i].end,
                    entries: segments[i].entries.clone(),
                };
                segments[i].end = end;
                edits.push(Edit::SplitEnd(i, end));
                segments.insert(i + 1, suffix);
            }
            let entries = &mut segments[i].entries;
            if entries.last().is_none_or(|&old| order >= readorders[old]) {
                entries.push(entry);
                edits.push(Edit::AppendEntry(i, entry));
            } else if let Some(position) = entries.iter().position(|&old| order < readorders[old]) {
                entries.insert(position, entry);
                edits.push(Edit::InsertEntry(i, position, entry));
            }
            last_end = segments[i].end;
        }
        i += 1;
    }
    let previous_end = segments.get(i.checked_sub(1)?)?.end;
    if end > previous_end {
        edits.push(Edit::Insert(i, start.max(previous_end), end, entry));
    }
    Some(edits)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn splitting_keeps_native_edit_order_and_equal_readorder_appends() {
        let edits = plan(
            vec![Segment {
                start: 0,
                end: 10,
                entries: vec![0],
            }],
            &[4, 4],
            2,
            8,
            1,
            1,
        )
        .unwrap();
        assert_eq!(
            edits,
            vec![
                Edit::SplitStart(0, 2),
                Edit::SplitEnd(1, 8),
                Edit::AppendEntry(1, 1)
            ]
        );
    }
    #[test]
    fn last_entry_shortcut_does_not_sort_an_unsorted_list() {
        let edits = plan(
            vec![Segment {
                start: 0,
                end: 10,
                entries: vec![0, 1],
            }],
            &[9, 1, 2],
            0,
            10,
            2,
            0,
        )
        .unwrap();
        assert_eq!(edits, vec![Edit::AppendEntry(0, 2)]);
    }
    #[test]
    fn invalid_native_indices_are_not_dereferenced() {
        assert!(plan(
            vec![Segment {
                start: 0,
                end: 10,
                entries: vec![usize::MAX]
            }],
            &[0],
            0,
            1,
            0,
            0
        )
        .is_none());
    }
}
