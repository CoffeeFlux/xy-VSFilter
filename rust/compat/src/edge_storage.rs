// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned scanline heads and initialized edge records. Index zero remains the
//! native list sentinel; unused edge slots are absent and never fabricated.
use alloc::vec::Vec;
#[derive(Clone, Copy)]
struct Edge {
    next: u32,
    position: i32,
}
pub struct Storage {
    edges: Vec<Option<Edge>>,
    heads: Vec<u32>,
}
impl Storage {
    pub fn new(height: i32, capacity: u32) -> Option<Self> {
        if height < 0 || capacity == 0 {
            return None;
        }
        let mut edges = Vec::new();
        edges.try_reserve_exact(capacity as usize).ok()?;
        edges.resize(capacity as usize, None);
        let mut heads = Vec::new();
        heads.try_reserve_exact(height as usize).ok()?;
        heads.resize(height as usize, 0);
        Some(Self { edges, heads })
    }
    pub fn resize(&mut self, capacity: u32) -> bool {
        let count = capacity as usize;
        if count > self.edges.len()
            && self
                .edges
                .try_reserve_exact(count - self.edges.len())
                .is_err()
        {
            return false;
        }
        self.edges.resize(count, None);
        true
    }
    pub fn insert(&mut self, index: u32, row: i32, position: i32) -> bool {
        if index == 0
            || index as usize >= self.edges.len()
            || row < 0
            || row as usize >= self.heads.len()
        {
            return false;
        }
        let next = self.heads[row as usize];
        self.edges[index as usize] = Some(Edge { next, position });
        self.heads[row as usize] = index;
        true
    }
    pub fn query(&self, field: u32, index: usize) -> Option<i64> {
        match field {
            0 => self.heads.get(index).map(|v| i64::from(*v)),
            1 => self
                .edges
                .get(index)
                .copied()
                .flatten()
                .map(|e| i64::from(e.next)),
            2 => self
                .edges
                .get(index)
                .copied()
                .flatten()
                .map(|e| i64::from(e.position)),
            3 => Some(self.edges.len() as i64),
            4 => Some(self.heads.len() as i64),
            _ => None,
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn row_heads_retain_reverse_insertion_order_across_growth() {
        let mut edges = Storage::new(2, 4).unwrap();
        assert_eq!(edges.query(0, 0), Some(0));
        assert_eq!(edges.query(1, 0), None);
        assert!(edges.insert(1, 0, i32::MIN));
        assert!(edges.insert(2, 1, i32::MAX));
        assert!(edges.insert(3, 0, -1));
        assert!(edges.resize(8));
        assert_eq!(edges.query(0, 0), Some(3));
        assert_eq!(edges.query(1, 3), Some(1));
        assert_eq!(edges.query(1, 1), Some(0));
        assert_eq!(edges.query(2, 1), Some(i64::from(i32::MIN)));
        assert_eq!(edges.query(2, 7), None);
        assert!(!edges.insert(0, 0, 7));
        assert!(!edges.insert(4, 2, 7));
        assert_eq!(edges.query(0, 0), Some(3));
    }
    #[test]
    fn empty_height_has_no_rows_and_rejected_shapes_do_not_allocate() {
        let edges = Storage::new(0, 1).unwrap();
        assert_eq!(edges.query(0, 0), None);
        assert!(Storage::new(-1, 1).is_none());
        assert!(Storage::new(1, 0).is_none());
    }
}
