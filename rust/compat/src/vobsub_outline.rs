// SPDX-License-Identifier: GPL-3.0-or-later
//! Exact bitmap outline-mask and integer walking transitions.
use core::mem::MaybeUninit;

pub fn mask(output: &mut [MaybeUninit<u8>], mut alpha: impl FnMut(usize) -> u8) {
    for (i, slot) in output.iter_mut().enumerate() {
        slot.write(u8::from(alpha(i) != 0));
    }
}

fn valid(grid: &[u8], width: i32, height: i32) -> bool {
    width > 0 && height > 0 && width.checked_mul(height).map(|n| n as usize) == Some(grid.len())
}

pub fn find(grid: &[u8], width: i32, height: i32) -> Option<[i32; 2]> {
    if !valid(grid, width, height) {
        return None;
    }
    for (y, row) in grid.chunks_exact(width as usize).enumerate() {
        if let Some(x) = row.windows(2).position(|p| p[0] == 0 && p[1] == 1) {
            return Some([x as i32, y as i32]);
        }
    }
    Some([width - 1, height])
}

#[repr(C)]
#[derive(Debug, PartialEq, Eq)]
pub struct Step {
    pub x: i32,
    pub y: i32,
    pub direction: i32,
    pub point_x: i32,
    pub point_y: i32,
}

/// A declined step leaves the mask unchanged for the original assertion path.
pub fn walk(
    grid: &mut [u8],
    width: i32,
    height: i32,
    mut x: i32,
    mut y: i32,
    direction: i32,
) -> Option<Step> {
    if !valid(grid, width, height)
        || x < 0
        || x >= width
        || y < 0
        || y >= height
        || !(0..4).contains(&direction)
    {
        return None;
    }
    let at = |x: i32, y: i32| {
        if x < 0 || x >= width || y < 0 || y >= height {
            0
        } else {
            grid[(y * width + x) as usize]
        }
    };
    let (point_x, point_y, fl, fr, br) = match direction {
        0 => (x + 1, y, at(x, y - 1), at(x + 1, y - 1), at(x + 1, y)),
        1 => (x + 1, y + 1, at(x + 1, y), at(x + 1, y + 1), at(x, y + 1)),
        2 => (x, y + 1, at(x, y + 1), at(x - 1, y + 1), at(x - 1, y)),
        _ => (x, y, at(x - 1, y), at(x - 1, y - 1), at(x, y - 1)),
    };
    let index = (y * width + x) as usize;
    let next = if fl == 1 {
        (direction + 3) & 3
    } else if fr != 1 && br == 1 {
        (direction + 1) & 3
    } else if grid[index] & 16 != 0 {
        return None;
    } else {
        direction
    };
    match next {
        0 => {
            if direction == 3 {
                x -= 1;
                y -= 1;
            }
            if direction == 0 {
                y -= 1;
            }
        }
        1 => {
            if direction == 0 {
                x += 1;
                y -= 1;
            }
            if direction == 1 {
                x += 1;
            }
        }
        2 => {
            if direction == 1 {
                x += 1;
                y += 1;
            }
            if direction == 2 {
                y += 1;
            }
        }
        _ => {
            if direction == 2 {
                x -= 1;
                y += 1;
            }
            if direction == 3 {
                x -= 1;
            }
        }
    }
    grid[index] = grid[index].wrapping_shl(1) | 2;
    Some(Step {
        x,
        y,
        direction: next,
        point_x,
        point_y,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn marked_neighbors_are_not_treated_as_opaque_pixels() {
        let mut grid = [0, 0, 0, 0, 0, 1, 0, 0, 0];
        assert_eq!(find(&grid, 3, 3), Some([1, 1]));
        assert_eq!(
            walk(&mut grid, 3, 3, 1, 1, 0),
            Some(Step {
                x: 2,
                y: 0,
                direction: 1,
                point_x: 2,
                point_y: 1
            })
        );
        assert_eq!(grid[4], 2);
        grid[4] = 16;
        grid[5] = 3;
        let before = grid;
        assert_eq!(walk(&mut grid, 3, 3, 1, 1, 0), None);
        assert_eq!(grid, before);
    }
}
