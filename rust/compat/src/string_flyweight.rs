// SPDX-License-Identifier: GPL-2.0-or-later
//! Cleanup XyFwStringW ownership and MRU policy. Keys stop at the first NUL,
//! but an entry retains the first inserter's complete stored UTF-16 contents.
use alloc::collections::{BTreeMap, VecDeque};
use alloc::sync::Arc;
use alloc::vec::Vec;

#[derive(Debug)]
pub struct Value {
    id: usize,
    units: Vec<u16>,
}
impl Value {
    pub fn id(&self) -> usize {
        self.id
    }
    pub fn units(&self) -> &[u16] {
        &self.units
    }
}
fn key(units: &[u16]) -> &[u16] {
    &units[..units.iter().position(|u| *u == 0).unwrap_or(units.len())]
}

pub struct Cache {
    values: BTreeMap<Vec<u16>, Arc<Value>>,
    // Front is most recently used. Arc ownership survives eviction in callers.
    order: VecDeque<Arc<Value>>,
    maximum: usize,
    next: usize,
    hits: usize,
    queries: usize,
}
#[derive(Debug, PartialEq, Eq)]
pub struct Disabled;
impl Default for Cache {
    fn default() -> Self {
        Self::new()
    }
}
impl Cache {
    pub const fn new() -> Self {
        Self {
            values: BTreeMap::new(),
            order: VecDeque::new(),
            maximum: 16384,
            next: 0,
            hits: 0,
            queries: 0,
        }
    }
    pub fn intern(&mut self, units: Vec<u16>) -> Result<Arc<Value>, Disabled> {
        // Native construction at capacity zero dereferences a removed list node.
        // Refuse this unresolved outcome; do not emulate dangling storage.
        if self.maximum == 0 {
            return Err(Disabled);
        }
        self.queries = self.queries.wrapping_add(1);
        if let Some(value) = self.values.get(key(&units)).cloned() {
            self.hits = self.hits.wrapping_add(1);
            let index = self
                .order
                .iter()
                .position(|v| Arc::ptr_eq(v, &value))
                .expect("cached entry has MRU node");
            self.order.remove(index);
            self.order.push_front(value.clone());
            return Ok(value);
        }
        self.next = self.next.wrapping_add(1);
        if self.next == 0 {
            self.next = 1;
        }
        let value = Arc::new(Value {
            id: self.next,
            units,
        });
        self.values
            .insert(key(value.units()).to_vec(), value.clone());
        self.order.push_front(value.clone());
        if self.order.len() > self.maximum {
            self.remove_tail();
        }
        Ok(value)
    }
    pub fn remove_tail(&mut self) {
        if let Some(value) = self.order.pop_back() {
            self.values.remove(key(value.units()));
        }
    }
    /// Module teardown releases cached allocations while retained owners remain
    /// valid. New construction after native cache destruction is unresolved,
    /// refused safely by maximum zero.
    pub fn shutdown(&mut self) {
        self.values.clear();
        self.order = VecDeque::new();
        self.maximum = 0;
    }
    pub fn clear(&mut self, reset: bool) {
        self.values.clear();
        self.order.clear();
        self.reset(reset);
    }
    fn reset(&mut self, reset: bool) {
        if reset {
            self.hits = 0;
            self.queries = 0;
        }
    }
    pub fn set_maximum(&mut self, maximum: usize, reset: bool) {
        self.reset(reset);
        self.maximum = maximum;
        while self.order.len() > maximum {
            self.remove_tail();
        }
    }
    /// Native size_t counters and IDs wrap at the target pointer width.
    pub fn stats(&self) -> [usize; 4] {
        [self.maximum, self.order.len(), self.hits, self.queries]
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;
    #[test]
    fn first_nul_identity_preserves_first_full_value() {
        let mut cache = Cache::new();
        let first = cache.intern(vec![65, 0, 0xd800]).unwrap();
        let short = cache.intern(vec![65]).unwrap();
        let other = cache.intern(vec![65, 0, 66]).unwrap();
        assert!(Arc::ptr_eq(&first, &short) && Arc::ptr_eq(&first, &other));
        assert_eq!(short.units(), &[65, 0, 0xd800]);
        assert_eq!(cache.stats(), [16384, 1, 2, 3]);
        let empty = cache.intern(vec![0, 1]).unwrap();
        assert_eq!(cache.intern(vec![]).unwrap().units(), empty.units());
    }
    #[test]
    fn eviction_and_reinsertion_do_not_invalidate_retained_owners() {
        let mut cache = Cache::new();
        cache.set_maximum(2, false);
        let a = cache.intern(vec![1]).unwrap();
        let b = cache.intern(vec![2]).unwrap();
        assert_eq!(cache.intern(vec![1]).unwrap().id(), a.id());
        cache.intern(vec![3]).unwrap();
        assert_ne!(cache.intern(vec![2]).unwrap().id(), b.id());
        cache.clear(false);
        assert_eq!(a.units(), &[1]);
        assert_eq!(b.units(), &[2]);
        assert_eq!(cache.stats(), [2, 0, 1, 5]);
        assert!(cache.intern(vec![1]).unwrap().id() > b.id());
        cache.set_maximum(0, true);
        assert_eq!(cache.stats(), [0, 0, 0, 0]);
        assert_eq!(cache.intern(vec![]).unwrap_err(), Disabled);
    }
    #[test]
    fn shutdown_releases_cache_ownership_but_retained_text_is_live() {
        let mut cache = Cache::new();
        let value = cache.intern(vec![65, 0, 0xd800]).unwrap();
        assert_eq!(Arc::strong_count(&value), 3);
        cache.shutdown();
        assert_eq!(Arc::strong_count(&value), 1);
        assert_eq!(value.units(), &[65, 0, 0xd800]);
        assert_eq!(cache.stats()[..2], [0, 0]);
        assert_eq!(cache.intern(vec![65]).unwrap_err(), Disabled);
    }
    #[test]
    fn counters_and_legacy_ids_wrap_but_never_allocate_zero() {
        let mut cache = Cache::new();
        cache.next = usize::MAX;
        cache.queries = usize::MAX;
        let value = cache.intern(vec![0xffff]).unwrap();
        assert_eq!(value.id(), 1);
        cache.hits = usize::MAX;
        cache.intern(vec![0xffff]).unwrap();
        assert_eq!(cache.stats(), [16384, 1, 0, 1]);
        cache.clear(true);
        assert_eq!(cache.intern(vec![0xdfff]).unwrap().id(), 2);
    }
}
