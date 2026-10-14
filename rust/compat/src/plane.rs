// SPDX-License-Identifier: GPL-2.0-or-later
//! Aligned output-only storage. Native memset initializes the logical plane
//! before it is consumed; MaybeUninit also preserves untouched padding.
use alloc::vec::Vec;
use core::mem::MaybeUninit;

/// Validate full-pitch copies before selecting a Rust destination allocation.
/// The legacy copy width includes source padding, even after a horizontal inset.
pub fn legacy_copy_fits(
    source_pitch: i32,
    source_height: i32,
    pitch: i32,
    height: i32,
    inset_x: i32,
    inset_y: i32,
) -> bool {
    if source_pitch <= 0
        || source_height < 0
        || pitch <= 0
        || height <= 0
        || inset_x < 0
        || inset_y < 0
    {
        return false;
    }
    let bytes = i64::from(pitch) * i64::from(height);
    let start = i64::from(inset_y) * i64::from(pitch) + i64::from(inset_x);
    if bytes > i64::from(i32::MAX) || start > bytes {
        return false;
    }
    if source_height == 0 {
        return true;
    }
    let source_bytes = i64::from(source_pitch) * i64::from(source_height);
    let end = start + i64::from(source_height - 1) * i64::from(pitch) + i64::from(source_pitch);
    let final_cursor = start + i64::from(source_height) * i64::from(pitch);
    source_bytes <= i64::from(i32::MAX) && end <= bytes && final_cursor <= bytes
}

#[repr(C, align(16))]
#[derive(Clone, Copy)]
pub struct Block(pub [MaybeUninit<u8>; 16]);
pub struct Plane {
    blocks: Vec<Block>,
}
impl Plane {
    pub fn new(count: usize) -> Option<Self> {
        if count == 0 || count > i32::MAX as usize {
            return None;
        }
        let length = count.checked_add(15)? / 16;
        let mut blocks = Vec::new();
        blocks.try_reserve_exact(length).ok()?;
        blocks.resize(length, Block([MaybeUninit::uninit(); 16]));
        Some(Self { blocks })
    }
    pub fn blocks_mut(&mut self) -> &mut [Block] {
        &mut self.blocks
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn full_pitch_copy_requires_room_for_the_final_inset_and_padding() {
        assert!(!legacy_copy_fits(16, 1, 16, 1, 1, 0));
        assert!(legacy_copy_fits(16, 1, 16, 2, 1, 0));
        assert!(legacy_copy_fits(16, 3, 32, 5, 2, 1));
        assert!(!legacy_copy_fits(16, 3, 32, 3, 2, 1));
        assert!(!legacy_copy_fits(16, 1, i32::MAX, 2, 0, 0));
        assert!(legacy_copy_fits(16, 0, 16, 1, 16, 0));
    }
    #[test]
    fn rounded_blocks_preserve_sse_alignment_and_checked_size() {
        for size in [1, 15, 16, 17, 1025] {
            let mut plane = Plane::new(size).unwrap();
            assert_eq!(plane.blocks_mut().len(), (size + 15) / 16);
            assert_eq!(plane.blocks_mut().as_ptr() as usize % 16, 0);
        }
        assert!(Plane::new(0).is_none());
        assert!(Plane::new(i32::MAX as usize + 1).is_none());
    }
}
