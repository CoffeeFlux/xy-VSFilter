// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned cleanup CompositeAlphaMask. Segments reuse the initial mask origin;
//! a boundary before the clipped start can initialize pixels outside dirty.
use crate::{be_blur::zeroed, clipper::Mask, raster::Image};
use alloc::vec::Vec;

#[derive(Clone, Copy)]
pub struct Parameters {
    pub clip: [i32; 4],
    pub sub: [i32; 2],
    pub body: bool,
    pub border: bool,
}
pub struct Geometry {
    dirty: [i32; 4],
    source: [i32; 8],
    origin: [i32; 2],
    extent: [i32; 2],
    parameters: Parameters,
}
fn intersection(a: [i32; 4], b: [i32; 4]) -> [i32; 4] {
    let r = [
        a[0].max(b[0]),
        a[1].max(b[1]),
        a[2].min(b[2]),
        a[3].min(b[3]),
    ];
    if r[0] < r[2] && r[1] < r[3] {
        r
    } else {
        [0; 4]
    }
}
pub fn prepare(
    image: Option<&Image>,
    mask: Option<&Mask>,
    p: Parameters,
    switches: bool,
) -> Result<Option<Geometry>, u32> {
    if !switches || (!p.body && !p.border) {
        return Ok(None);
    }
    let f = image.map(Image::field_values).unwrap_or([0; 8]);
    if p.border && f[7] & 4 == 0 {
        return Ok(None);
    }
    let mut clip = p.clip;
    if let Some(mask) = mask {
        let m = mask.fields();
        clip = intersection(
            clip,
            [m[0], m[1], m[0].wrapping_add(m[2]), m[1].wrapping_add(m[3])],
        );
    }
    let mut x = p.sub[0].wrapping_add(f[0]).wrapping_add(4) >> 3;
    let mut y = p.sub[1].wrapping_add(f[1]).wrapping_add(4) >> 3;
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
    // An invalid native address is unresolved, not a clipped substitute.
    if xo < 0
        || yo < 0
        || f[6] < f[4]
        || xo.checked_add(w).filter(|&v| v <= f[4]).is_none()
        || yo.checked_add(h).filter(|&v| v <= f[5]).is_none()
    {
        return Err(6);
    }
    Ok(Some(Geometry {
        dirty: [x, y, x.wrapping_add(w), y.wrapping_add(h)],
        source: f,
        origin: [xo, yo],
        extent: [w, h],
        parameters: p,
    }))
}
impl Geometry {
    pub fn finish(
        &self,
        image: &Image,
        mask: Option<&Mask>,
        switches: &[u32],
    ) -> Result<Composite, u32> {
        let count = usize::try_from(self.source[6].checked_mul(self.source[5]).ok_or(6u32)?)
            .map_err(|_| 6u32)?;
        let mut result = Composite {
            dirty: self.dirty,
            size: [self.source[4], self.source[5], self.source[6]],
            values: zeroed(count)?,
            known: zeroed(count)?,
        };
        if switches.get(1).ok_or(6u32)? == &u32::MAX {
            self.fill(
                &mut result,
                image,
                mask,
                self.origin[0],
                self.extent[0],
                (switches[0] >> 24) as u8,
            )?;
        } else {
            let end = self.origin[0] + self.extent[0];
            let mut last = self.origin[0];
            let mut at = 0usize;
            while last < end {
                let alpha = (*switches.get(at).ok_or(6u32)? >> 24) as u8;
                while *switches.get(at + 3).ok_or(6u32)? < end as u32
                    && (*switches.get(at + 2).ok_or(6u32)? >> 24) as u8 == alpha
                {
                    at = at.checked_add(2).ok_or(6u32)?;
                }
                let next = (*switches.get(at + 3).ok_or(6u32)?).min(end as u32) as i32;
                // Native advances the switch cursor even for empty/backward
                // intervals. A later segment may begin before the dirty rect.
                self.fill(
                    &mut result,
                    image,
                    mask,
                    last,
                    next.wrapping_sub(last),
                    alpha,
                )?;
                last = next;
                at = at.checked_add(2).ok_or(6u32)?;
            }
        }
        Ok(result)
    }
    fn fill(
        &self,
        result: &mut Composite,
        image: &Image,
        mask: Option<&Mask>,
        x: i32,
        w: i32,
        alpha: u8,
    ) -> Result<(), u32> {
        if w <= 0 {
            return Ok(());
        }
        let mask = mask.filter(|m| m.fields()[5] & 1 != 0);
        let (mask_origin, mask_pitch) = if let Some(mask) = mask {
            let m = mask.fields();
            let origin = m[4]
                .wrapping_mul(self.dirty[1])
                .wrapping_add(self.dirty[0])
                .wrapping_sub(m[1].wrapping_mul(m[4]))
                .wrapping_sub(m[0]);
            (
                usize::try_from(origin).map_err(|_| 6u32)?,
                usize::try_from(m[4]).map_err(|_| 6u32)?,
            )
        } else {
            (0, 0)
        };
        let pitch = self.source[6] as usize;
        let subtract = self.parameters.border && !self.parameters.body;
        for row in 0..self.extent[1] as usize {
            let start = (self.origin[1] as usize + row)
                .checked_mul(pitch)
                .and_then(|n| n.checked_add(usize::try_from(x).ok()?))
                .ok_or(6u32)?;
            let mask_start = mask_origin
                .checked_add(row.checked_mul(mask_pitch).ok_or(6u32)?)
                .ok_or(6u32)?;
            for column in 0..w as usize {
                let at = start.checked_add(column).ok_or(6u32)?;
                let source = image.plane_pixel(self.parameters.border, at)?;
                let body = if subtract {
                    image.plane_pixel(false, at)?
                } else {
                    0
                };
                let coverage = if let Some(mask) = mask {
                    mask.pixel(mask_start.checked_add(column).ok_or(6u32)?)?
                } else {
                    0
                };
                let flags = u32::from(subtract) | (u32::from(mask.is_some()) << 1);
                *result.values.get_mut(at).ok_or(6u32)? =
                    crate::mask::scalar(source, body, coverage, i32::from(alpha), flags);
                result.known[at] = 1;
            }
        }
        Ok(())
    }
}
pub struct Composite {
    dirty: [i32; 4],
    size: [i32; 3],
    values: Vec<u8>,
    known: Vec<u8>,
}
impl Composite {
    pub(crate) fn pixel(&self, at: usize) -> Result<u8, u32> {
        if *self.known.get(at).ok_or(6u32)? == 0 {
            return Err(6);
        }
        self.values.get(at).copied().ok_or(6)
    }
    pub fn wire_len(&self) -> Result<usize, u32> {
        self.values
            .len()
            .checked_mul(2)
            .and_then(|n| n.checked_add(32))
            .ok_or(5)
    }
    pub fn encode(&self) -> Result<Vec<u8>, u32> {
        let mut out = zeroed(self.wire_len()?)?;
        for (i, v) in self
            .dirty
            .iter()
            .chain(self.size.iter())
            .chain(core::iter::once(&1))
            .enumerate()
        {
            out[i * 4..i * 4 + 4].copy_from_slice(&v.to_le_bytes());
        }
        let end = 32 + self.values.len();
        out[32..end].copy_from_slice(&self.values);
        out[end..].copy_from_slice(&self.known);
        Ok(out)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn image(values: &[u8], known: &[u8]) -> Image {
        let width = values.len() as i32;
        let mut wire = alloc::vec![0;32+values.len()*2];
        for (i, v) in [0, 0, width * 8, 8, width, 1, width, 3]
            .into_iter()
            .enumerate()
        {
            wire[i * 4..i * 4 + 4].copy_from_slice(&v.to_le_bytes());
        }
        wire[32..32 + values.len()].copy_from_slice(values);
        wire[32 + values.len()..].copy_from_slice(known);
        Image::decode(&wire).unwrap()
    }
    fn p() -> Parameters {
        Parameters {
            clip: [0, 0, 4, 1],
            sub: [0; 2],
            body: true,
            border: false,
        }
    }
    struct N;
    impl crate::clipper::Numeric for N {
        fn scale(&mut self, _: i32, _: u64) -> i32 {
            unreachable!()
        }
    }
    fn mask() -> Mask {
        let source = image(&[0, 16, 32, 64], &[1; 4]);
        crate::clipper::Request::begin(
            crate::clipper::Parameters {
                size: [4, 1],
                inverse: false,
                effect: -1,
                values: [0; 4],
                scales: [0; 2],
            },
            &mut N,
        )
        .unwrap()
        .finish(Some(&source), &mut N)
        .unwrap()
        .unwrap()
    }
    #[test]
    fn color_segments_restart_mask_but_equal_alpha_coalesces() {
        let source = image(&[64; 4], &[1; 4]);
        let mask = mask();
        let g = prepare(Some(&source), Some(&mask), p(), true)
            .unwrap()
            .unwrap();
        let split = g
            .finish(
                &source,
                Some(&mask),
                &[0x80000000, 0, 0xff000000, 2, 0, u32::MAX],
            )
            .unwrap();
        assert_eq!(split.values, [0, 32, 0, 64]);
        assert_eq!(split.known, [1; 4]);
        let equal = g
            .finish(
                &source,
                Some(&mask),
                &[0xff000000, 0, 0xff123456, 2, 0, u32::MAX],
            )
            .unwrap();
        assert_eq!(equal.values, [0, 64, 128, 255]);
    }
    #[test]
    fn backward_boundary_initializes_pixels_before_dirty_rect() {
        let source = image(&[64; 4], &[1; 4]);
        let mut p = p();
        p.clip[0] = 2;
        let g = prepare(Some(&source), None, p, true).unwrap().unwrap();
        let output = g
            .finish(&source, None, &[0x80000000, 0, 0xff000000, 1, 0, u32::MAX])
            .unwrap();
        assert_eq!(output.dirty, [2, 0, 4, 1]);
        assert_eq!(output.values, [0, 255, 255, 255]);
        assert_eq!(output.known, [0, 1, 1, 1]);
    }
    #[test]
    fn clipped_unknown_source_is_not_read_but_written_unknown_is_unresolved() {
        let source = image(&[7, 64, 65, 255], &[0, 1, 1, 1]);
        let mut p = p();
        p.clip[0] = 1;
        let g = prepare(Some(&source), None, p, true).unwrap().unwrap();
        let output = g.finish(&source, None, &[0xff000000, u32::MAX]).unwrap();
        assert_eq!(output.known, [0, 1, 1, 1]);
        let g = prepare(
            Some(&source),
            None,
            Parameters {
                clip: [0, 0, 4, 1],
                ..p
            },
            true,
        )
        .unwrap()
        .unwrap();
        assert!(matches!(
            g.finish(&source, None, &[0xff000000, u32::MAX]),
            Err(6)
        ));
        assert!(matches!(g.finish(&source, None, &[0, 0, 0, 0]), Err(6)));
    }
    #[test]
    fn absent_border_and_empty_intersection_preserve_empty_output() {
        let source = image(&[64; 4], &[1; 4]);
        let mut p = p();
        p.border = true;
        assert!(prepare(Some(&source), None, p, true).unwrap().is_none());
        p.border = false;
        p.clip = [10, 10, 12, 12];
        assert!(prepare(Some(&source), Some(&mask()), p, true)
            .unwrap()
            .is_none());
        assert!(prepare(Some(&source), None, p, false).unwrap().is_none());
    }
}
