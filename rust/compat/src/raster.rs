// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned, fully initialized raster construction from owned scans and outlines.
//! The packet is a detached snapshot for transitional native overlay consumers.
use alloc::{collections::BTreeMap, vec::Vec};

pub struct Image {
    bytes: Vec<u8>,
    // None means all bytes are known; unknown values are private placeholders.
    known: Option<Vec<u8>>,
}
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
            return Ok(Self { bytes, known: None });
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
        Ok(Self { bytes, known: None })
    }
    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }
    pub fn all_known(&self) -> bool {
        self.known.is_none()
    }
    pub fn duplicate(&self) -> Result<Self, u32> {
        Self::decode(&self.encode()?)
    }
    pub fn offset(&mut self, x: i32, y: i32) {
        self.bytes[..4].copy_from_slice(&x.to_le_bytes());
        self.bytes[4..8].copy_from_slice(&y.to_le_bytes());
    }
    pub fn wire_len(&self) -> Option<usize> {
        self.bytes
            .len()
            .checked_sub(32)?
            .checked_mul(2)?
            .checked_add(32)
    }
    pub fn encode(&self) -> Result<Vec<u8>, u32> {
        let length = self.wire_len().ok_or(5u32)?;
        let mut out = Vec::new();
        out.try_reserve_exact(length).map_err(|_| 5u32)?;
        out.extend_from_slice(&self.bytes);
        if let Some(known) = &self.known {
            out.extend_from_slice(known);
        } else {
            out.resize(length, 1);
        }
        Ok(out)
    }
    pub fn decode(wire: &[u8]) -> Result<Self, u32> {
        if wire.len() < 32 {
            return Err(3);
        }
        let fields = fields(wire);
        let flags = fields[7];
        if flags & !7 != 0 {
            return Err(3);
        }
        let planes = usize::from(flags & 2 != 0) + usize::from(flags & 4 != 0);
        let count = if planes == 0 {
            0
        } else {
            if fields[4] < 0 || fields[5] < 0 || fields[6] < fields[4] {
                return Err(6);
            }
            usize::try_from(fields[6].checked_mul(fields[5]).ok_or(6u32)?).map_err(|_| 6u32)?
        };
        let payload = count.checked_mul(planes).ok_or(5u32)?;
        if payload.checked_mul(2).and_then(|n| n.checked_add(32)) != Some(wire.len()) {
            return Err(3);
        }
        let mask = &wire[32 + payload..];
        if mask.iter().any(|&v| v > 1) {
            return Err(3);
        }
        let mut bytes = Vec::new();
        bytes.try_reserve_exact(32 + payload).map_err(|_| 5u32)?;
        bytes.extend_from_slice(&wire[..32 + payload]);
        // Canonicalize only the private placeholder, never native unknown bytes.
        for (value, &known) in bytes[32..].iter_mut().zip(mask) {
            if known == 0 {
                *value = 0;
            }
        }
        let known = if mask.contains(&0) {
            let mut known = Vec::new();
            known.try_reserve_exact(payload).map_err(|_| 5u32)?;
            known.extend_from_slice(mask);
            Some(known)
        } else {
            None
        };
        Ok(Self { bytes, known })
    }
    pub fn variance(&self, x: u32, y: u32) -> Result<Option<Self>, u32> {
        let old = fields(&self.bytes);
        let x = (x & 7) as i32;
        let y = (y & 7) as i32;
        let w = old[2].wrapping_add(x);
        let h = old[3].wrapping_add(y);
        let width = (w.wrapping_add(7) >> 3).wrapping_add(1);
        let height = (h.wrapping_add(7) >> 3).wrapping_add(1);
        let pitch = width.wrapping_add(15) & !15;
        if pitch.wrapping_mul(height) <= 0 {
            return Ok(None);
        }
        if width <= 0 || height <= 0 || pitch < width {
            return Err(6);
        }
        let count = usize::try_from(pitch.checked_mul(height).ok_or(6u32)?).map_err(|_| 6u32)?;
        let border = old[7] & 1 == 0;
        let payload = count.checked_mul(if border { 2 } else { 1 }).ok_or(5u32)?;
        let length = payload.checked_add(32).ok_or(5u32)?;
        let mut bytes = Vec::new();
        bytes.try_reserve_exact(length).map_err(|_| 5u32)?;
        bytes.resize(length, 0);
        for (i, v) in [
            old[0].wrapping_sub(x),
            old[1].wrapping_sub(y),
            w,
            h,
            width,
            height,
            pitch,
            if border { 6 } else { 3 },
        ]
        .into_iter()
        .enumerate()
        {
            bytes[i * 4..i * 4 + 4].copy_from_slice(&v.to_le_bytes());
        }
        let mut known = Vec::new();
        known.try_reserve_exact(payload).map_err(|_| 5u32)?;
        known.resize(payload, 1);
        let full = pitch == old[6] && width == old[4] && height >= old[5];
        if old[4] < 0 || old[5] < 0 || old[6] < old[4] {
            return Err(6);
        }
        let old_count =
            usize::try_from(old[6].checked_mul(old[5]).ok_or(6u32)?).map_err(|_| 6u32)?;
        for plane in 0..2usize {
            let present = plane == 0 || border;
            let source = old[7] & (2 << plane) != 0;
            if full && present != source {
                return Ok(None);
            }
            if !present {
                continue;
            }
            let source_plane = if plane == 1 && old[7] & 2 == 0 {
                0
            } else {
                plane
            };
            let ranges = if full { 1 } else { old[5] as usize };
            for row in 0..ranges {
                let n = if full { old_count } else { old[4] as usize };
                if n == 0 {
                    continue;
                }
                if !source {
                    return Err(6);
                }
                let start = source_plane
                    .checked_mul(old_count)
                    .and_then(|v| v.checked_add(row * old[6] as usize))
                    .ok_or(6u32)?;
                let dest = plane * count + row * pitch as usize;
                let end = start.checked_add(n).ok_or(6u32)?;
                let dest_end = dest.checked_add(n).ok_or(6u32)?;
                let src = self.bytes.get(32 + start..32 + end).ok_or(6u32)?;
                bytes
                    .get_mut(32 + dest..32 + dest_end)
                    .ok_or(6u32)?
                    .copy_from_slice(src);
                if let Some(mask) = &self.known {
                    known
                        .get_mut(dest..dest_end)
                        .ok_or(6u32)?
                        .copy_from_slice(mask.get(start..end).ok_or(6u32)?);
                }
            }
        }
        // Cleanup completes both copies before filtering the body, then border.
        for plane in 0..if border { 2 } else { 1 } {
            let mut columns = Vec::new();
            columns
                .try_reserve_exact(width as usize)
                .map_err(|_| 5u32)?;
            columns.resize(width as usize, 0u16);
            for row in 0..height as usize {
                let at = plane * count + row * pitch as usize;
                let end = at + width as usize;
                if known[at..end].contains(&0) {
                    return Err(6);
                }
                if !crate::bilinear::row(
                    &mut bytes[32 + at..32 + end],
                    &mut columns,
                    x as u32,
                    y as u32,
                ) {
                    return Err(6);
                }
            }
        }
        let known = if known.contains(&0) {
            Some(known)
        } else {
            None
        };
        Ok(Some(Self { bytes, known }))
    }
}
fn fields(bytes: &[u8]) -> [i32; 8] {
    core::array::from_fn(|i| i32::from_le_bytes(bytes[4 * i..4 * i + 4].try_into().unwrap()))
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
    pub fn get_mut(&mut self, id: u64) -> Result<&mut Image, u32> {
        self.values.get_mut(&id).ok_or(1)
    }
    pub fn remove(&mut self, id: u64) -> Result<(), u32> {
        self.values.remove(&id).map(|_| ()).ok_or(1)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn imported() -> Image {
        let mut wire = alloc::vec![0;96];
        for (i, v) in [0i32, 0, 8, 8, 2, 2, 16, 3].into_iter().enumerate() {
            wire[i * 4..i * 4 + 4].copy_from_slice(&v.to_le_bytes());
        }
        wire[32..64].fill(0xa5);
        for (at, value) in [(0, 8), (1, 16), (16, 24), (17, 32)] {
            wire[32 + at] = value;
            wire[64 + at] = 1;
        }
        Image::decode(&wire).unwrap()
    }
    #[test]
    fn variance_carries_unknown_padding_but_never_filters_unknown_pixels() {
        let image = imported();
        assert!(!image.all_known());
        let out = image.variance(0, 0).unwrap().unwrap();
        assert_eq!(out.encode().unwrap(), image.encode().unwrap());
        let out = image.variance(1, 0).unwrap().unwrap();
        assert!(out.all_known());
        assert_eq!(&out.bytes()[32..35], &[7, 15, 2]);
        assert_eq!(&out.bytes()[48..51], &[21, 31, 4]);
        assert!(out.bytes()[35..48].iter().all(|&v| v == 0));
        let mut bad = image.encode().unwrap();
        bad[64] = 0;
        assert!(Image::decode(&bad).unwrap().variance(0, 0).is_err());
        bad[64] = 2;
        assert!(Image::decode(&bad).is_err());
    }
    #[test]
    fn copying_offsets_does_not_mutate_source_pixels_metadata_or_validity() {
        let source = imported();
        let original = source.encode().unwrap();
        let mut copy = source.duplicate().unwrap();
        copy.offset(i32::MIN, i32::MAX);
        let shifted = copy.variance(7, 7).unwrap().unwrap();
        assert_eq!(&fields(shifted.bytes())[..2], &[i32::MAX - 6, i32::MAX - 7]);
        assert_eq!(source.encode().unwrap(), original);
        assert_eq!(&copy.encode().unwrap()[8..], &original[8..]);
    }
    #[test]
    fn variance_empty_input_has_zero_planes_and_full_copy_rejects_missing_planes() {
        let image = Image::decode(&[0; 32]).unwrap();
        let out = image.variance(u32::MAX, u32::MAX).unwrap().unwrap();
        assert_eq!(fields(out.bytes()), [-7, -7, 7, 7, 2, 2, 16, 6]);
        assert!(out.bytes()[32..].iter().all(|&v| v == 0));
        let mut header = [0u8; 32];
        for (i, v) in [0i32, 0, 8, 8, 2, 2, 16, 1].into_iter().enumerate() {
            header[i * 4..i * 4 + 4].copy_from_slice(&v.to_le_bytes());
        }
        assert!(Image::decode(&header)
            .unwrap()
            .variance(0, 0)
            .unwrap()
            .is_none());
    }
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
