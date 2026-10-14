// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned screen-layout records, ordered collision passes and segment retention.
//! Native border geometry remains in the caller; arithmetic declines preserve
//! the original state so the caller can use its existing fallback.
use alloc::vec::Vec;
#[derive(Default)]
pub struct Records {
    entries: Vec<[i32; 7]>,
}
impl Records {
    /// Return false before mutation if native signed distance/abs is outside
    /// its representable domain. The caller retains its existing fallback.
    pub fn advance(&mut self, segment: i32, active: &[i32]) -> bool {
        if self.entries.iter().any(|r| {
            r[4].checked_sub(segment)
                .and_then(i32::checked_abs)
                .is_none()
        }) {
            return false;
        }
        self.entries.retain_mut(|record| {
            let keep = (record[4] - segment).abs() <= 1 && active.contains(&record[5]);
            if keep {
                record[4] = segment;
            }
            keep
        });
        true
    }
    /// Preserve the native ordered collision passes. The supplied rectangle
    /// already includes native border inflation. Failure leaves it unchanged.
    pub fn place(&self, mut rect: [i32; 4], layer: i32, down: bool) -> Option<[i32; 4]> {
        loop {
            let mut ok = true;
            for record in &self.entries {
                let intersects = rect[0].max(record[0]) < rect[2].min(record[2])
                    && rect[1].max(record[1]) < rect[3].min(record[3]);
                if layer == record[6] && intersects {
                    let height = rect[3].checked_sub(rect[1])?;
                    if down {
                        rect[3] = record[3].checked_add(height)?;
                        rect[1] = record[3];
                    } else {
                        rect[1] = record[1].checked_sub(height)?;
                        rect[3] = record[1];
                    }
                    ok = false;
                }
            }
            if ok {
                return Some(rect);
            }
        }
    }
    pub fn clear(&mut self) {
        self.entries.clear();
    }
    pub fn add(&mut self, record: [i32; 7]) {
        self.entries.push(record);
    }
    pub fn count(&self) -> usize {
        self.entries.len()
    }
    pub fn field(&self, index: usize, field: usize) -> i32 {
        self.entries
            .get(index)
            .and_then(|r| r.get(field))
            .copied()
            .unwrap_or(0)
    }
    pub fn find(&self, segment: i32, entry: i32) -> Option<usize> {
        self.entries
            .iter()
            .position(|r| r[4] == segment && r[5] == entry)
    }
    pub fn segment(&mut self, index: usize, segment: i32) {
        if let Some(record) = self.entries.get_mut(index) {
            record[4] = segment;
        }
    }
    pub fn remove(&mut self, index: usize) {
        if index < self.entries.len() {
            self.entries.remove(index);
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn first_match_ignores_layer_and_removal_retains_survivor_order() {
        let mut records = Records::default();
        records.add([1, 2, 3, 4, 5, 6, 7]);
        records.add([11, 12, 13, 14, 5, 6, -7]);
        assert_eq!(records.find(5, 6), Some(0));
        records.segment(0, 4);
        assert_eq!(records.find(5, 6), Some(1));
        records.remove(0);
        assert_eq!(records.field(0, 0), 11);
        assert_eq!(records.find(5, 6), Some(0));
        records.clear();
        assert_eq!(records.count(), 0);
        assert_eq!(records.find(5, 6), None);
    }
    #[test]
    fn collision_passes_preserve_order_and_revisit_earlier_records() {
        let mut records = Records::default();
        records.add([0, 20, 10, 30, 0, 0, 0]);
        records.add([0, 0, 10, 20, 0, 1, 0]);
        assert_eq!(
            records.place([0, 0, 10, 10], 0, true),
            Some([0, 30, 10, 40])
        );
        assert_eq!(records.place([0, 0, 10, 10], 1, true), Some([0, 0, 10, 10]));
        assert_eq!(
            records.place([0, 10, 10, 30], 0, false),
            Some([0, -20, 10, 0])
        );
        assert!(records.advance(1, &[1]));
        assert_eq!(records.count(), 1);
        assert_eq!(records.field(0, 4), 1);
        assert_eq!(records.field(0, 5), 1);
        assert!(records.advance(0, &[1]));
        assert_eq!(records.field(0, 4), 0);
        assert!(records.advance(2, &[1]));
        assert_eq!(records.count(), 0);
    }
    #[test]
    fn arithmetic_declines_are_transactional() {
        let mut records = Records::default();
        records.add([0, 0, 10, 1, 0, 0, 0]);
        records.add([0, 1, 10, i32::MAX, i32::MIN, 1, 0]);
        assert!(!records.advance(0, &[0]));
        assert_eq!(records.count(), 2);
        assert_eq!(records.field(0, 4), 0);
        assert_eq!(records.field(1, 4), i32::MIN);
        assert_eq!(records.place([0, 0, 10, 10], 0, true), None);
        assert_eq!(records.count(), 2);
    }
}
