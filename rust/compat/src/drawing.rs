// SPDX-License-Identifier: GPL-2.0-or-later
// Derived from CPolygon::ParseStr, Copyright (C) 2003-2006 Gabest.
//! Drawing command recovery and path storage. Tokenization is a separate stage;
//! each requested point is still parsed with the original native GetPOINT.
use alloc::vec::Vec;

pub const MOVE: u8 = 6;
pub const MOVE_NO_CLOSE: u8 = 0xfe;
pub const LINE: u8 = 2;
pub const BEZIER: u8 = 4;
pub const SPLINE: u8 = 0xfc;
pub const PATCH: u8 = 0xfa;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Point {
    pub x: i32,
    pub y: i32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Vertex {
    pub kind: u8,
    pub point: Point,
}

#[derive(Default)]
pub struct Drawing {
    vertices: Vec<Vertex>,
    first_move: Option<usize>,
    last_spline: Option<usize>,
    pending: Option<u8>,
    spline_remaining: usize,
}

impl Drawing {
    pub fn vertices(&self) -> &[Vertex] {
        &self.vertices
    }

    /// True asks the native caller to attempt GetPOINT, even on an empty token.
    pub fn command(&mut self, command: u32) -> bool {
        self.pending = match command {
            109 => {
                // Even an empty m command records a move at the current index.
                self.first_move.get_or_insert(self.vertices.len());
                Some(MOVE)
            }
            110 => Some(MOVE_NO_CLOSE),
            108 if !self.vertices.is_empty() => Some(LINE),
            98 if !self.vertices.is_empty() => Some(BEZIER),
            115 if !self.vertices.is_empty() => {
                self.last_spline = Some(self.vertices.len());
                self.spline_remaining = 3;
                Some(SPLINE)
            }
            112 if self.vertices.len() >= 3 => Some(PATCH),
            99 => {
                if let Some(start) = self.last_spline.filter(|&start| start > 0) {
                    // Copy by value before growth: closing a spline reuses its
                    // preceding point and its first two control points.
                    for index in [start - 1, start, start + 1] {
                        let point = self.vertices[index].point;
                        self.vertices.push(Vertex { kind: PATCH, point });
                    }
                    self.last_spline = None;
                }
                None
            }
            _ => None,
        };
        self.pending.is_some()
    }

    /// Submit the exact success/failure of one native GetPOINT. True requests
    /// another attempt on the remaining token; false finishes this command.
    pub fn point(&mut self, point: Option<Point>) -> bool {
        let Some(kind) = self.pending else {
            return false;
        };
        if let Some(point) = point {
            self.vertices.push(Vertex { kind, point });
        }
        if kind == SPLINE {
            self.spline_remaining -= 1;
            if point.is_none() {
                self.vertices.truncate(self.last_spline.unwrap_or(0));
                self.last_spline = None;
            }
            if point.is_none() || self.spline_remaining == 0 {
                // s has no break: after success OR rollback it enters p. With
                // >=3 previous points, failed s asks GetPOINT once more.
                self.pending = (self.vertices.len() >= 3).then_some(PATCH);
            }
        } else if point.is_none() {
            // The original b counter grows alongside the array. Its apparent
            // modulo-three truncation therefore removes zero points.
            self.pending = None;
        }
        self.pending.is_some()
    }

    pub fn finish(&mut self) -> bool {
        let valid = self.first_move == Some(0);
        if !valid {
            self.vertices.clear();
        }
        valid
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn point(x: i32) -> Option<Point> {
        Some(Point { x, y: -x })
    }

    #[test]
    fn empty_move_and_incomplete_bezier_are_retained() {
        let mut path = Drawing::default();
        assert!(path.command(u32::from(b'm')));
        assert!(!path.point(None));
        assert!(path.finish());
        assert!(path.command(u32::from(b'n')));
        assert!(path.point(point(1)));
        assert!(!path.point(None));
        assert!(path.command(u32::from(b'b')));
        assert!(path.point(point(2)));
        assert!(!path.point(None));
        assert!(path.finish());
        assert_eq!(path.vertices().len(), 2);
        assert_eq!(path.vertices()[1].kind, BEZIER);
    }

    #[test]
    fn failed_spline_rolls_back_then_requests_a_patch_point() {
        let mut path = Drawing::default();
        path.command(u32::from(b'm'));
        for x in 0..3 {
            path.point(point(x));
        }
        path.point(None);
        assert!(path.command(u32::from(b's')));
        assert!(path.point(point(9)));
        assert!(path.point(None));
        assert_eq!(path.vertices().len(), 3);
        assert!(!path.point(None));
        assert!(!path.command(u32::from(b'c')));
        assert_eq!(path.vertices().len(), 3);
    }

    #[test]
    fn closing_spline_copies_original_points_once() {
        let mut path = Drawing::default();
        path.command(u32::from(b'm'));
        path.point(point(5));
        path.point(None);
        path.command(u32::from(b's'));
        for x in 6..9 {
            assert!(path.point(point(x)));
        }
        assert!(!path.point(None));
        assert!(!path.command(u32::from(b'c')));
        let points: Vec<_> = path.vertices().iter().map(|v| v.point.x).collect();
        assert_eq!(points, [5, 6, 7, 8, 5, 6, 7]);
        path.command(u32::from(b'c'));
        assert_eq!(path.vertices().len(), 7);
        assert!(path.finish());
    }
}
