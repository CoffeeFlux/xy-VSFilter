// SPDX-License-Identifier: GPL-2.0-or-later
//! `CSimpleTextSubtitle::CreateSegments`, including empty/reversed entries.
//! Breakpoints from every entry split the timeline, even entries that never
//! become active. Adjacent segments with identical entry lists stay separate.
use alloc::vec::Vec;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Segment {
    pub start: i64,
    pub end: i64,
    pub entries: Vec<usize>,
}

pub fn create(entries: &[(i64, i64)]) -> Vec<Segment> {
    let mut breakpoints = Vec::new();
    for &(start, end) in entries {
        breakpoints.push(start);
        breakpoints.push(end);
    }
    breakpoints.sort_unstable();
    breakpoints.dedup();
    let mut segments: Vec<_> = breakpoints
        .windows(2)
        .map(|pair| Segment {
            start: pair[0],
            end: pair[1],
            entries: Vec::new(),
        })
        .collect();
    for (index, &(start, end)) in entries.iter().enumerate() {
        let first = segments.partition_point(|segment| segment.start < start);
        for segment in &mut segments[first..] {
            if segment.end > end {
                break;
            }
            segment.entries.push(index);
        }
    }
    segments.retain(|segment| !segment.entries.is_empty());
    segments
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;

    #[test]
    fn inactive_entries_still_split_active_intervals() {
        let segments = create(&[(0, 10), (4, 4), (8, 6), (0, 10)]);
        assert_eq!(
            segments
                .iter()
                .map(|s| (s.start, s.end))
                .collect::<Vec<_>>(),
            vec![(0, 4), (4, 6), (6, 8), (8, 10)]
        );
        assert!(segments.iter().all(|s| s.entries == [0, 3]));
    }

    #[test]
    fn preserves_input_order_and_extreme_times_without_subtraction() {
        assert_eq!(
            create(&[(0, i64::MAX), (i64::MIN, 1)]),
            vec![
                Segment {
                    start: i64::MIN,
                    end: 0,
                    entries: vec![1]
                },
                Segment {
                    start: 0,
                    end: 1,
                    entries: vec![0, 1]
                },
                Segment {
                    start: 1,
                    end: i64::MAX,
                    entries: vec![0]
                },
            ]
        );
        assert!(create(&[]).is_empty());
        assert!(create(&[(1, 1), (5, -5)]).is_empty());
    }
}
