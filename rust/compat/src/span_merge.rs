// SPDX-License-Identifier: GPL-2.0-or-later
//! Ordered outline-span merging. Offset construction remains native.
use alloc::vec::Vec;
pub type Span = [u64; 2];

pub fn merge(a: &[Span], b: &[Span], offset1: u64, offset2: u64) -> Option<Vec<Span>> {
    let mut output = Vec::new();
    output
        .try_reserve_exact(a.len().checked_add(b.len())?)
        .ok()?;
    let (mut ia, mut ib) = (0, 0);
    while ia < a.len() && ib < b.len() {
        let x1;
        let mut x2;
        if b[ib][0].wrapping_add(offset1) < a[ia][0] {
            x1 = b[ib][0].wrapping_add(offset1);
            x2 = b[ib][1].wrapping_add(offset2);
            ib += 1;
            loop {
                while ia < a.len() && a[ia][0] <= x2 {
                    x2 = x2.max(a[ia][1]);
                    ia += 1;
                }
                if ib == b.len() || b[ib][0].wrapping_add(offset1) > x2 {
                    break;
                }
                loop {
                    x2 = x2.max(b[ib][1].wrapping_add(offset2));
                    ib += 1;
                    if ib == b.len() || b[ib][0].wrapping_add(offset1) > x2 {
                        break;
                    }
                }
                if ia == a.len() || a[ia][0] > x2 {
                    break;
                }
            }
        } else {
            x1 = a[ia][0];
            x2 = a[ia][1];
            ia += 1;
            loop {
                while ib < b.len() && b[ib][0].wrapping_add(offset1) <= x2 {
                    x2 = x2.max(b[ib][1].wrapping_add(offset2));
                    ib += 1;
                }
                if ia == a.len() || a[ia][0] > x2 {
                    break;
                }
                loop {
                    x2 = x2.max(a[ia][1]);
                    ia += 1;
                    if ia == a.len() || a[ia][0] > x2 {
                        break;
                    }
                }
                if ib == b.len() || b[ib][0].wrapping_add(offset1) > x2 {
                    break;
                }
            }
        }
        output.push([x1, x2]);
    }
    // The pinned leftover-A loop does not advance its iterator inside the loop.
    // Do not turn that unresolved native control flow into a successful merge.
    if ia < a.len() {
        return None;
    }
    while ib < b.len() {
        let x1 = b[ib][0].wrapping_add(offset1);
        let mut x2 = b[ib][1].wrapping_add(offset2);
        ib += 1;
        while ib < b.len() && b[ib][0].wrapping_add(offset1) <= x2 {
            x2 = x2.max(b[ib][1].wrapping_add(offset2));
            ib += 1;
        }
        output.push([x1, x2]);
    }
    Some(output)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn ties_and_touching_spans_merge_without_sorting() {
        assert_eq!(
            merge(&[[1, 3], [8, 9]], &[[1, 8], [12, 13]], 0, 0).unwrap(),
            [[1, 9], [12, 13]]
        );
        assert_eq!(merge(&[], &[[9, 2], [1, 4]], 0, 0).unwrap(), [[9, 4]]);
    }
    #[test]
    fn offsets_wrap_and_unresolved_leftovers_decline() {
        assert_eq!(merge(&[], &[[0, 3]], u64::MAX, 1).unwrap(), [[u64::MAX, 4]]);
        assert_eq!(merge(&[[9, 10]], &[], 0, 0), None);
        assert_eq!(merge(&[[9, 10]], &[[1, 2]], 0, 0), None);
    }
}
