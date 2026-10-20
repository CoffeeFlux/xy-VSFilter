// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned equivalent of cleanup's weak Gaussian coefficient table. Expired
//! NaN-key entries retain initialized keys, never dangling coefficient storage.
use crate::gaussian_coefficients::Coefficients;
use alloc::{collections::BTreeMap, vec::Vec};

pub trait Numeric {
    fn copy(&mut self, key: u64) -> u64;
    fn equal(&mut self, lookup: u64, stored: u64) -> bool;
    fn build(&mut self, key: u64) -> Result<Coefficients, u32>;
    /// Returns zero when no growth is required, otherwise ceil(size/max_load).
    fn growth(&mut self, size: usize, buckets: usize) -> Result<usize, u32>;
}

fn hash(key: u64) -> usize {
    515usize
        .wrapping_mul(33)
        .wrapping_add(key as u32 as i32 as usize)
        .wrapping_mul(33)
        .wrapping_add((key >> 32) as u32 as i32 as usize)
}
struct Value {
    deleter_key: u64,
    references: usize,
    coefficients: Coefficients,
}
struct Entry {
    key: u64,
    bucket: usize,
}
pub struct Cache {
    next: u64,
    handles: BTreeMap<u64, u64>,
    values: BTreeMap<u64, Value>,
    entries: BTreeMap<u64, Entry>,
    order: Vec<u64>,
    buckets: BTreeMap<usize, Vec<u64>>,
    bucket_count: usize,
    live_bytes: usize,
}
impl Default for Cache {
    fn default() -> Self {
        Self::new()
    }
}
impl Cache {
    pub const fn new() -> Self {
        Self {
            next: 1,
            handles: BTreeMap::new(),
            values: BTreeMap::new(),
            entries: BTreeMap::new(),
            order: Vec::new(),
            buckets: BTreeMap::new(),
            bucket_count: 8,
            live_bytes: 0,
        }
    }
    pub fn stats(&self) -> [u64; 2] {
        [self.entries.len() as u64, self.live_bytes as u64]
    }
    pub fn identity(&self, handle: u64) -> Result<u64, u32> {
        self.handles.get(&handle).copied().ok_or(1)
    }
    pub fn get(&self, handle: u64) -> Result<&Coefficients, u32> {
        self.values
            .get(&self.identity(handle)?)
            .map(|v| &v.coefficients)
            .ok_or(1)
    }
    fn find(&self, key: u64, hashed: usize, n: &mut impl Numeric) -> Option<u64> {
        self.buckets
            .get(&(hashed & (self.bucket_count - 1)))?
            .iter()
            .rev()
            .find(|id| n.equal(key, self.entries[id].key))
            .copied()
    }
    fn erase(&mut self, id: u64) {
        let bucket = self.entries.remove(&id).unwrap().bucket;
        self.order.retain(|&v| v != id);
        let values = self.buckets.get_mut(&bucket).unwrap();
        values.retain(|&v| v != id);
        if values.is_empty() {
            self.buckets.remove(&bucket);
        }
    }
    fn rehash(&mut self, count: usize, n: &mut impl Numeric) {
        let mut order = self.order.clone();
        let mut buckets: BTreeMap<usize, Vec<u64>> = BTreeMap::new();
        for id in &self.order {
            let key = self.entries[id].key;
            let number = hash(key) & (count - 1);
            self.entries.get_mut(id).unwrap().bucket = number;
            let bucket = buckets.entry(number).or_default();
            if bucket.is_empty() {
                bucket.push(*id);
                continue;
            }
            let mut at = 0;
            for (index, previous) in bucket.iter().enumerate().rev() {
                if n.equal(key, self.entries[previous].key) {
                    at = index + 1;
                    break;
                }
            }
            let old = order.iter().position(|v| v == id).unwrap();
            order.remove(old);
            let insertion = if at < bucket.len() {
                order.iter().position(|v| *v == bucket[at]).unwrap()
            } else {
                order
                    .iter()
                    .position(|v| *v == *bucket.last().unwrap())
                    .unwrap()
                    + 1
            };
            order.insert(insertion, *id);
            bucket.insert(at, *id);
        }
        self.order = order;
        self.buckets = buckets;
        self.bucket_count = count;
    }
    pub fn acquire(&mut self, key: u64, n: &mut impl Numeric) -> Result<u64, u32> {
        let handle = self.next;
        self.next = self.next.checked_add(1).ok_or(5u32)?;
        let hashed = hash(key);
        if let Some(id) = self.find(key, hashed, n) {
            if let Some(value) = self.values.get_mut(&id) {
                value.references = value.references.checked_add(1).ok_or(5u32)?;
                self.handles.insert(handle, id);
                return Ok(handle);
            }
            self.erase(id);
        }
        // These are distinct native x86 load/spills. The hashed input key,
        // constructor argument, captured deleter key and stored key can differ.
        let argument = n.copy(key);
        let coefficients = n.build(argument)?;
        let deleter_key = n.copy(key);
        if self.find(key, hashed, n).is_none() {
            let stored = n.copy(key);
            let required = n.growth(
                self.entries.len().checked_add(1).ok_or(5u32)?,
                self.bucket_count,
            )?;
            if required != 0 {
                let required = required.max(8);
                let target = if self.bucket_count < 512 && self.bucket_count * 8 >= required {
                    self.bucket_count * 8
                } else {
                    required
                };
                self.rehash(target.checked_next_power_of_two().ok_or(5u32)?, n);
                // Native repeats lookup using the copied node key, but the
                // original hash, to locate insertion after a forced rehash.
                let _ = self.find(stored, hashed, n);
            }
            let number = hashed & (self.bucket_count - 1);
            let bucket = self.buckets.entry(number).or_default();
            let at = bucket
                .first()
                .map(|first| self.order.iter().position(|v| v == first).unwrap())
                .unwrap_or(self.order.len());
            self.order.insert(at, handle);
            bucket.insert(0, handle);
            self.entries.insert(
                handle,
                Entry {
                    key: stored,
                    bucket: number,
                },
            );
        }
        self.live_bytes = self
            .live_bytes
            .checked_add(coefficients.bytes().len() - 24)
            .ok_or(5u32)?;
        self.values.insert(
            handle,
            Value {
                deleter_key,
                references: 1,
                coefficients,
            },
        );
        self.handles.insert(handle, handle);
        Ok(handle)
    }
    pub fn release(&mut self, handle: u64, n: &mut impl Numeric) -> Result<(), u32> {
        let id = self.handles.remove(&handle).ok_or(1u32)?;
        let value = self.values.get_mut(&id).ok_or(1u32)?;
        value.references -= 1;
        if value.references != 0 {
            return Ok(());
        }
        let key = value.deleter_key;
        if self.find(key, hash(key), n) == Some(id) {
            self.erase(id);
        }
        let value = self.values.remove(&id).unwrap();
        self.live_bytes -= value.coefficients.bytes().len() - 24;
        Ok(())
    }
    pub fn gaussian(
        &mut self,
        image: &crate::raster::Image,
        sigma: [u64; 2],
        n: &mut impl Numeric,
        filter: &mut impl crate::gaussian_filter::Numeric,
    ) -> Result<crate::raster::Image, u32> {
        let x = self.acquire(sigma[0], n)?;
        let y = match self.acquire(sigma[1], n) {
            Ok(v) => v,
            Err(e) => {
                self.release(x, n)?;
                return Err(e);
            }
        };
        let result = (|| {
            let kernels = [self.get(x)?.kernel()?, self.get(y)?.kernel()?];
            image.gaussian(&kernels, filter)
        })();
        self.release(y, n)?;
        self.release(x, n)?;
        result
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    struct Steps;
    impl crate::gaussian_coefficients::Numeric for Steps {
        fn begin(&mut self) -> Option<(i32, u64)> {
            None
        }
        fn start(&mut self) {
            unreachable!()
        }
        fn next(&mut self) -> u32 {
            unreachable!()
        }
        fn normalize(&mut self, _: u32) -> u32 {
            unreachable!()
        }
    }
    struct N;
    impl Numeric for N {
        fn copy(&mut self, k: u64) -> u64 {
            k
        }
        fn equal(&mut self, a: u64, b: u64) -> bool {
            f64::from_bits(a) == f64::from_bits(b)
        }
        fn build(&mut self, _: u64) -> Result<Coefficients, u32> {
            Coefficients::build(&mut Steps)
        }
        fn growth(&mut self, s: usize, b: usize) -> Result<usize, u32> {
            Ok(if s > b { s } else { 0 })
        }
    }
    #[test]
    fn shared_values_release_once_and_stale_handles_cannot_resurrect() {
        let mut cache = Cache::new();
        let mut n = N;
        let a = cache.acquire(1f64.to_bits(), &mut n).unwrap();
        let b = cache.acquire(1f64.to_bits(), &mut n).unwrap();
        assert_ne!(a, b);
        assert_eq!(cache.identity(a), cache.identity(b));
        assert_eq!(cache.stats(), [1, 0]);
        cache.release(a, &mut n).unwrap();
        assert!(cache.get(a).is_err());
        assert!(cache.get(b).is_ok());
        cache.release(b, &mut n).unwrap();
        assert_eq!(cache.stats(), [0, 0]);
        assert_eq!(cache.release(b, &mut n), Err(1));
        let c = cache.acquire(1f64.to_bits(), &mut n).unwrap();
        assert_ne!(c, b);
    }
    #[test]
    fn expired_nan_keys_survive_rehash_without_retaining_payloads() {
        let mut cache = Cache::new();
        let mut n = N;
        for _ in 0..80 {
            let id = cache.acquire(f64::NAN.to_bits(), &mut n).unwrap();
            cache.release(id, &mut n).unwrap();
        }
        assert_eq!(cache.stats(), [80, 0]);
        assert_eq!(cache.bucket_count, 512);
        assert!(cache.values.is_empty());
        let a = cache.acquire(0, &mut n).unwrap();
        let b = cache.acquire((-0f64).to_bits(), &mut n).unwrap();
        assert_eq!(cache.identity(a), cache.identity(b));
        cache.release(a, &mut n).unwrap();
        cache.release(b, &mut n).unwrap();
        assert_eq!(cache.stats(), [80, 0]);
    }
}
