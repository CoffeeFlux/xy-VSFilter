// SPDX-License-Identifier: GPL-2.0-or-later
//! Integer ellipse arc generation, preserving native update and fill order.
//! None slots are not exported, so native untouched storage remains untouched.
use alloc::vec::Vec;
pub struct Arc {
    cells: Vec<Option<i32>>,
}
impl Arc {
    pub fn len(&self) -> usize {
        self.cells.len()
    }
    pub fn is_empty(&self) -> bool {
        self.cells.is_empty()
    }
    pub fn cell(&self, index: usize) -> Option<i32> {
        self.cells.get(index).copied().flatten()
    }
    fn put(&mut self, index: i64, value: i64) -> Option<()> {
        *self.cells.get_mut(usize::try_from(index).ok()?)? = Some(i32::try_from(value).ok()?);
        Some(())
    }
    fn symmetric(&mut self, ry: i64, y: i64, x: i64) -> Option<()> {
        self.put(ry + y, x)?;
        self.put(ry - y, x)
    }
}
pub fn generate(rx: i32, ry: i32) -> Option<Arc> {
    // The production ellipse caller supplies nonnegative radii. Signed radius
    // normalization in other native callers retains the original fallback.
    if rx < 0 || ry < 0 {
        return None;
    }
    let count = (ry as usize).checked_mul(2)?.checked_add(1)?;
    let mut cells = Vec::new();
    cells.try_reserve_exact(count).ok()?;
    cells.resize(count, None);
    let mut arc = Arc { cells };
    let a = i64::from(rx);
    let b = i64::from(ry);
    if rx == 0 {
        for cell in &mut arc.cells {
            *cell = Some(0);
        }
        return Some(arc);
    }
    if ry == 0 {
        arc.put(0, a)?;
        return Some(arc);
    }
    let aa = a.checked_mul(a)?;
    let bb = b.checked_mul(b)?;
    let a2 = aa.checked_mul(2)?;
    let b2 = bb.checked_mul(2)?;
    let mut x = a;
    let mut y = 0i64;
    let mut dx = bb.checked_mul(1i64.checked_sub(a.checked_mul(2)?)?)?;
    let mut dy = aa;
    let mut err = 0i64;
    let mut stopx = b2.checked_mul(a)?;
    let mut stopy = 0i64;
    while stopx >= stopy {
        arc.symmetric(b, y, -x)?;
        y = y.checked_add(1)?;
        stopy = stopy.checked_add(a2)?;
        err = err.checked_add(dy)?;
        dy = dy.checked_add(a2)?;
        if err.checked_mul(2)?.checked_add(dx)? > 0 {
            x = x.checked_sub(1)?;
            stopx = stopx.checked_sub(b2)?;
            err = err.checked_add(dx)?;
            dx = dx.checked_add(b2)?;
        }
    }
    let mut last_y_stop = y;
    x = 0;
    y = b;
    dx = bb;
    dy = aa.checked_mul(1i64.checked_sub(b.checked_mul(2)?)?)?;
    err = 0;
    stopx = 0;
    stopy = a2.checked_mul(b)?;
    while stopx <= stopy {
        arc.symmetric(b, y, -x)?;
        x = x.checked_add(1)?;
        stopx = stopx.checked_add(b2)?;
        err = err.checked_add(dx)?;
        dx = dx.checked_add(b2)?;
        if err.checked_mul(2)?.checked_add(dy)? > 0 {
            y = y.checked_sub(1)?;
            stopy = stopy.checked_sub(a2)?;
            err = err.checked_add(dy)?;
            dy = dy.checked_add(a2)?;
            arc.symmetric(b, y, -x)?;
        }
    }
    while y > last_y_stop {
        y -= 1;
        let value = arc.cell(usize::try_from(b + y + 1).ok()?)?;
        arc.symmetric(b, y, i64::from(value))?;
        if y <= last_y_stop {
            break;
        }
        // Intentionally read the native BASE-relative index, not the centered
        // pointer used by the surrounding writes. Preserve this asymmetry.
        let value = arc.cell(usize::try_from(last_y_stop - 1).ok()?)?;
        arc.symmetric(b, last_y_stop, i64::from(value))?;
        last_y_stop += 1;
    }
    Some(arc)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn degenerate_axes_keep_the_native_sign_and_length() {
        let arc = generate(7, 0).unwrap();
        assert_eq!(arc.len(), 1);
        assert_eq!(arc.cell(0), Some(7));
        assert_eq!(arc.cell(1), None);
        let arc = generate(0, 2).unwrap();
        assert_eq!(arc.len(), 5);
        assert!((0..5).all(|i| arc.cell(i) == Some(0)));
        assert!(generate(-1, 1).is_none());
        assert!(generate(1, -1).is_none());
    }
    #[test]
    fn intermediate_overflow_declines_without_exposing_partial_cells() {
        assert!(generate(i32::MAX, 2).is_none());
    }
    #[test]
    fn integer_circle_keeps_native_asymmetric_increment_results() {
        let arc = generate(2, 2).unwrap();
        let cells: Vec<_> = (0..arc.len()).map(|i| arc.cell(i)).collect();
        assert_eq!(cells, [Some(-1), Some(-2), Some(-2), Some(-2), Some(-1)]);
    }
}
