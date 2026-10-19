// SPDX-License-Identifier: GPL-2.0-or-later
//! CWord path traversal. Arithmetic is supplied by the target's numeric backend:
//! x87 retained intermediates and SSE2/CRT trigonometry are observably different.
use crate::path_align::Point;

pub struct Parameters {
    /// scale X/Y, angle Z/X/Y, shift X/Y, target scale X/Y; raw native doubles.
    pub values: [u64; 9],
    pub origin: Point,
    pub round: bool,
}
pub trait Backend {
    fn point(&mut self, point: Point, first: bool) -> Point;
    fn needs_rounding(&self) -> bool;
}
pub fn transform(points: &mut [Point], round: bool, backend: &mut impl Backend) {
    for (index, point) in points.iter_mut().enumerate() {
        *point = backend.point(*point, index == 0);
        // Native comparisons occur after conversion, on each point, and short
        // circuit. Moving them outside the loop changes exception side effects.
        if round && backend.needs_rounding() {
            point.x = point.x.wrapping_add(32) & !63;
            point.y = point.y.wrapping_add(32) & !63;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec::Vec;
    #[test]
    fn traversal_preserves_first_point_and_comparison_order() {
        struct Numeric(Vec<(Point, bool)>, core::cell::Cell<usize>);
        impl Backend for Numeric {
            fn point(&mut self, p: Point, first: bool) -> Point {
                self.0.push((p, first));
                p
            }
            fn needs_rounding(&self) -> bool {
                self.1.set(self.1.get() + 1);
                true
            }
        }
        let mut n = Numeric(Vec::new(), core::cell::Cell::new(0));
        transform(&mut [], true, &mut n);
        assert!(n.0.is_empty());
        assert_eq!(n.1.get(), 0);
        let mut points = [
            Point {
                x: i32::MAX,
                y: -33,
            },
            Point { x: 32, y: -32 },
        ];
        let original = points;
        transform(&mut points, true, &mut n);
        assert_eq!(n.0, [(original[0], true), (original[1], false)]);
        assert_eq!(n.1.get(), 2);
        assert_eq!(
            points,
            [
                Point {
                    x: i32::MIN,
                    y: -64
                },
                Point { x: 64, y: 0 }
            ]
        );
        transform(&mut points, false, &mut n);
        assert_eq!(n.1.get(), 2);
    }
}
