// SPDX-License-Identifier: GPL-3.0-or-later
//! Circular outline-segment selection; native appends retain their exception order.
#[repr(C)]
#[derive(Debug, PartialEq, Eq)]
pub struct Segment {
    pub begin: i32,
    pub end: i32,
    pub last: i32,
    pub start_x: i32,
    pub start_y: i32,
    pub end_x: i32,
    pub end_y: i32,
}

pub fn select(
    count: usize,
    start: i32,
    mut direction: impl FnMut(usize) -> i32,
    mut point: impl FnMut(usize) -> [i32; 2],
) -> Option<Segment> {
    let len = i32::try_from(count).ok()?;
    if start < 0 || start >= len {
        return None;
    }
    let mut current = start;
    let mut first = -1;
    let mut last = -1;
    let mut last_direction = 0;
    for _ in 0..len {
        current = (current + 1) % len;
        let value = direction(current as usize);
        if value == 0 {
            continue;
        }
        if first == -1 {
            first = current;
        }
        if last_direction == value {
            let a = point(first as usize);
            let b = point(start as usize);
            let c = point(last as usize);
            let d = point(current as usize);
            let start_x = a[0].checked_add(b[0])? >> 1;
            let start_y = a[1].checked_add(b[1])? >> 1;
            let end_x = c[0].checked_add(d[0])? >> 1;
            let end_y = c[1].checked_add(d[1])? >> 1;
            if first < start {
                first = first.checked_add(len)?;
            }
            let mut begin = (start.checked_add(first)? >> 1).checked_add(1)?;
            if begin >= len {
                begin -= len;
            }
            if current < last {
                current = current.checked_add(len)?;
            }
            let mut end = last.checked_add(current)?.checked_add(1)? >> 1;
            if end >= len {
                end -= len;
            }
            return Some(Segment {
                begin,
                end,
                last,
                start_x,
                start_y,
                end_x,
                end_y,
            });
        }
        last_direction = value;
        last = current;
    }
    None
}

/// Select the longest outline section, preferring an interior section on ties.
/// Missing maxima decline so native assertion ordering is preserved. Midpoints
/// use the original UINT addition width, even on 64-bit Windows.
pub fn split(lengths: &[u32], starts: &[u32], ends: &[u32]) -> Option<[usize; 2]> {
    if lengths.len() != starts.len() || lengths.len() != ends.len() {
        return None;
    }
    let mut longest = 0;
    let mut longest_inner = 0;
    let mut index = None;
    let mut inner_index = None;
    for (i, &length) in lengths.iter().enumerate() {
        if longest < length {
            longest = length;
            index = Some(i);
        }
        if longest_inner < length && i > 0 && i < lengths.len() - 1 {
            longest_inner = length;
            inner_index = Some(i);
        }
    }
    let index = index?;
    let inner_index = inner_index?;
    let selected = if longest == longest_inner {
        inner_index
    } else {
        index
    };
    let sum = starts[selected].wrapping_add(ends[selected]);
    Some([(sum >> 1) as usize, (sum.wrapping_add(1) >> 1) as usize])
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn wrap_selection_and_negative_midpoint_rounding_are_retained() {
        let points = [[-2, 1], [-1, 3], [4, -5]];
        assert_eq!(
            select(3, 0, |_| 1, |i| points[i]),
            Some(Segment {
                begin: 1,
                end: 2,
                last: 1,
                start_x: -2,
                start_y: 2,
                end_x: 1,
                end_y: -1
            })
        );
        assert!(select(4, 0, |_| 0, |_| panic!("no point access")).is_none());
        assert!(select(
            0,
            0,
            |_| panic!("no direction access"),
            |_| panic!("no point access")
        )
        .is_none());
    }
    #[test]
    fn split_prefers_the_first_tied_inner_run_and_keeps_uint_wrapping() {
        assert_eq!(
            split(&[5, 5, 5, 2], &[0, 5, 10, 15], &[5, 10, 15, 17]),
            Some([7, 8])
        );
        assert_eq!(
            split(&[1, 9, 2], &[0, u32::MAX, 7], &[1, 0, 9]),
            Some([0x7fffffff, 0])
        );
        assert_eq!(split(&[1, 0, 1], &[0, 1, 1], &[1, 1, 2]), None);
    }
}
