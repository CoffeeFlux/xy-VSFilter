// SPDX-License-Identifier: GPL-2.0-or-later
//! Safe ownership for the cleanup's XyInternedByKey<STSStyleBase, CMyFont>.
//! This table owns keys even after their weak value expires. Unlike style
//! interning, looking past a released NaN key does not read freed storage.
//! Windows font creation/selection remains a separate boundary, not ported here.
#![forbid(unsafe_code)]
use alloc::collections::BTreeMap;
use alloc::sync::{Arc, Weak};
use alloc::vec::Vec;

#[derive(Clone, Debug)]
pub struct Key {
    pub char_set: i32,
    pub font_name: Vec<u16>,
    pub font_size: f64,
    pub font_weight: i32,
    pub italic: bool,
    pub underline: bool,
    pub strikeout: bool,
}
impl Key {
    pub fn native_eq(&self, other: &Self) -> bool {
        fn prefix(v: &[u16]) -> &[u16] {
            &v[..v.iter().position(|&x| x == 0).unwrap_or(v.len())]
        }
        self.char_set == other.char_set
            && prefix(&self.font_name) == prefix(&other.font_name)
            && self.font_size == other.font_size
            && self.font_weight == other.font_weight
            && self.italic == other.italic
            && self.underline == other.underline
            && self.strikeout == other.strikeout
    }
    pub fn hash(&self) -> usize {
        fn mix(h: usize, v: usize) -> usize {
            h.wrapping_mul(33).wrapping_add(v)
        }
        let name = self
            .font_name
            .iter()
            .take_while(|&&v| v != 0)
            .fold(0u32, |h, &v| h.wrapping_mul(33).wrapping_add(v as u32));
        let bits = self.font_size.to_bits();
        let double = mix(
            mix(515, bits as u32 as i32 as usize),
            (bits >> 32) as u32 as i32 as usize,
        );
        let mut h = mix(name as usize, self.char_set as usize);
        h = mix(h, double);
        h = mix(h, self.font_weight as usize);
        for v in [self.italic, self.underline, self.strikeout] {
            h = mix(h, v as usize);
        }
        h
    }
}
pub struct Value<V> {
    identity: u64,
    key: Key,
    payload: V,
}
impl<V> Value<V> {
    pub fn identity(&self) -> u64 {
        self.identity
    }
    pub fn key(&self) -> &Key {
        &self.key
    }
    pub fn payload(&self) -> &V {
        &self.payload
    }
}
struct Entry<V> {
    hash: usize,
    key: Key,
    value: Weak<Value<V>>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Identity,
    Capacity,
    Construction,
}
pub struct Table<V> {
    entries: BTreeMap<u64, Entry<V>>,
    order: Vec<u64>,
    buckets: BTreeMap<usize, Vec<u64>>,
    bucket_count: usize,
}
impl<V> Default for Table<V> {
    fn default() -> Self {
        Self::new()
    }
}
impl<V> Table<V> {
    pub const fn new() -> Self {
        Self {
            entries: BTreeMap::new(),
            order: Vec::new(),
            buckets: BTreeMap::new(),
            bucket_count: 8,
        }
    }
    pub fn count(&self) -> usize {
        self.entries.len()
    }
    pub fn bucket_count(&self) -> usize {
        self.bucket_count
    }
    fn find(
        &self,
        key: &Key,
        hash: usize,
        equal: &mut impl FnMut(&Key, &Key) -> bool,
    ) -> Option<u64> {
        self.buckets
            .get(&(hash & (self.bucket_count - 1)))?
            .iter()
            .rev()
            .find(|id| equal(key, &self.entries[id].key))
            .copied()
    }
    fn erase(&mut self, id: u64) {
        let key = self.entries.remove(&id).unwrap().hash & (self.bucket_count - 1);
        self.order.retain(|&v| v != id);
        let bucket = self.buckets.get_mut(&key).unwrap();
        bucket.retain(|&v| v != id);
        if bucket.is_empty() {
            self.buckets.remove(&key);
        }
    }
    fn growth(&self) -> Result<Option<usize>, Error> {
        let size = self.count().checked_add(1).ok_or(Error::Capacity)?;
        if (size as f32) / (self.bucket_count as f32) <= 1.0 {
            return Ok(None);
        }
        let rounded = size as f32;
        // Avoid the unsigned conversion's extra inexact operation, as observed
        // in style ownership. The order Vec bounds all representable counts.
        if rounded >= isize::MAX as f32 {
            return Err(Error::Capacity);
        }
        let required = rounded as isize as usize;
        let target = if self.bucket_count < 512 && self.bucket_count * 8 >= required {
            self.bucket_count * 8
        } else {
            required
        };
        Ok(Some(
            target.checked_next_power_of_two().ok_or(Error::Capacity)?,
        ))
    }
    fn rehash(&mut self, count: usize, equal: &mut impl FnMut(&Key, &Key) -> bool) {
        let mut order = self.order.clone();
        let mut buckets: BTreeMap<usize, Vec<u64>> = BTreeMap::new();
        for id in &self.order {
            let entry = &self.entries[id];
            let bucket = buckets.entry(entry.hash & (count - 1)).or_default();
            if bucket.is_empty() {
                bucket.push(*id);
                continue;
            }
            let mut at = 0;
            for (index, previous) in bucket.iter().enumerate().rev() {
                if equal(&entry.key, &self.entries[previous].key) {
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
    /// Reserve a fresh nonzero identity externally. Builder and equality observer
    /// must not reenter or mutate this table. Equality must remain native_eq.
    /// Construction runs after initial lookup and before emplace's comparisons.
    pub fn intern_with(
        &mut self,
        identity: u64,
        key: Key,
        build: impl FnOnce(&Key) -> Result<V, Error>,
        equal: &mut impl FnMut(&Key, &Key) -> bool,
    ) -> Result<Arc<Value<V>>, Error> {
        if identity == 0 || self.entries.contains_key(&identity) {
            return Err(Error::Identity);
        }
        let hash = key.hash();
        if let Some(id) = self.find(&key, hash, equal) {
            if let Some(value) = self.entries[&id].value.upgrade() {
                return Ok(value);
            }
            self.erase(id);
        }
        let payload = build(&key)?;
        let value = Arc::new(Value {
            identity,
            key,
            payload,
        });
        // Native emplace can reject a newly constructed value. It still returns
        // that value to the caller; deletion only erases a matching raw identity.
        if self.find(&value.key, hash, equal).is_some() {
            return Ok(value);
        }
        if let Some(count) = self.growth()? {
            self.rehash(count, equal);
            if self.find(&value.key, hash, equal).is_some() {
                return Ok(value);
            }
        }
        let bucket = self
            .buckets
            .entry(hash & (self.bucket_count - 1))
            .or_default();
        let at = bucket
            .first()
            .map(|first| self.order.iter().position(|v| v == first).unwrap())
            .unwrap_or(self.order.len());
        self.order.insert(at, identity);
        bucket.insert(0, identity);
        self.entries.insert(
            identity,
            Entry {
                hash,
                key: value.key.clone(),
                value: Arc::downgrade(&value),
            },
        );
        Ok(value)
    }
    /// Call immediately before dropping each owner, under serialized complete
    /// admission. Only the last owner runs lookup; payload Drop follows it.
    pub fn release_with(
        &mut self,
        value: &Arc<Value<V>>,
        equal: &mut impl FnMut(&Key, &Key) -> bool,
    ) {
        if Arc::strong_count(value) != 1 {
            return;
        }
        if let Some(id) = self.find(&value.key, value.key.hash(), equal) {
            if id == value.identity {
                self.erase(id);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn key(size: f64) -> Key {
        Key {
            char_set: 1,
            font_name: alloc::vec![65, 0, 66],
            font_size: size,
            font_weight: 400,
            italic: false,
            underline: false,
            strikeout: false,
        }
    }
    #[test]
    fn expired_nan_keys_remain_safe_during_rehash_and_release() {
        let mut table = Table::new();
        let n = table
            .intern_with(1, key(f64::NAN), |_| Ok(1), &mut Key::native_eq)
            .unwrap();
        table.release_with(&n, &mut Key::native_eq);
        drop(n);
        assert_eq!(table.count(), 1);
        let mut held = Vec::new();
        for id in 2..=100 {
            let mut k = key(id as f64);
            k.char_set = id as i32;
            held.push(
                table
                    .intern_with(id, k, |_| Ok(id), &mut Key::native_eq)
                    .unwrap(),
            );
        }
        for v in held {
            table.release_with(&v, &mut Key::native_eq);
        }
        assert_eq!(table.count(), 1);
        assert_eq!(table.bucket_count(), 512);
    }
    #[test]
    fn first_value_shared_and_failed_construction_does_not_insert() {
        let mut table = Table::new();
        let a = table
            .intern_with(1, key(20.0), |_| Ok(7), &mut Key::native_eq)
            .unwrap();
        let mut same = key(20.0);
        same.font_name[2] = 99;
        let b = table
            .intern_with(
                2,
                same,
                |_| panic!("live key must reuse value"),
                &mut Key::native_eq,
            )
            .unwrap();
        assert!(Arc::ptr_eq(&a, &b));
        assert_eq!(b.key().font_name[2], 66);
        assert_eq!(*b.payload(), 7);
        table.release_with(&a, &mut Key::native_eq);
        drop(a);
        assert_eq!(table.count(), 1);
        table.release_with(&b, &mut Key::native_eq);
        drop(b);
        assert_eq!(table.count(), 0);
        assert!(matches!(
            table.intern_with(
                3,
                key(21.0),
                |_| Err(Error::Construction),
                &mut Key::native_eq
            ),
            Err(Error::Construction)
        ));
        assert_eq!(table.count(), 0);
    }
}
