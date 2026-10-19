// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned cleanup ellipse cache. Arithmetic instructions are supplied separately;
//! all arc, scratch and intersection-table indexing stays in safe Rust.
use crate::{
    ellipse_arc::{self, Arc},
    ellipse_matrix::{Matrix, Scratch},
    wide_region::Ellipse,
};

pub trait Numeric {
    fn new(rx: i32, ry: i32) -> Self;
    fn table_setup(&mut self, ry: i32);
    fn table_sample(&mut self, y: i32, dy: i32) -> i32;
    fn analytic(&mut self, dx: i32, dy: i32) -> i32;
}
struct State<N> {
    rx: i32,
    ry: i32,
    arc: Arc,
    matrix: Option<Matrix>,
    numeric: N,
}
pub struct Cache<N> {
    state: Option<State<N>>,
}
impl<N> Default for Cache<N> {
    fn default() -> Self {
        Self::new()
    }
}
impl<N> Cache<N> {
    pub const fn new() -> Self {
        Self { state: None }
    }
}
impl<N: Numeric> Ellipse for Cache<N> {
    fn initialize(&mut self, rx: i32, ry: i32) -> Result<(), u32> {
        if self
            .state
            .as_ref()
            .is_some_and(|s| s.rx == rx && s.ry == ry)
        {
            return Ok(());
        }
        self.state = None;
        if rx < 0 || ry <= 0 {
            return Err(6);
        }
        let arc = ellipse_arc::generate(rx, ry).ok_or(6u32)?;
        let mut numeric = N::new(rx, ry);
        // Preserve the native signed multiplication and threshold. A wrapped
        // small allocation that cannot hold its logical table is unresolved.
        let count = ry
            .wrapping_mul(2)
            .wrapping_add(1)
            .wrapping_mul(rx.wrapping_mul(2).wrapping_add(1));
        let matrix = if count <= 129 * 129 {
            let mut matrix = Matrix::new(rx, ry).ok_or(6u32)?;
            numeric.table_setup(ry);
            let mut scratch = Scratch::new(ry).ok_or(5u32)?;
            if !scratch.set(-ry - 1, rx) {
                return Err(6);
            }
            for dy in -2 * ry..=-1 {
                let mut y = -ry;
                while y - dy <= ry {
                    let value = numeric.table_sample(y, dy);
                    if !scratch.set(y, value) {
                        return Err(6);
                    }
                    let clamped = scratch.get(y - 1).ok_or(6u32)?.min(value.min(rx).max(-rx));
                    if !scratch.set(y, clamped) {
                        return Err(6);
                    }
                    y += 1;
                }
                y -= 1;
                for j in -rx..scratch.get(y).ok_or(6u32)? {
                    if !matrix.set(dy, j, i32::MAX) {
                        return Err(6);
                    }
                }
                while y > -ry {
                    for j in scratch.get(y).ok_or(6u32)?..scratch.get(y - 1).ok_or(6u32)? {
                        if !matrix.set(dy, j, y) {
                            return Err(6);
                        }
                    }
                    y -= 1;
                }
                let x = scratch.get(y).ok_or(6u32)?;
                if !matrix.set(dy, x, y) {
                    return Err(6);
                }
                for j in x..=rx {
                    if !matrix.set(dy, j, i32::MIN) {
                        return Err(6);
                    }
                }
            }
            Some(matrix)
        } else {
            None
        };
        self.state = Some(State {
            rx,
            ry,
            arc,
            matrix,
            numeric,
        });
        Ok(())
    }
    fn cross(&mut self, right: bool, center: u64, base: u64) -> Result<i32, u32> {
        let s = self.state.as_mut().ok_or(6u32)?;
        let dx = if right {
            (base as u32).wrapping_sub(center as u32)
        } else {
            (center as u32).wrapping_sub(base as u32)
        } as i32;
        let dy = ((center >> 32) as u32).wrapping_sub((base >> 32) as u32) as i32;
        if dx < -s.rx {
            return Ok(i32::MAX);
        }
        if dx > s.rx {
            return Ok(i32::MIN);
        }
        if let Some(matrix) = &s.matrix {
            matrix.get(dy, dx).ok_or(6)
        } else {
            Ok(s.numeric.analytic(dx, dy))
        }
    }
    fn arc(&mut self, right: bool, center: u64, line: i32) -> Result<i32, u32> {
        let s = self.state.as_ref().ok_or(6u32)?;
        let relative = (line as i64 as u64).wrapping_sub(center >> 32);
        let index = (s.ry as u64).wrapping_add(relative);
        let value = s
            .arc
            .cell(usize::try_from(index).map_err(|_| 6u32)?)
            .ok_or(6u32)?;
        let x = center as u32 as i32;
        Ok(if right {
            x.wrapping_sub(value)
        } else {
            x.wrapping_add(value)
        })
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec::Vec;
    struct Arithmetic {
        setup: usize,
        samples: Vec<(i32, i32)>,
        analytic: usize,
    }
    impl Numeric for Arithmetic {
        fn new(_: i32, _: i32) -> Self {
            Self {
                setup: 0,
                samples: Vec::new(),
                analytic: 0,
            }
        }
        fn table_setup(&mut self, _: i32) {
            self.setup += 1;
        }
        fn table_sample(&mut self, y: i32, dy: i32) -> i32 {
            self.samples.push((y, dy));
            0
        }
        fn analytic(&mut self, dx: i32, dy: i32) -> i32 {
            self.analytic += 1;
            dx.wrapping_add(dy)
        }
    }
    fn point(x: u32, y: u32) -> u64 {
        u64::from(x) | (u64::from(y) << 32)
    }
    #[test]
    fn table_order_cache_hits_and_unwritten_rows_remain_distinct() {
        let mut cache = Cache::<Arithmetic>::new();
        cache.initialize(2, 2).unwrap();
        let state = cache.state.as_ref().unwrap();
        assert_eq!(
            state.numeric.samples,
            [
                (-2, -4),
                (-2, -3),
                (-1, -3),
                (-2, -2),
                (-1, -2),
                (0, -2),
                (-2, -1),
                (-1, -1),
                (0, -1),
                (1, -1)
            ]
        );
        assert_eq!(state.numeric.setup, 1);
        assert_eq!(state.matrix.as_ref().unwrap().get(-1, -1), Some(i32::MAX));
        assert_eq!(state.matrix.as_ref().unwrap().get(-1, 0), Some(i32::MIN));
        cache.initialize(2, 2).unwrap();
        assert_eq!(cache.state.as_ref().unwrap().numeric.samples.len(), 10);
        assert_eq!(cache.cross(false, point(10, 100), point(10, 100)), Err(6));
        assert_eq!(cache.arc(false, point(10, 100), 98), Ok(9));
        assert_eq!(cache.arc(true, point(u32::MAX, 100), 99), Ok(1));
        assert_eq!(cache.arc(false, point(10, 100), 97), Err(6));
    }
    #[test]
    fn analytic_selection_clips_before_arithmetic_and_invalid_replacement_drops_cache() {
        let mut cache = Cache::<Arithmetic>::new();
        cache.initialize(65, 65).unwrap();
        assert!(cache.state.as_ref().unwrap().matrix.is_none());
        assert_eq!(cache.state.as_ref().unwrap().numeric.setup, 0);
        assert_eq!(
            cache.cross(false, point(400, 100), point(500, 110)),
            Ok(i32::MAX)
        );
        assert_eq!(
            cache.cross(true, point(400, 100), point(500, 110)),
            Ok(i32::MIN)
        );
        assert_eq!(cache.state.as_ref().unwrap().numeric.analytic, 0);
        assert_eq!(
            cache.cross(false, point(495, 100), point(500, 110)),
            Ok(-15)
        );
        assert_eq!(cache.state.as_ref().unwrap().numeric.analytic, 1);
        assert_eq!(cache.initialize(-1, 1), Err(6));
        assert!(cache.state.is_none());
    }
}
