// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned cleanup clip-mask construction. Preparation precedes polygon painting;
//! scroll endpoint conversion follows successful base construction, in order.
use crate::{be_blur::zeroed, raster::Image};
use alloc::{collections::BTreeMap, vec::Vec};

pub trait Numeric {
    fn scale(&mut self, value: i32, scale: u64) -> i32;
}
#[derive(Clone, Copy)]
pub struct Parameters {
    pub size: [i32; 2],
    pub inverse: bool,
    pub effect: i32,
    pub values: [i32; 4], // top, bottom, banner width, scroll height
    pub scales: [u64; 2],
}
#[derive(Clone, Copy)]
pub struct Request {
    parameters: Parameters,
    fade: i32,
}
impl Request {
    pub fn begin(parameters: Parameters, n: &mut impl Numeric) -> Option<Self> {
        let fade = match parameters.effect {
            -1 => 0,
            3 => n.scale(parameters.values[2], parameters.scales[0]),
            4 => n.scale(parameters.values[3], parameters.scales[1]),
            _ => return None,
        };
        let minimum = i32::from(parameters.effect != -1 || parameters.inverse);
        if parameters.size.iter().any(|&v| v < minimum) {
            return None;
        }
        Some(Self { parameters, fade })
    }
    pub fn finish(&self, image: Option<&Image>, n: &mut impl Numeric) -> Result<Option<Mask>, u32> {
        let p = self.parameters;
        let f = image.map(Image::field_values).unwrap_or([0; 8]);
        let x = f[0].wrapping_add(4) >> 3;
        let y = f[1].wrapping_add(4) >> 3;
        let body = image.and_then(Image::body);
        if p.effect == -1 && !p.inverse {
            let mut result =
                Mask::empty([x, y, f[4], f[5], f[6], if body.is_some() { 3 } else { 2 }]);
            if let Some((values, known)) = body {
                result.values = zeroed(values.len())?;
                result.values.copy_from_slice(values);
                if let Some(known) = known {
                    let mut copied = zeroed(known.len())?;
                    copied.copy_from_slice(known);
                    result.known = Some(copied);
                }
            }
            return Ok(Some(result));
        }
        let [width, height] = p.size;
        let (mut x, mut y, mut w, mut h, mut xo, mut yo) = (x, y, f[4], f[5], 0i32, 0i32);
        if x < 0 {
            xo = x.wrapping_neg();
            w = w.wrapping_sub(xo);
            x = 0;
        }
        if y < 0 {
            yo = y.wrapping_neg();
            h = h.wrapping_sub(yo);
            y = 0;
        }
        if x.wrapping_add(w) > width {
            w = width.wrapping_sub(x);
        }
        if y.wrapping_add(h) > height {
            h = height.wrapping_sub(y);
        }
        if w <= 0 || h <= 0 {
            return Ok(None);
        }
        // Preserve wrapping metadata, but never emulate an invalid native address.
        if x < 0
            || y < 0
            || xo < 0
            || yo < 0
            || x.checked_add(w).filter(|&v| v <= width).is_none()
            || y.checked_add(h).filter(|&v| v <= height).is_none()
        {
            return Err(6);
        }
        let count = (width as usize).checked_mul(height as usize).ok_or(6u32)?;
        if count > isize::MAX as usize {
            return Err(6);
        }
        let mut result = Mask::empty([0, 0, width, height, width, 1]);
        result.values = zeroed(count)?;
        result.values.fill(if p.inverse { 64 } else { 0 });
        let (source, known) = body.ok_or(6u32)?;
        for row in 0..h as usize {
            let start = (yo as usize)
                .checked_add(row)
                .and_then(|v| v.checked_mul(f[6] as usize))
                .and_then(|v| v.checked_add(xo as usize))
                .ok_or(6u32)?;
            let end = start.checked_add(w as usize).ok_or(6u32)?;
            let src = source.get(start..end).ok_or(6u32)?;
            if known.is_some_and(|v| v[start..end].contains(&0)) {
                return Err(6);
            }
            let at = (y as usize + row) * width as usize + x as usize;
            for (out, &input) in result.values[at..at + w as usize].iter_mut().zip(src) {
                *out = if p.inverse {
                    64u8.wrapping_sub(input)
                } else {
                    input
                };
            }
        }
        match p.effect {
            3 => result.banner(self.fade)?,
            4 => {
                // Native divides before converting the top endpoint.
                let delta = 16384i32.checked_div(self.fade).ok_or(6u32)?;
                let top = n.scale(p.values[0], p.scales[1]) >> 3;
                result.scroll(top, top.wrapping_add(self.fade), 0, delta, true)?;
                let delta = (-16384i32).checked_div(self.fade).ok_or(6u32)?;
                let bottom = n.scale(p.values[1], p.scales[1]) >> 3;
                result.scroll(bottom.wrapping_sub(self.fade), bottom, 16384, delta, false)?;
            }
            _ => {}
        }
        Ok(Some(result))
    }
}
pub struct Mask {
    fields: [i32; 6], // left, top, width, height, pitch, present/shared flags
    values: Vec<u8>,
    known: Option<Vec<u8>>,
}
impl Mask {
    fn empty(fields: [i32; 6]) -> Self {
        Self {
            fields,
            values: Vec::new(),
            known: None,
        }
    }
    fn banner(&mut self, fade: i32) -> Result<(), u32> {
        let delta = 16384i32.checked_div(fade).ok_or(6u32)?;
        let width = self.fields[2];
        for row in self.values.chunks_exact_mut(width as usize) {
            let mut alpha = 0i32;
            for byte in &mut row[..fade.min(width).max(0) as usize] {
                *byte = (i32::from(*byte).wrapping_mul(alpha) >> 14) as u8;
                alpha = alpha.wrapping_add(delta);
            }
            alpha = 16384;
            let mut k = width.wrapping_sub(fade);
            if k < 0 {
                alpha = alpha.wrapping_sub(k.wrapping_neg().wrapping_mul(delta));
                k = 0;
            }
            if k < width {
                for byte in &mut row[k as usize..] {
                    *byte = (i32::from(*byte).wrapping_mul(alpha) >> 14) as u8;
                    alpha = alpha.wrapping_sub(delta);
                }
            }
        }
        Ok(())
    }
    fn scroll(
        &mut self,
        mut first: i32,
        last: i32,
        mut alpha: i32,
        delta: i32,
        top: bool,
    ) -> Result<(), u32> {
        let width = self.fields[2] as usize;
        let height = self.fields[3];
        if first < 0 {
            alpha = alpha.wrapping_add(first.wrapping_neg().wrapping_mul(delta));
            first = 0;
        }
        let last = last.min(height);
        if first >= height {
            return Ok(());
        }
        let start = first as usize * width;
        let end = last.max(first) as usize * width;
        if top {
            self.values[..start].fill(0);
        }
        for row in self.values[start..end].chunks_exact_mut(width) {
            for byte in row {
                *byte = (i32::from(*byte).wrapping_mul(alpha) >> 14) as u8;
            }
            alpha = alpha.wrapping_add(delta);
        }
        if !top {
            self.values[end..].fill(0);
        }
        Ok(())
    }
    pub fn wire_len(&self) -> Result<usize, u32> {
        self.values
            .len()
            .checked_mul(2)
            .and_then(|v| v.checked_add(24))
            .ok_or(5)
    }
    pub fn encode(&self) -> Result<Vec<u8>, u32> {
        let mut out = zeroed(self.wire_len()?)?;
        for (i, v) in self.fields.iter().enumerate() {
            out[i * 4..i * 4 + 4].copy_from_slice(&v.to_le_bytes());
        }
        let end = 24 + self.values.len();
        out[24..end].copy_from_slice(&self.values);
        if let Some(known) = &self.known {
            out[end..].copy_from_slice(known);
        } else {
            out[end..].fill(1);
        }
        Ok(out)
    }
    pub fn duplicate(&self) -> Result<Self, u32> {
        let mut values = zeroed(self.values.len())?;
        values.copy_from_slice(&self.values);
        let known = if let Some(v) = &self.known {
            let mut k = zeroed(v.len())?;
            k.copy_from_slice(v);
            Some(k)
        } else {
            None
        };
        Ok(Self {
            fields: self.fields,
            values,
            known,
        })
    }
}
pub struct Store<T> {
    next: u64,
    values: BTreeMap<u64, T>,
}
impl<T> Default for Store<T> {
    fn default() -> Self {
        Self::new()
    }
}
impl<T> Store<T> {
    pub const fn new() -> Self {
        Self {
            next: 1,
            values: BTreeMap::new(),
        }
    }
    pub fn insert(&mut self, value: T) -> Result<u64, u32> {
        let id = self.next;
        self.next = self.next.checked_add(1).ok_or(5u32)?;
        self.values.insert(id, value);
        Ok(id)
    }
    pub fn get(&self, id: u64) -> Result<&T, u32> {
        self.values.get(&id).ok_or(1)
    }
    pub fn remove(&mut self, id: u64) -> Result<(), u32> {
        self.values.remove(&id).map(|_| ()).ok_or(1)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    struct N(Vec<i32>);
    impl Numeric for N {
        fn scale(&mut self, value: i32, _: u64) -> i32 {
            self.0.push(value);
            value
        }
    }
    fn image(width: i32, height: i32, offset: i32, values: &[u8], known: &[u8]) -> Image {
        let mut wire = alloc::vec![0;32+values.len()*2];
        for (i, v) in [offset, 0, width * 8, height * 8, width, height, width, 3]
            .into_iter()
            .enumerate()
        {
            wire[4 * i..4 * i + 4].copy_from_slice(&v.to_le_bytes());
        }
        wire[32..32 + values.len()].copy_from_slice(values);
        wire[32 + values.len()..].copy_from_slice(known);
        Image::decode(&wire).unwrap()
    }
    fn p(effect: i32) -> Parameters {
        Parameters {
            size: [3, 1],
            inverse: false,
            effect,
            values: [8, 24, 4, 2],
            scales: [1f64.to_bits(); 2],
        }
    }
    #[test]
    fn inverse_intersection_preserves_wrapping_coverage_and_empty_result() {
        let image = image(4, 1, -8, &[0, 64, 65, 255], &[1; 4]);
        let mut n = N(Vec::new());
        let mut p = p(-1);
        p.inverse = true;
        let request = Request::begin(p, &mut n).unwrap();
        let mask = request.finish(Some(&image), &mut n).unwrap().unwrap();
        assert_eq!(mask.values, [0, 255, 65]);
        assert!(n.0.is_empty());
        let outside = image_with_offset(32);
        assert!(request.finish(Some(&outside), &mut n).unwrap().is_none());
    }
    fn image_with_offset(offset: i32) -> Image {
        image(1, 1, offset, &[64], &[1])
    }
    #[test]
    fn simple_zero_target_retains_values_and_unknown_bytes_after_source_drop() {
        let image = image(3, 1, -8, &[64, 0, 255], &[1, 0, 1]);
        let mut n = N(Vec::new());
        let mut p = p(-1);
        p.size = [0, 0];
        let request = Request::begin(p, &mut n).unwrap();
        let mask = request.finish(Some(&image), &mut n).unwrap().unwrap();
        drop(image);
        assert_eq!(mask.fields, [-1, 0, 3, 1, 3, 3]);
        assert_eq!(mask.encode().unwrap()[24..], [64, 0, 255, 1, 0, 1]);
        p.inverse = true;
        assert!(Request::begin(p, &mut n).is_none());
    }
    #[test]
    fn base_refuses_unknown_logical_input_and_banner_overlaps_in_order() {
        let mut n = N(Vec::new());
        let request = Request::begin(p(3), &mut n).unwrap();
        let unknown = image(3, 1, 0, &[64; 3], &[1, 0, 1]);
        assert!(matches!(request.finish(Some(&unknown), &mut n), Err(6)));
        let input = image(3, 1, 0, &[64; 3], &[1; 3]);
        assert_eq!(
            request
                .finish(Some(&input), &mut n)
                .unwrap()
                .unwrap()
                .values,
            [0, 8, 8]
        );
        let mut p = p(3);
        p.values[2] = -4;
        let request = Request::begin(p, &mut n).unwrap();
        assert_eq!(
            request
                .finish(Some(&input), &mut n)
                .unwrap()
                .unwrap()
                .values,
            [64; 3]
        );
    }
    #[test]
    fn scroll_conversion_order_and_no_base_early_return() {
        let mut n = N(Vec::new());
        let mut p = p(4);
        p.size = [1, 4];
        let request = Request::begin(p, &mut n).unwrap();
        assert_eq!(n.0, [2]);
        assert!(request.finish(None, &mut n).unwrap().is_none());
        assert_eq!(n.0, [2]);
        let input = image(1, 4, 0, &[64; 4], &[1; 4]);
        let mask = request.finish(Some(&input), &mut n).unwrap().unwrap();
        assert_eq!(n.0, [2, 8, 24]);
        assert_eq!(mask.values, [0, 0, 16, 0]);
        p.size = [0, 0];
        assert!(Request::begin(p, &mut n).is_none());
        assert_eq!(n.0, [2, 8, 24, 2]);
    }
}
