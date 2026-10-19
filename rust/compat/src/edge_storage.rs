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
    /// Insert one ordered line as a single admitted operation. Invalid row or
    /// counter ranges are unresolved native memory accesses, not clipped lines.
    /// Errors: 3 invalid/unresolved shape, 5 allocation failure.
    pub fn line(
        &mut self,
        next: u32,
        start: crate::path_align::Point,
        end: crate::path_align::Point,
        up: bool,
    ) -> Result<[u32; 2], u32> {
        let line = crate::scan_line::Line::new(start, end, up).ok_or(3u32)?;
        let capacity = u32::try_from(self.edges.len()).map_err(|_| 3u32)?;
        if next == 0 || next > capacity {
            return Err(3);
        }
        let Some((first, last)) = line.rows() else {
            return Ok([next, capacity]);
        };
        if first < 0 || last as usize >= self.heads.len() {
            return Err(3);
        }
        let count = (last - first + 1) as u32;
        let required = next.checked_add(count).ok_or(3u32)?;
        let mut grown = capacity;
        while required > grown {
            grown = grown.checked_mul(2).filter(|v| *v > grown).ok_or(3u32)?;
        }
        if grown != capacity && !self.resize(grown) {
            return Err(5);
        }
        for (index, (row, position)) in line.enumerate() {
            // All indices and row bounds were checked before allocation or
            // mutation. No raw pointer or unchecked indexing is necessary.
            let inserted = self.insert(next + index as u32, row, position);
            debug_assert!(inserted);
        }
        Ok([required, grown])
    }
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
    pub fn spans(&self, row: i32) -> Option<crate::scan_row::Row> {
        let mut index = *self.heads.get(usize::try_from(row).ok()?)?;
        let mut positions = Vec::new();
        while index != 0 {
            // A valid native row cannot visit more slots than the backing store.
            // Decline an invalid link/cycle without exporting a partial result.
            if positions.len() >= self.edges.len() {
                return None;
            }
            let edge = self.edges.get(index as usize).copied().flatten()?;
            positions.try_reserve(1).ok()?;
            positions.push(edge.position);
            index = edge.next;
        }
        crate::scan_row::Row::new(positions, row)
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn line_growth_retains_heads_and_invalid_rows_do_not_publish_edges() {
        use crate::path_align::Point;
        let mut edges = Storage::new(3, 2).unwrap();
        assert!(edges.insert(1, 0, 99));
        assert_eq!(
            edges.line(2, Point { x: 0, y: 0 }, Point { x: 16, y: 24 }, true),
            Ok([5, 8])
        );
        assert_eq!(edges.query(0, 0), Some(2));
        assert_eq!(edges.query(1, 2), Some(1));
        assert_eq!(edges.query(0, 2), Some(4));
        assert_eq!(
            edges.line(5, Point { x: 0, y: -8 }, Point { x: 0, y: 24 }, false),
            Err(3)
        );
        assert_eq!(edges.query(0, 0), Some(2));
        assert_eq!(edges.query(3, 0), Some(8));
    }
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
