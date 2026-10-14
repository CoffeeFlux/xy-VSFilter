// SPDX-License-Identifier: GPL-2.0-or-later
//! Accumulate one native eight-subpixel span into an initialized mask plane.
pub fn accumulate(plane: &mut [u8], pitch: i32, y: i32, x1: i32, x2: i32) -> bool {
    if x2 <= x1 {
        return true;
    }
    let prepare = || {
        let first = x1 >> 3;
        let last = x2.checked_sub(1)? >> 3;
        let base = pitch.checked_mul(y >> 3)?;
        let start = usize::try_from(base.checked_add(first)?).ok()?;
        let end = usize::try_from(base.checked_add(last)?).ok()?;
        if start > end || end >= plane.len() {
            return None;
        }
        if first == last {
            Some((start, end, x2.checked_sub(x1)? as u8, 0))
        } else {
            // Keep unsupported native signed-shift domains on the native path.
            let next = first.checked_add(1)?;
            if next < 0 || last < 0 {
                return None;
            }
            let left = next.checked_mul(8)?.checked_sub(x1)?;
            let right = x2.checked_sub(last.checked_mul(8)?)?;
            Some((start, end, left as u8, right as u8))
        }
    };
    let Some((start, end, left, right)) = prepare() else {
        return false;
    };
    plane[start] = plane[start].wrapping_add(left);
    if start != end {
        for value in &mut plane[start + 1..end] {
            *value = value.wrapping_add(8);
        }
        plane[end] = plane[end].wrapping_add(right);
    }
    true
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn partial_endpoints_and_repeated_coverage_wrap_bytes() {
        let mut plane = [250; 8];
        assert!(accumulate(&mut plane, 4, 0, 3, 26));
        assert_eq!(plane, [255, 2, 2, 252, 250, 250, 250, 250]);
        assert!(accumulate(&mut plane, 4, 0, 3, 5));
        assert_eq!(plane[0], 1);
    }
    #[test]
    fn addressing_is_checked_as_a_linear_plane_not_clipped_to_a_row() {
        let mut plane = [0; 16];
        assert!(accumulate(&mut plane, 4, 8, -3, 10));
        assert_eq!(&plane[3..6], &[3, 8, 2]);
        assert!(accumulate(&mut plane, 4, -8, 40, 44));
        assert_eq!(plane[1], 4);
        let before = plane;
        assert!(!accumulate(&mut plane, 4, 0, -3, 10));
        assert_eq!(plane, before);
        assert!(accumulate(&mut [], i32::MAX, i32::MAX, 4, 4));
    }
}
