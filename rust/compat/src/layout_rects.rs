// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned screen-layout records. Native rectangle geometry and segment-distance
//! arithmetic remain in the caller; order and first-match reuse are preserved.
use alloc::vec::Vec;
#[derive(Default)]
pub struct Records {
    entries: Vec<[i32; 7]>,
}
impl Records {
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
}
