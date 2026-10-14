// SPDX-License-Identifier: GPL-2.0-or-later
//! Signed edge sorting and nonzero winding, with the native packed coordinates.
use alloc::vec::Vec;

pub struct Row(Vec<[u64; 2]>);
impl Row {
    pub fn new(mut positions: Vec<i32>, y: i32) -> Option<Self> {
        if y < 0 {
            return None;
        }
        positions.sort_unstable();
        let mut spans = Vec::new();
        spans.try_reserve_exact(positions.len() / 2).ok()?;
        let mut count = 0i32;
        let mut start = 0;
        for x in positions {
            if count == 0 {
                start = x >> 1;
            }
            count = count.checked_add(if x & 1 != 0 { 1 } else { -1 })?;
            if count == 0 && x >> 1 > start {
                // Preserve the signed native addition order before conversion
                // to the unsigned outline fields. Decline before publication.
                let pack = |coordinate: i32| {
                    (i64::from(y) << 32)
                        .checked_add(i64::from(coordinate))?
                        .checked_add(0x4000_0000_4000_0000)
                        .map(|v| v as u64)
                };
                spans.push([pack(start)?, pack(x >> 1)?]);
            }
        }
        // An unfinished winding is discarded, including negative windings.
        Some(Self(spans))
    }
    pub fn field(&self, field: u32, index: usize) -> Option<u64> {
        match field {
            0 => Some(self.0.len() as u64),
            1 | 2 => self.0.get(index).map(|span| span[(field - 1) as usize]),
            _ => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;
    const BIAS: u64 = 0x4000_0000_4000_0000;
    #[test]
    fn touching_edges_stay_separate_and_either_winding_direction_is_filled() {
        assert_eq!(
            Row::new(vec![1, 4, 5, 8], 0).unwrap().0,
            [[BIAS, BIAS + 2], [BIAS + 2, BIAS + 4]]
        );
        assert_eq!(
            Row::new(vec![0, 5], 1).unwrap().0,
            [[BIAS + (1 << 32), BIAS + (1 << 32) + 2]]
        );
        assert!(Row::new(vec![1, 3, 4], 0).unwrap().0.is_empty());
        assert!(Row::new(vec![0, 1], 0).unwrap().0.is_empty());
    }
    #[test]
    fn signed_coordinates_and_packed_overflow_are_explicit() {
        assert_eq!(
            Row::new(vec![i32::MIN + 1, i32::MAX - 1], 0).unwrap().0,
            [[BIAS - (1 << 30), BIAS + (1 << 30) - 1]]
        );
        assert!(Row::new(vec![1, 4], i32::MAX).is_none());
        assert!(Row::new(vec![1, 4], -1).is_none());
        assert!(Row::new(vec![1], i32::MAX).unwrap().0.is_empty());
    }
}
