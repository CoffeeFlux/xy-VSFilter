// SPDX-License-Identifier: GPL-2.0-or-later
//! Safe coefficient construction. The target backend preserves the cleanup
//! floating-point recurrence; no coefficient address enters that backend.
use alloc::{collections::BTreeMap, vec::Vec};

pub trait Numeric {
    /// Always evaluates the exponential before the constructor's zero test.
    /// None is the original early return for sigma equal to initialized zero.
    fn begin(&mut self) -> Option<i32>;
    fn start(&mut self);
    fn next(&mut self) -> u32;
    fn normalize(&mut self, bits: u32) -> u32;
}
pub struct Coefficients {
    bytes: Vec<u8>,
}
impl Coefficients {
    pub fn build(sigma: u64, numeric: &mut impl Numeric) -> Result<Self, u32> {
        let Some(width) = numeric.begin() else {
            return Ok(Self {
                bytes: crate::be_blur::zeroed(24)?,
            });
        };
        let width = width | 1;
        let radius = width / 2;
        let extended = width.wrapping_add(3) & !3;
        let count = if extended > 0 { extended as usize } else { 0 };
        if width > 0 && (extended < width || radius < 0) {
            return Err(6);
        }
        let mut values = crate::be_blur::zeroed::<u32>(count)?;
        if width > 0 {
            numeric.start();
            values[radius as usize] = 1.0f32.to_bits();
            for i in 0..radius as usize {
                let bits = numeric.next();
                values[radius as usize - i - 1] = bits;
                values[radius as usize + i + 1] = bits;
            }
            for bits in &mut values[..width as usize] {
                *bits = numeric.normalize(*bits);
            }
        }
        let size = count
            .checked_mul(4)
            .and_then(|v| v.checked_add(24))
            .ok_or(5u32)?;
        let mut bytes = crate::be_blur::zeroed::<u8>(size)?;
        for (i, value) in [radius, width, extended, i32::from(count != 0)]
            .into_iter()
            .enumerate()
        {
            bytes[4 * i..4 * i + 4].copy_from_slice(&value.to_le_bytes());
        }
        bytes[16..24].copy_from_slice(&sigma.to_le_bytes());
        for (out, bits) in bytes[24..].chunks_exact_mut(4).zip(values) {
            out.copy_from_slice(&bits.to_le_bytes());
        }
        Ok(Self { bytes })
    }
    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }
}
pub struct Store {
    next: u64,
    values: BTreeMap<u64, Coefficients>,
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
    pub fn insert(&mut self, value: Coefficients) -> Result<u64, u32> {
        let id = self.next;
        self.next = self.next.checked_add(1).ok_or(5u32)?;
        self.values.insert(id, value);
        Ok(id)
    }
    pub fn get(&self, id: u64) -> Result<&Coefficients, u32> {
        self.values.get(&id).ok_or(1)
    }
    pub fn remove(&mut self, id: u64) -> Result<(), u32> {
        self.values.remove(&id).map(|_| ()).ok_or(1)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    struct Steps {
        width: Option<i32>,
        n: u32,
    }
    impl Numeric for Steps {
        fn begin(&mut self) -> Option<i32> {
            self.width
        }
        fn start(&mut self) {
            assert_eq!(self.n, 0);
        }
        fn next(&mut self) -> u32 {
            self.n += 1;
            self.n
        }
        fn normalize(&mut self, v: u32) -> u32 {
            v.wrapping_add(10)
        }
    }
    #[test]
    fn symmetric_recurrence_is_normalized_only_across_logical_width() {
        let mut numeric = Steps {
            width: Some(4),
            n: 0,
        };
        let out = Coefficients::build(123, &mut numeric).unwrap();
        let words: Vec<u32> = out.bytes()[24..]
            .chunks_exact(4)
            .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
            .collect();
        assert_eq!(words, [12, 11, 1.0f32.to_bits() + 10, 11, 12, 0, 0, 0]);
        assert_eq!(numeric.n, 2);
        assert_eq!(&out.bytes()[16..24], &123u64.to_le_bytes());
    }
    #[test]
    fn constructor_zero_and_negative_width_keep_the_original_metadata() {
        let mut numeric = Steps { width: None, n: 0 };
        assert_eq!(
            Coefficients::build(1 << 63, &mut numeric).unwrap().bytes(),
            &[0; 24]
        );
        numeric.width = Some(-2);
        let out = Coefficients::build(123, &mut numeric).unwrap();
        assert_eq!(
            &out.bytes()[..12],
            &[0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0]
        );
        assert_eq!(out.bytes().len(), 24);
        numeric.width = Some(i32::MAX);
        assert!(Coefficients::build(123, &mut numeric).is_err());
    }
}
