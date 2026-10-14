// SPDX-License-Identifier: GPL-2.0-or-later
//! Integer block geometry used by CSubtitle::MakeLines, in native operation order.
pub fn grow(space: [i32; 2], line: [i32; 4]) -> Option<[i32; 2]> {
    let [width, border, ascent, descent] = line;
    Some([
        width.checked_add(border)?.max(space[0]),
        space[1].checked_add(ascent.checked_add(descent)?)?,
    ])
}

pub fn place(
    size: [i32; 2],
    margin: [i32; 4],
    space: [i32; 2],
    alignment: i32,
) -> Option<[i32; 4]> {
    let [left, top, right, bottom] = margin;
    let x = match alignment % 3 {
        1 => left,
        2 => {
            left.checked_add(size[0].checked_sub(right)?)?
                .checked_sub(space[0])?
                .checked_add(1)?
                / 2
        }
        _ => size[0].checked_sub(right)?.checked_sub(space[0])?,
    };
    let y = if alignment <= 3 {
        size[1].checked_sub(bottom)?.checked_sub(space[1])?
    } else if alignment <= 6 {
        top.checked_add(size[1].checked_sub(bottom)?)?
            .checked_sub(space[1])?
            .checked_add(1)?
            / 2
    } else {
        top
    };
    Some([x, y, x.checked_add(space[0])?, y.checked_add(space[1])?])
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn width_includes_only_horizontal_border_and_height_can_decrease() {
        assert_eq!(grow([0, 0], [-9, 3, 7, 2]), Some([0, 9]));
        assert_eq!(grow([5, 9], [6, 2, -7, -3]), Some([8, -1]));
    }
    #[test]
    fn signed_alignment_and_center_division_follow_native_rules() {
        assert_eq!(place([10, 10], [0; 4], [13, 13], 5), Some([-1, -1, 12, 12]));
        assert_eq!(
            place([10, 10], [1, 2, 3, 4], [5, 6], -1),
            Some([2, 0, 7, 6])
        );
        // Left/top alignment does not evaluate unused right/bottom arithmetic.
        assert_eq!(place([i32::MIN; 2], [0, 0, 1, 1], [0, 0], 7), Some([0; 4]));
    }
    #[test]
    fn intermediate_and_final_rectangle_overflow_decline() {
        assert_eq!(grow([0, 0], [i32::MAX, 1, 0, 0]), None);
        assert_eq!(grow([0, -1], [0, 0, i32::MAX, 1]), None);
        assert_eq!(place([1, 0], [i32::MAX, 0, 0, 0], [i32::MAX, 0], 2), None);
        assert_eq!(place([0, 0], [i32::MAX, 0, 0, 0], [1, 0], 7), None);
    }
}
