// SPDX-License-Identifier: GPL-2.0-or-later
//! Aligned output-only storage. Native memset initializes the logical plane
//! before it is consumed; MaybeUninit also preserves untouched padding.
use alloc::vec::Vec;
use core::mem::MaybeUninit;

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
