// SPDX-License-Identifier: GPL-2.0-or-later
//! Canonical XyBitmap initialization and complete Draw/AdditionDraw traversal.
//! Packed padding is unknown; planar initialization covers complete planes.
use crate::{be_blur::zeroed, composite_mask::Composite, raster::Image};
use alloc::vec::Vec;

pub struct Bitmap {
    fields: [i32; 8], // type, x, y, width, height, pitch, alignment residue, present
    values: Vec<u8>,
    known: Vec<u8>,
}

pub struct Draw {
    origin: [i32; 2],
    extent: [i32; 2],
    source_pitch: usize,
    destination: usize,
    addition: bool,
}
impl Bitmap {
    pub fn new(rect: [i32; 4], layout: i32) -> Result<Self, u32> {
        let w = rect[2].wrapping_sub(rect[0]);
        let h = rect[3].wrapping_sub(rect[1]);
        if !(0..=1).contains(&layout) || w < 0 || h < 0 {
            return Err(6);
        }
        let aligned = w.checked_add(15).ok_or(6u32)? & !15;
        let pitch = if layout == 0 {
            aligned.checked_mul(4).ok_or(6u32)?
        } else {
            aligned
        };
        let count = aligned
            .checked_mul(4)
            .and_then(|v| v.checked_mul(h))
            .ok_or(6u32)? as usize;
        let shift = if layout == 0 {
            rect[0].wrapping_mul(4)
        } else {
            rect[0]
        } & 15;
        let mut result = Self {
            fields: [layout, rect[0], rect[1], w, h, pitch, shift, 1],
            values: zeroed(count)?,
            known: zeroed(count)?,
        };
        if layout == 1 {
            result.values[..pitch as usize * h as usize].fill(255);
            result.known.fill(1);
        } else {
            for row in 0..h as usize {
                for x in 0..w as usize {
                    let at = row * pitch as usize + x * 4;
                    result.values[at + 3] = 255;
                    result.known[at..at + 4].fill(1);
                }
            }
        }
        Ok(result)
    }
    pub fn wire_len(&self) -> Result<usize, u32> {
        self.values
            .len()
            .checked_mul(2)
            .and_then(|v| v.checked_add(32))
            .ok_or(5)
    }
    pub fn encode(&self) -> Result<Vec<u8>, u32> {
        let mut out = zeroed(self.wire_len()?)?;
        for (i, v) in self.fields.iter().enumerate() {
            out[i * 4..i * 4 + 4].copy_from_slice(&v.to_le_bytes());
        }
        let end = 32 + self.values.len();
        out[32..end].copy_from_slice(&self.values);
        out[end..].copy_from_slice(&self.known);
        Ok(out)
    }
    pub fn prepare(
        &self,
        image: Option<&Image>,
        clip: [i32; 4],
        sub: [i32; 2],
        active: bool,
        addition: bool,
    ) -> Result<Option<Draw>, u32> {
        if !active {
            return Ok(None);
        }
        let f = image.map(Image::field_values).unwrap_or([0; 8]);
        let mut x = sub[0].wrapping_add(f[0]).wrapping_add(4) >> 3;
        let mut y = sub[1].wrapping_add(f[1]).wrapping_add(4) >> 3;
        let (mut w, mut h, mut xo, mut yo) = (f[4], f[5], 0i32, 0i32);
        if x < clip[0] {
            xo = clip[0].wrapping_sub(x);
            w = w.wrapping_sub(xo);
            x = clip[0];
        }
        if y < clip[1] {
            yo = clip[1].wrapping_sub(y);
            h = h.wrapping_sub(yo);
            y = clip[1];
        }
        if x.wrapping_add(w) > clip[2] {
            w = clip[2].wrapping_sub(x);
        }
        if y.wrapping_add(h) > clip[3] {
            h = clip[3].wrapping_sub(y);
        }
        if w <= 0 || h <= 0 {
            return Ok(None);
        }
        let dx = x.checked_sub(self.fields[1]).ok_or(6u32)?;
        let dy = y.checked_sub(self.fields[2]).ok_or(6u32)?;
        if xo < 0
            || yo < 0
            || dx < 0
            || dy < 0
            || f[6] < 0
            || xo.checked_add(w).filter(|&v| v <= f[4]).is_none()
            || yo.checked_add(h).filter(|&v| v <= f[5]).is_none()
            || dx.checked_add(w).filter(|&v| v <= self.fields[3]).is_none()
            || dy.checked_add(h).filter(|&v| v <= self.fields[4]).is_none()
        {
            return Err(6);
        }
        // Production DrawItem routes planar addition to ordinary Draw. Direct
        // planar AdditionDraw logs unsupported and returns without pixel writes.
        if addition && self.fields[0] == 1 {
            return Ok(None);
        }
        let scale = if self.fields[0] == 0 { 4 } else { 1 };
        let destination = (dy as usize)
            .checked_mul(self.fields[5] as usize)
            .and_then(|v| v.checked_add(dx as usize * scale))
            .ok_or(6u32)?;
        Ok(Some(Draw {
            origin: [xo, yo],
            extent: [w, h],
            source_pitch: f[6] as usize,
            destination,
            addition,
        }))
    }
    pub fn draw(&mut self, plan: &Draw, alpha: &Composite, switches: &[u32]) -> Result<(), u32> {
        let first = *switches.first().ok_or(6u32)?;
        if *switches.get(1).ok_or(6u32)? == u32::MAX {
            return self.segment(plan, alpha, 0, plan.extent[0] as usize, first);
        }
        if plan.addition {
            let mut translated = [0u32; 8];
            translated[0] = first;
            for i in 1..4 {
                translated[2 * i] = *switches.get(2 * i).ok_or(6u32)?;
                translated[2 * i + 1] = switches
                    .get(2 * i + 1)
                    .ok_or(6u32)?
                    .saturating_sub(plan.origin[0] as u32);
                if translated[2 * i + 1] >= plan.extent[0] as u32 {
                    translated[2 * i + 1] = plan.extent[0] as u32;
                    break;
                }
            }
            let (mut at, mut last) = (0usize, 0usize);
            while last < plan.extent[0] as usize {
                let next =
                    (*translated.get(at + 3).ok_or(6u32)? as usize).min(plan.extent[0] as usize);
                let color = *translated.get(at).ok_or(6u32)?;
                at += 2;
                if next < last {
                    continue;
                }
                self.segment(plan, alpha, last, next - last, color)?;
                last = next;
            }
        } else if self.fields[0] == 1 {
            let (mut at, mut last) = (0usize, plan.origin[0] as usize);
            let end = last + plan.extent[0] as usize;
            while last < end {
                let next = (*switches.get(at + 3).ok_or(6u32)? as usize).min(end);
                let color = *switches.get(at).ok_or(6u32)?;
                at += 2;
                if next < last {
                    continue;
                }
                self.segment(
                    plan,
                    alpha,
                    last - plan.origin[0] as usize,
                    next - last,
                    color,
                )?;
                last = next;
            }
        } else {
            // Native color survives rows while the switch cursor restarts.
            let mut color = first;
            for row in 0..plan.extent[1] as usize {
                let mut at = 0usize;
                for column in 0..plan.extent[0] as usize {
                    let x = column + plan.origin[0] as usize;
                    if x >= *switches.get(at + 1).ok_or(6u32)? as usize {
                        while x >= *switches.get(at + 1).ok_or(6u32)? as usize {
                            at += 2;
                        }
                        color = *switches.get(at - 2).ok_or(6u32)?;
                    }
                    self.pixel(plan, alpha, row, column, color)?;
                }
            }
        }
        Ok(())
    }
    fn segment(
        &mut self,
        plan: &Draw,
        alpha: &Composite,
        x: usize,
        width: usize,
        color: u32,
    ) -> Result<(), u32> {
        for row in 0..plan.extent[1] as usize {
            for column in x..x + width {
                self.pixel(plan, alpha, row, column, color)?;
            }
        }
        Ok(())
    }
    fn pixel(
        &mut self,
        plan: &Draw,
        alpha: &Composite,
        row: usize,
        column: usize,
        color: u32,
    ) -> Result<(), u32> {
        let src = (plan.origin[1] as usize + row)
            .checked_mul(plan.source_pitch)
            .and_then(|v| v.checked_add(plan.origin[0] as usize + column))
            .ok_or(6u32)?;
        let a = alpha.pixel(src)?;
        let base = plan.destination + row * self.fields[5] as usize;
        if self.fields[0] == 0 {
            let at = base + column * 4;
            if self.known.get(at..at + 4).ok_or(6u32)?.contains(&0) {
                return Err(6);
            }
            let pixel = self.values.get_mut(at..at + 4).ok_or(6u32)?;
            let old = u32::from_le_bytes(pixel.try_into().unwrap());
            let new = if plan.addition {
                crate::fill::add_pixel(old, a, color)
            } else {
                crate::fill::mix_pixel_sse(old, color, u32::from(a))
            };
            pixel.copy_from_slice(&new.to_le_bytes());
        } else {
            let channels = [0, (color >> 16) as u8, (color >> 8) as u8, color as u8];
            let plane = self.fields[5] as usize * self.fields[4] as usize;
            for i in [1, 2, 3, 0] {
                let at = base + column + i * plane;
                if *self.known.get(at).ok_or(6u32)? == 0 {
                    return Err(6);
                }
                crate::fill::plane_row(&mut self.values[at..at + 1], a, channels[i]);
            }
        }
        Ok(())
    }
    pub fn flip_alpha(&mut self) -> Result<(), u32> {
        if self.fields[0] != 0 {
            return Err(6);
        }
        for y in 0..self.fields[4] as usize {
            for x in 0..self.fields[3] as usize {
                self.values[y * self.fields[5] as usize + x * 4 + 3] ^= 255;
            }
        }
        Ok(())
    }
    pub fn debug_box(&mut self, color: u32) -> Result<(), u32> {
        if self.fields[0] != 0 {
            return Err(6);
        }
        let (w, h) = (self.fields[3] as usize, self.fields[4] as usize);
        for y in 0..h {
            for x in 0..w {
                if y == 0 || y + 1 == h || x == 0 || x + 1 == w {
                    let at = y * self.fields[5] as usize + x * 4;
                    self.values[at..at + 4].copy_from_slice(&color.to_le_bytes());
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn source(w: i32, h: i32) -> (Image, Composite) {
        let count = (w * h) as usize;
        let mut bytes = alloc::vec![0u8;32+count*2];
        for (i, v) in [0, 0, w * 8, h * 8, w, h, w, 3].into_iter().enumerate() {
            bytes[i * 4..i * 4 + 4].copy_from_slice(&v.to_le_bytes());
        }
        bytes[32..32 + count].fill(64);
        bytes[32 + count..].fill(1);
        let image = Image::decode(&bytes).unwrap();
        let p = crate::composite_mask::Parameters {
            clip: [0, 0, w, h],
            sub: [0, 0],
            body: true,
            border: false,
        };
        let alpha = crate::composite_mask::prepare(Some(&image), None, p, true)
            .unwrap()
            .unwrap()
            .finish(&image, None, &[0xff000000, u32::MAX])
            .unwrap();
        (image, alpha)
    }
    #[test]
    fn initialization_preserves_packed_padding_and_complete_planar_planes() {
        let packed = Bitmap::new([-3, 7, 0, 9], 0).unwrap();
        assert_eq!(packed.fields, [0, -3, 7, 3, 2, 64, 4, 1]);
        assert_eq!(&packed.values[..4], &[0, 0, 0, 255]);
        assert!(packed.known[..12].iter().all(|&v| v == 1));
        assert!(packed.known[12..64].iter().all(|&v| v == 0));
        let planar = Bitmap::new([-3, 7, 0, 9], 1).unwrap();
        assert_eq!(planar.fields, [1, -3, 7, 3, 2, 16, 13, 1]);
        assert_eq!(&planar.values[..32], &[255; 32]);
        assert!(planar.values[32..].iter().all(|&v| v == 0));
        assert!(planar.known.iter().all(|&v| v == 1));
        assert!(Bitmap::new([0, 0, 0, 0], 0).unwrap().values.is_empty());
        assert!(Bitmap::new([0, 0, i32::MAX, 1], 0).is_err());
        assert!(Bitmap::new([0, 0, 1, 1], 2).is_err());
    }
    #[test]
    fn packed_color_carries_across_rows_before_first_boundary() {
        let (image, alpha) = source(3, 2);
        let mut b = Bitmap::new([0, 0, 3, 2], 0).unwrap();
        let p = b
            .prepare(Some(&image), [0, 0, 3, 2], [0, 0], true, false)
            .unwrap()
            .unwrap();
        b.draw(&p, &alpha, &[0xff000011, 1, 0xff220000, 2, 0, u32::MAX])
            .unwrap();
        assert_eq!(&b.values[..12], &[17, 0, 0, 0, 17, 0, 0, 0, 0, 0, 34, 0]);
        assert_eq!(&b.values[64..76], &[0, 0, 34, 0, 17, 0, 0, 0, 0, 0, 34, 0]);
    }
    #[test]
    fn planar_and_addition_skip_backward_boundaries_without_rewinding() {
        let (image, alpha) = source(4, 2);
        for layout in [0, 1] {
            let mut b = Bitmap::new([0, 0, 4, 2], layout).unwrap();
            let p = b
                .prepare(Some(&image), [2, 0, 4, 2], [0, 0], true, layout == 0)
                .unwrap()
                .unwrap();
            b.draw(&p, &alpha, &[0xff112233, 0, 0xff445566, 1, 0, u32::MAX])
                .unwrap();
            if layout == 0 {
                assert_eq!(
                    &b.values[..16],
                    &[0, 0, 0, 255, 0, 0, 0, 255, 102, 85, 68, 0, 102, 85, 68, 0]
                );
            } else {
                assert_eq!(&b.values[..4], &[255, 255, 0, 0]);
                assert_eq!(&b.values[32..36], &[0, 0, 68, 68]);
                assert_eq!(&b.values[64..68], &[0, 0, 85, 85]);
                assert_eq!(&b.values[96..100], &[0, 0, 102, 102]);
            }
        }
    }
    #[test]
    fn final_actions_retain_padding_and_release_safe_empty_geometry() {
        let mut b = Bitmap::new([0, 0, 3, 3], 0).unwrap();
        b.flip_alpha().unwrap();
        b.debug_box(0x80402010).unwrap();
        assert_eq!(&b.values[..4], &[16, 32, 64, 128]);
        assert_eq!(&b.values[68..72], &[0, 0, 0, 0]);
        assert!(b.known[12..64].iter().all(|&v| v == 0));
        assert!(b
            .prepare(None, [0, 0, 3, 3], [0, 0], true, false)
            .unwrap()
            .is_none());
        let (image, alpha) = source(3, 3);
        let p = b
            .prepare(Some(&image), [0, 0, 3, 3], [0, 0], true, false)
            .unwrap()
            .unwrap();
        assert!(b.draw(&p, &alpha, &[0, 0, 0, 0]).is_err());
    }
}
