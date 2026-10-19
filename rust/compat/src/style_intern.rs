// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned style interning with the cleanup's MSVC unique-table traversal.
//! This is the ownership engine, not yet the renderer's native adapter.
//! Comparison callbacks make traversal observable without exposing pointers.
//! They must return the same deterministic value equality as the native key;
//! observers may record calls but must not change comparison policy mid-operation.
//! Native floating flags/traps and allocation failures need separate validation.
use crate::{document_style::StyleValue, rect_intern};
use alloc::collections::BTreeMap;
use alloc::sync::{Arc, Weak};
use alloc::vec::Vec;

pub struct Value {
    identity: u64,
    style: StyleValue,
    rectangle: Arc<rect_intern::Value>,
}
impl Value {
    pub fn identity(&self) -> u64 {
        self.identity
    }
    pub fn style(&self) -> &StyleValue {
        &self.style
    }
}
struct Entry {
    hash: usize,
    value: Weak<Value>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    /// A native lookup/rehash would read an expired raw key. Never dereference
    /// or silently replace it; this reference history is unresolved.
    ExpiredKey,
    Identity,
    Capacity,
}
pub struct Table {
    entries: BTreeMap<u64, Entry>,
    order: Vec<u64>,
    buckets: BTreeMap<usize, Vec<u64>>,
    bucket_count: usize,
}
impl Default for Table {
    fn default() -> Self {
        Self::new()
    }
}
impl Table {
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
        key: &StyleValue,
        hash: usize,
        equal: &mut impl FnMut(&StyleValue, &StyleValue) -> bool,
    ) -> Result<Option<Arc<Value>>, Error> {
        if let Some(bucket) = self.buckets.get(&(hash & (self.bucket_count - 1))) {
            for id in bucket.iter().rev() {
                let v = self.entries[id].value.upgrade().ok_or(Error::ExpiredKey)?;
                if equal(key, &v.style) {
                    return Ok(Some(v));
                }
            }
        }
        Ok(None)
    }
    fn growth(&self) -> Result<Option<usize>, Error> {
        let size = self.count().checked_add(1).ok_or(Error::Capacity)?;
        // MSVC max_load_factor defaults to 1.0f. Preserve its f32 size rounding.
        if (size as f32) / (self.bucket_count as f32) <= 1.0 {
            return Ok(None);
        }
        let rounded = size as f32;
        // LLVM's direct f32 -> usize lowering evaluates an unused subtraction
        // of 2^(usize::BITS-1), setting inexact even for small exact counts such
        // as 9 and 65. Valid node counts fit our order Vec's signed allocation
        // bound. Convert through the signed type to avoid that extra operation;
        // never clear ambient floating status to hide the discrepancy.
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
    fn rehash(
        &mut self,
        count: usize,
        equal: &mut impl FnMut(&StyleValue, &StyleValue) -> bool,
    ) -> Result<(), Error> {
        // Stage the rebuilt table: an unresolved expired key cannot partially
        // corrupt our safe bookkeeping. Callback observations are not rolled back.
        let mut order = self.order.clone();
        let mut buckets: BTreeMap<usize, Vec<u64>> = BTreeMap::new();
        for id in &self.order {
            let entry = &self.entries[id];
            let value = entry.value.upgrade().ok_or(Error::ExpiredKey)?;
            let bucket = buckets.entry(entry.hash & (count - 1)).or_default();
            if bucket.is_empty() {
                bucket.push(*id);
                continue;
            }
            let mut at = 0;
            for (index, previous) in bucket.iter().enumerate().rev() {
                let other = self.entries[previous]
                    .value
                    .upgrade()
                    .ok_or(Error::ExpiredKey)?;
                if equal(&value.style, &other.style) {
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
        Ok(())
    }
    /// Identity must be freshly reserved by the enclosing checked registry.
    /// The rectangle table is shared with all other canonical rectangle owners.
    pub fn intern(
        &mut self,
        identity: u64,
        style: StyleValue,
        rectangles: &mut rect_intern::Table,
    ) -> Result<Arc<Value>, Error> {
        self.intern_with(
            identity,
            style,
            rectangles,
            &mut StyleValue::native_compatible_eq,
        )
    }
    pub fn intern_with(
        &mut self,
        identity: u64,
        style: StyleValue,
        rectangles: &mut rect_intern::Table,
        equal: &mut impl FnMut(&StyleValue, &StyleValue) -> bool,
    ) -> Result<Arc<Value>, Error> {
        if identity == 0 || self.entries.contains_key(&identity) {
            return Err(Error::Identity);
        }
        let hash = hash(&style);
        // XyInterned::Intern's lookup, then unordered_map::emplace's lookup.
        if let Some(value) = self.find(&style, hash, equal)? {
            return Ok(value);
        }
        if let Some(value) = self.find(&style, hash, equal)? {
            return Ok(value);
        }
        if let Some(count) = self.growth()? {
            self.rehash(count, equal)?;
            // MSVC searches again after rebuilding buckets.
            if let Some(value) = self.find(&style, hash, equal)? {
                return Ok(value);
            }
        }
        let rectangle = rectangles.intern(identity, style.margins);
        let value = Arc::new(Value {
            identity,
            style,
            rectangle,
        });
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
                value: Arc::downgrade(&value),
            },
        );
        Ok(value)
    }
    /// Call immediately before dropping an owner, under complete serialized
    /// admission. The table holds Weak references, so a count of one is final.
    /// A non-reflexive key remains a counted expired entry after its final drop,
    /// matching the observed native count without retaining a dangling pointer.
    pub fn release(
        &mut self,
        value: &Arc<Value>,
        rectangles: &mut rect_intern::Table,
    ) -> Result<(), Error> {
        self.release_with(value, rectangles, &mut StyleValue::native_compatible_eq)
    }
    pub fn release_with(
        &mut self,
        value: &Arc<Value>,
        rectangles: &mut rect_intern::Table,
        equal: &mut impl FnMut(&StyleValue, &StyleValue) -> bool,
    ) -> Result<(), Error> {
        if Arc::strong_count(value) != 1 {
            return Ok(());
        }
        let hash = hash(&value.style);
        let result = self.find(&value.style, hash, equal).map(|found| {
            if found.as_ref().is_some_and(|v| Arc::ptr_eq(v, value)) {
                self.entries.remove(&value.identity);
                self.order.retain(|id| *id != value.identity);
                let key = hash & (self.bucket_count - 1);
                let bucket = self.buckets.get_mut(&key).unwrap();
                bucket.retain(|id| *id != value.identity);
                if bucket.is_empty() {
                    self.buckets.remove(&key);
                }
            }
        });
        // Release the canonical rectangle even if reference lookup is unresolved.
        // The removed style owner is still alive until its caller drops it.
        rectangles.release(&value.rectangle);
        result
    }
}
fn mix(hash: usize, value: usize) -> usize {
    hash.wrapping_mul(33).wrapping_add(value)
}
fn double_hash(value: f64) -> usize {
    let bits = value.to_bits();
    mix(
        mix(515, bits as u32 as i32 as usize),
        (bits >> 32) as u32 as i32 as usize,
    )
}
/// Actual cleanup hash, including signed double halves and target size_t width.
/// A full-hash inequality is NOT a reason to skip native key comparison.
pub fn hash(style: &StyleValue) -> usize {
    let name = style
        .font_name
        .iter()
        .take_while(|v| **v != 0)
        .fold(0u32, |h, v| h.wrapping_mul(33).wrapping_add(*v as u32));
    let mut h = mix(name as usize, style.char_set as usize);
    h = mix(h, double_hash(style.font_size));
    h = mix(h, style.font_weight as usize);
    for v in [style.italic, style.underline, style.strikeout] {
        h = mix(h, v as usize);
    }
    for v in [style.colors[0], style.colors[2]] {
        h = mix(h, v as usize);
    }
    for v in [
        style.font_scale_x,
        style.font_scale_y,
        style.font_angle_x,
        style.font_angle_y,
        style.font_angle_z,
        style.font_shift_x,
        style.font_shift_y,
    ] {
        h = mix(h, double_hash(v));
    }
    h
}

#[cfg(test)]
mod tests {
    use super::*;
    fn style() -> StyleValue {
        crate::ass_style_value::Builder::default().value
    }
    #[test]
    fn zero_alias_keeps_first_raw_value_and_rectangle_outlives_other_owners() {
        let mut table = Table::new();
        let mut rectangles = rect_intern::Table::new();
        let rect = rectangles.intern(1, [20; 4]);
        let mut first = style();
        first.font_size = -0.0;
        first.font_name = alloc::vec![65, 0, 0xd800];
        let mut second = first.clone();
        second.font_size = 0.0;
        second.font_name = alloc::vec![65];
        assert_ne!(hash(&first), hash(&second));
        let a = table.intern(2, first, &mut rectangles).unwrap();
        let b = table.intern(3, second, &mut rectangles).unwrap();
        assert!(Arc::ptr_eq(&a, &b));
        assert_eq!(b.style.font_size.to_bits(), (-0.0f64).to_bits());
        assert_eq!(b.style.font_name, [65, 0, 0xd800]);
        rectangles.release(&rect);
        drop(rect);
        table.release(&a, &mut rectangles).unwrap();
        drop(a);
        assert_eq!((table.count(), rectangles.count()), (1, 1));
        table.release(&b, &mut rectangles).unwrap();
        drop(b);
        assert_eq!((table.count(), rectangles.count()), (0, 0));
    }
    #[test]
    fn nonreflexive_release_keeps_count_but_expired_lookup_is_refused() {
        let mut table = Table::new();
        let mut rectangles = rect_intern::Table::new();
        let mut value = style();
        value.font_size = f64::from_bits(0x7ff8000000000001);
        let a = table.intern(1, value.clone(), &mut rectangles).unwrap();
        table.release(&a, &mut rectangles).unwrap();
        drop(a);
        assert_eq!((table.count(), rectangles.count()), (1, 0));
        assert!(matches!(
            table.intern(2, value, &mut rectangles),
            Err(Error::ExpiredKey)
        ));
        assert_eq!((table.count(), rectangles.count()), (1, 0));
    }
    #[test]
    fn only_final_alias_release_invokes_lookup_and_drops_rectangle() {
        let mut table = Table::new();
        let mut rectangles = rect_intern::Table::new();
        let a = table.intern(1, style(), &mut rectangles).unwrap();
        let b = a.clone();
        let mut calls = 0;
        let mut eq = |a: &StyleValue, b: &StyleValue| {
            calls += 1;
            a.native_compatible_eq(b)
        };
        table.release_with(&a, &mut rectangles, &mut eq).unwrap();
        drop(a);
        table.release_with(&b, &mut rectangles, &mut eq).unwrap();
        drop(b);
        assert_eq!(calls, 1);
        assert_eq!((table.count(), rectangles.count()), (0, 0));
    }
}
