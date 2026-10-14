// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned ellipse intersection cells. Native float calculations supply values;
//! an unwritten cell remains absent rather than acquiring a default coordinate.
use alloc::vec::Vec;
pub struct Matrix {
    rx: i32,
    ry: i32,
    width: usize,
    cells: Vec<Option<i32>>,
}
impl Matrix {
    pub fn new(rx: i32, ry: i32) -> Option<Self> {
        if rx < 0 || ry <= 0 {
            return None;
        }
        let width = (rx as usize).checked_mul(2)?.checked_add(1)?;
        let height = (ry as usize).checked_mul(2)?.checked_add(1)?;
        let count = width.checked_mul(height)?;
        // The native caller only builds tables within this existing threshold.
        if count > 129 * 129 {
            return None;
        }
        let mut cells = Vec::new();
        cells.try_reserve_exact(count).ok()?;
        cells.resize(count, None);
        Some(Self {
            rx,
            ry,
            width,
            cells,
        })
    }
    fn index(&self, dy: i32, dx: i32) -> Option<usize> {
        if dx < -self.rx || dx > self.rx || dy < -2 * self.ry || dy > 0 {
            return None;
        }
        Some((dy + 2 * self.ry) as usize * self.width + (dx + self.rx) as usize)
    }
    pub fn set(&mut self, dy: i32, dx: i32, value: i32) -> bool {
        let Some(index) = self.index(dy, dx) else {
            return false;
        };
        self.cells[index] = Some(value);
        true
    }
    pub fn get(&self, dy: i32, dx: i32) -> Option<i32> {
        self.index(dy, dx).and_then(|index| self.cells[index])
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn absent_rows_and_extreme_values_remain_distinct() {
        let mut matrix = Matrix::new(2, 3).unwrap();
        assert_eq!(matrix.get(0, 0), None);
        assert!(matrix.set(-6, -2, i32::MIN));
        assert!(matrix.set(-1, 2, i32::MAX));
        assert_eq!(matrix.get(-6, -2), Some(i32::MIN));
        assert_eq!(matrix.get(-1, 2), Some(i32::MAX));
        assert_eq!(matrix.get(0, 0), None);
        assert!(!matrix.set(-7, 0, 1));
        assert!(!matrix.set(-1, 3, 1));
        assert_eq!(matrix.get(-1, 2), Some(i32::MAX));
    }
    #[test]
    fn native_threshold_and_degenerate_width_are_preserved() {
        assert!(Matrix::new(64, 64).is_some());
        assert!(Matrix::new(65, 65).is_none());
        let mut matrix = Matrix::new(0, 1).unwrap();
        assert!(matrix.set(-2, 0, 42));
        assert_eq!(matrix.get(-2, 0), Some(42));
        assert!(Matrix::new(-1, 2).is_none());
        assert!(Matrix::new(2, 0).is_none());
        assert!(Matrix::new(i32::MAX, i32::MAX).is_none());
    }
}
