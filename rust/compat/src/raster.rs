// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned, fully initialized raster construction from owned scans and outlines.
//! The packet is a detached snapshot for transitional native overlay consumers.
use alloc::{collections::BTreeMap, vec::Vec};

pub struct Image(Vec<u8>);
impl Image {
    pub fn render(
        scan: &crate::scan_convert::Scan,
        region: &crate::wide_region::Region,
        offset: [i32; 2],
        sub: [i32; 2],
    ) -> Result<Self, u32> {
        Self::from_parts(
            scan.size(),
            scan.spans(),
            region.border(),
            region.spans(),
            offset,
            sub,
        )
    }
    fn from_parts(
        size: [i32; 2],
        spans: &[[u64; 2]],
        border: i32,
        wide: &[[u64; 2]],
        offset: [i32; 2],
        sub: [i32; 2],
    ) -> Result<Self, u32> {
        let mut bytes = Vec::new();
        bytes.try_reserve_exact(32).map_err(|_| 5u32)?;
        bytes.resize(32, 0);
        // CleanUp precedes this early return, including its false wide-empty flag.
        if size[0] == 0 || size[1] == 0 {
            return Ok(Self(bytes));
        }
        let mut sub = [sub[0] & 7, sub[1] & 7];
        let mut size = [size[0].wrapping_add(sub[0]), size[1].wrapping_add(sub[1])];
        if !wide.is_empty() {
            let border = border.wrapping_add(7) & !7;
            for axis in 0..2 {
                size[axis] = size[axis].wrapping_add(border.wrapping_mul(2));
                sub[axis] = sub[axis].wrapping_add(border);
            }
        }
        let width = (size[0].wrapping_add(7) >> 3).wrapping_add(1);
        let height = (size[1].wrapping_add(7) >> 3).wrapping_add(1);
        let pitch = width.wrapping_add(15) & !15;
        // Native allocation/address overflow is unresolved, never clipped or
        // allowed to index outside the owned plane. Preserve wrapping metadata.
        if width <= 0 || height <= 0 || pitch < width {
            return Err(6);
        }
        let count = usize::try_from(pitch.checked_mul(height).ok_or(6u32)?).map_err(|_| 6u32)?;
        let planes = if wide.is_empty() { 1 } else { 2 };
        let length = count
            .checked_mul(planes)
            .and_then(|n| n.checked_add(32))
            .ok_or(5u32)?;
        bytes
            .try_reserve_exact(length - bytes.len())
            .map_err(|_| 5u32)?;
        bytes.resize(length, 0);
        let flags = if wide.is_empty() { 3 } else { 6 };
        for (slot, value) in [
            offset[0].wrapping_sub(sub[0]),
            offset[1].wrapping_sub(sub[1]),
            size[0],
            size[1],
            width,
            height,
            pitch,
            flags,
        ]
        .into_iter()
        .enumerate()
        {
            bytes[slot * 4..slot * 4 + 4].copy_from_slice(&value.to_le_bytes());
        }
        // Cleanup accumulates border before body, with wrapping byte additions.
        // Every byte, including padding, starts initialized to zero here.
        for (index, spans) in [(1usize, wide), (0, spans)] {
            for span in spans {
                let y = ((span[0] >> 32) as i32)
                    .wrapping_sub(0x40000000)
                    .wrapping_add(sub[1]);
                let x1 = (span[0] as i32)
                    .wrapping_sub(0x40000000)
                    .wrapping_add(sub[0]);
                let x2 = (span[1] as i32)
                    .wrapping_sub(0x40000000)
                    .wrapping_add(sub[0]);
                let plane = &mut bytes[32 + index * count..32 + (index + 1) * count];
                if !crate::span_coverage::accumulate(plane, pitch, y, x1, x2) {
                    return Err(6);
                }
            }
        }
        Ok(Self(bytes))
    }
    pub fn bytes(&self) -> &[u8] {
        &self.0
    }
}

pub struct Store {
    next: u64,
    values: BTreeMap<u64, Image>,
}
impl Default for Store {
    fn default() -> Self {
        Self::new()
    }
}
impl Store {
    pub const fn new() -> Self {
        Self {
            next: 1,
            values: BTreeMap::new(),
        }
    }
    pub fn insert(&mut self, image: Image) -> Result<u64, u32> {
        let id = self.next;
        self.next = self.next.checked_add(1).ok_or(5u32)?;
        self.values.insert(id, image);
        Ok(id)
    }
    pub fn get(&self, id: u64) -> Result<&Image, u32> {
        self.values.get(&id).ok_or(1)
    }
    pub fn remove(&mut self, id: u64) -> Result<(), u32> {
        self.values.remove(&id).map(|_| ()).ok_or(1)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn point(x: i32, y: i32) -> u64 {
        (u64::from(y.wrapping_add(0x40000000) as u32) << 32)
            | u64::from(x.wrapping_add(0x40000000) as u32)
    }
    fn header(image: &Image) -> Vec<i32> {
        image.bytes()[..32]
            .chunks_exact(4)
            .map(|b| i32::from_le_bytes(b.try_into().unwrap()))
            .collect()
    }
    #[test]
    fn empty_dimension_clears_every_field_before_considering_spans_or_offsets() {
        for size in [[0, 1], [1, 0]] {
            let image = Image::from_parts(
                size,
                &[[0, u64::MAX]],
                i32::MAX,
                &[[0, u64::MAX]],
                [i32::MAX, i32::MIN],
                [-1, -1],
            )
            .unwrap();
            assert_eq!(image.bytes(), &[0; 32]);
        }
    }
    #[test]
    fn padding_is_initialized_and_overlap_wraps_without_row_clipping() {
        let span = [point(3, 0), point(26, 0)];
        let image = Image::from_parts([32, 8], &[span; 33], 0, &[], [0; 2], [0; 2]).unwrap();
        assert_eq!(header(&image), [0, 0, 32, 8, 5, 2, 16, 3]);
        assert_eq!(&image.bytes()[32..36], &[165, 8, 8, 66]);
        assert!(image.bytes()[36..].iter().all(|&n| n == 0));
        let image = Image::from_parts(
            [32, 8],
            &[[point(-3, 8), point(10, 8)]],
            0,
            &[],
            [0; 2],
            [0; 2],
        )
        .unwrap();
        assert_eq!(&image.bytes()[47..50], &[3, 8, 2]);
        assert!(Image::from_parts(
            [32, 8],
            &[[point(-3, 0), point(10, 0)]],
            0,
            &[],
            [0; 2],
            [0; 2]
        )
        .is_err());
    }
    #[test]
    fn border_rounding_signed_subpixels_and_detached_owner_lifetime() {
        let spans = [[point(0, 0), point(8, 0)]];
        let image =
            Image::from_parts([8, 8], &spans, 9, &spans, [i32::MIN, i32::MAX], [-1, -9]).unwrap();
        assert_eq!(
            header(&image),
            [i32::MAX - 22, i32::MAX - 23, 47, 47, 7, 7, 16, 6]
        );
        assert_eq!(image.bytes().len(), 32 + 112 * 2);
        assert_eq!(&image.bytes()[32..144], &image.bytes()[144..]);
        let mut store = Store::new();
        let id = store.insert(image).unwrap();
        let detached = store.get(id).unwrap().bytes().to_vec();
        store.remove(id).unwrap();
        assert!(store.get(id).is_err());
        assert_eq!(detached.len(), 256);
        assert!(store.remove(id).is_err());
    }
}
