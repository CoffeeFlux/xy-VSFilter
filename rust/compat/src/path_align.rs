// SPDX-License-Identifier: GPL-2.0-or-later
//! Exact AlignLeftTop geometry over initialized native point buffers.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Point {
    pub x: i32,
    pub y: i32,
}

pub fn align(points: &mut [Point]) -> Option<[i32; 4]> {
    let first = *points.first()?;
    let (mut minx, mut miny, mut maxx, mut maxy) = (first.x, first.y, first.x, first.y);
    for point in points.iter() {
        minx = minx.min(point.x);
        miny = miny.min(point.y);
        maxx = maxx.max(point.x);
        maxy = maxy.max(point.y);
    }
    minx = (minx >> 3) & !7;
    miny = (miny >> 3) & !7;
    maxx = maxx.checked_add(7)? >> 3;
    maxy = maxy.checked_add(7)? >> 3;
    let dx = minx.checked_mul(8)?;
    let dy = miny.checked_mul(8)?;
    let width = maxx.checked_add(1)?.checked_sub(minx)?;
    let height = maxy.checked_add(1)?.checked_sub(miny)?;
    // Validate the entire operation before changing any point. Caller outputs
    // are assigned by C++ after this borrow ends, in their original order.
    for point in points.iter() {
        point.x.checked_sub(dx)?;
        point.y.checked_sub(dy)?;
    }
    for point in points {
        point.x -= dx;
        point.y -= dy;
    }
    Some([minx, miny, width, height])
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn negative_coordinates_round_down_and_size_keeps_the_extra_unit() {
        let mut points = [Point { x: -1, y: -65 }, Point { x: 64, y: 7 }];
        assert_eq!(align(&mut points), Some([-8, -16, 17, 18]));
        assert_eq!(points, [Point { x: 63, y: 63 }, Point { x: 128, y: 135 }]);
    }
    #[test]
    fn late_point_overflow_declines_before_any_mutation() {
        let mut points = [Point { x: i32::MIN, y: 0 }, Point { x: 0, y: 0 }];
        let before = points;
        assert_eq!(align(&mut points), None);
        assert_eq!(points, before);
        let mut points = [Point { x: i32::MAX, y: 0 }];
        let before = points;
        assert_eq!(align(&mut points), None);
        assert_eq!(points, before);
        assert_eq!(align(&mut []), None);
    }
}
