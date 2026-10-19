// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned named-style table matching the selected Windows ATL default map.
//!
//! Authority: successful x86/x64 captures in ci-38641065757, including inspected
//! selected SDK definitions. This is an independent safe implementation, not SDK
//! source. Keys use LPCWSTR argument semantics: comparison, hashing AND initial
//! stored-key construction stop at NUL. Native allocation failures, node/pointer
//! identities, custom load settings and locked/reentrant mutation are outside
//! this successful-allocation model. The experimental document owns this table;
//! native AddStyle still orchestrates equality checks and conflict renaming.
use alloc::vec::Vec;

const PRIMES: &[u32] = &[
    17,
    23,
    29,
    37,
    41,
    53,
    67,
    83,
    103,
    131,
    163,
    211,
    257,
    331,
    409,
    521,
    647,
    821,
    1031,
    1291,
    1627,
    2053,
    2591,
    3251,
    4099,
    5167,
    6521,
    8209,
    10331,
    13007,
    16411,
    20663,
    26017,
    32771,
    41299,
    52021,
    65537,
    82571,
    104033,
    131101,
    165161,
    208067,
    262147,
    330287,
    416147,
    524309,
    660563,
    832291,
    1048583,
    1321139,
    1664543,
    2097169,
    2642257,
    3329023,
    4194319,
    5284493,
    6658049,
    8388617,
    10568993,
    13316089,
    u32::MAX,
];

fn prefix(key: &[u16]) -> &[u16] {
    &key[..key.iter().position(|&unit| unit == 0).unwrap_or(key.len())]
}

/// Windows ULONG hash width is 32 on both validated architectures.
pub fn string_hash(key: &[u16]) -> u32 {
    prefix(key).iter().fold(0u32, |hash, &unit| {
        hash.wrapping_mul(33).wrapping_add(u32::from(unit))
    })
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct Entry<V> {
    key: Vec<u16>,
    hash: u32,
    value: V,
}

/// Target arithmetic supplied by the boundary. Callbacks return (high, low)
/// thresholds and the size estimate before prime selection, respectively.
/// A static service keeps cloning storage free of floating operations.
#[derive(Debug)]
pub struct Arithmetic {
    thresholds: fn(u32) -> (usize, usize),
    estimate: fn(usize) -> usize,
}
impl Arithmetic {
    pub const fn new(thresholds: fn(u32) -> (usize, usize), estimate: fn(usize) -> usize) -> Self {
        Self {
            thresholds,
            estimate,
        }
    }
}
impl PartialEq for Arithmetic {
    fn eq(&self, other: &Self) -> bool {
        // Compare static service identity, never compiler-dependent function
        // addresses. Services are borrowed, not copied into the map.
        core::ptr::eq(self, other)
    }
}
impl Eq for Arithmetic {}
static PORTABLE_ARITHMETIC: Arithmetic = Arithmetic::new(
    |bins| {
        (
            (2.25f32 * bins as f32) as usize,
            (0.25f32 * bins as f32) as usize,
        )
    },
    |count| (count as f32 / 0.75f32) as usize,
);

/// Native default bins/load thresholds, with ascending-bin, head-first order.
/// Values are owned; replacing/removing/clearing returns retired values so an
/// adapter can destroy them after leaving its admission gate.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StyleMap<V> {
    buckets: Vec<Vec<Entry<V>>>,
    count: usize,
    high: usize,
    low: usize,
    initialized: bool,
    arithmetic: &'static Arithmetic,
}

impl<V> Default for StyleMap<V> {
    fn default() -> Self {
        Self::new()
    }
}

impl<V> StyleMap<V> {
    pub fn new() -> Self {
        Self {
            buckets: empty_buckets(17),
            count: 0,
            high: 38,
            low: 0,
            initialized: false,
            arithmetic: &PORTABLE_ARITHMETIC,
        }
    }
    /// Construct a host map, including observable native constructor arithmetic.
    /// Portable defaults used for scratch storage do not run host callbacks.
    pub fn with_arithmetic(arithmetic: &'static Arithmetic) -> Self {
        let mut map = Self {
            arithmetic,
            ..Self::new()
        };
        map.update_thresholds();
        map
    }
    fn update_thresholds(&mut self) {
        let (high, low) = (self.arithmetic.thresholds)(self.bin_count());
        self.high = high;
        self.low = if low < 17 { 0 } else { low };
    }
    fn pick_size(&self) -> u32 {
        prime_for_estimate((self.arithmetic.estimate)(self.count))
    }
    pub fn count(&self) -> usize {
        self.count
    }
    pub fn bin_count(&self) -> u32 {
        self.buckets.len() as u32
    }
    fn bucket(&self, key: &[u16]) -> usize {
        (string_hash(key) % self.bin_count()) as usize
    }
    pub fn get(&self, key: &[u16]) -> Option<&V> {
        self.buckets[self.bucket(key)]
            .iter()
            .find(|entry| entry.key == prefix(key))
            .map(|entry| &entry.value)
    }
    pub fn get_mut(&mut self, key: &[u16]) -> Option<&mut V> {
        let bucket = self.bucket(key);
        self.buckets[bucket]
            .iter_mut()
            .find(|entry| entry.key == prefix(key))
            .map(|entry| &mut entry.value)
    }
    pub fn insert(&mut self, mut key: Vec<u16>, value: V) -> Option<V> {
        let bucket = self.bucket(&key);
        if let Some(entry) = self.buckets[bucket]
            .iter_mut()
            .find(|entry| entry.key == prefix(&key))
        {
            return Some(core::mem::replace(&mut entry.value, value));
        }
        if !self.initialized {
            self.update_thresholds();
            self.initialized = true;
        }
        key.truncate(prefix(&key).len());
        let hash = string_hash(&key);
        self.buckets[bucket].insert(0, Entry { key, hash, value });
        self.count += 1;
        if self.count > self.high {
            self.rehash(self.pick_size());
        }
        None
    }
    pub fn remove(&mut self, key: &[u16]) -> Option<V> {
        let bucket = self.bucket(key);
        let position = self.buckets[bucket]
            .iter()
            .position(|entry| entry.key == prefix(key))?;
        let entry = self.buckets[bucket].remove(position);
        self.count -= 1;
        if self.count < self.low {
            self.rehash(self.pick_size());
        }
        Some(entry.value)
    }
    /// Retains bin count, matching native RemoveAll's internal rehash lock.
    /// Retired values follow the prior enumeration order.
    pub fn clear(&mut self) -> Vec<V> {
        let mut retired = Vec::with_capacity(self.count);
        for bucket in &mut self.buckets {
            retired.extend(bucket.drain(..).map(|entry| entry.value));
        }
        self.count = 0;
        self.initialized = false;
        retired
    }
    pub fn iter(&self) -> impl Iterator<Item = (&[u16], &V)> {
        self.buckets.iter().flat_map(|bucket| {
            bucket
                .iter()
                .map(|entry| (entry.key.as_slice(), &entry.value))
        })
    }
    fn rehash(&mut self, bins: u32) {
        if bins == self.bin_count() {
            return;
        }
        let mut replacement = empty_buckets(bins);
        // Traverse previous bins/chains in enumeration order, prepend each node.
        for bucket in &mut self.buckets {
            for entry in bucket.drain(..) {
                replacement[(entry.hash % bins) as usize].insert(0, entry);
            }
        }
        self.buckets = replacement;
        self.update_thresholds();
    }
}

fn empty_buckets<V>(bins: u32) -> Vec<Vec<Entry<V>>> {
    (0..bins).map(|_| Vec::new()).collect()
}
fn prime_for_estimate(estimate: usize) -> u32 {
    let estimate = estimate.min(u32::MAX as usize) as u32;
    match PRIMES.iter().copied().find(|&prime| prime >= estimate) {
        Some(prime) if prime != u32::MAX => prime,
        _ => estimate,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;
    fn key(n: u16) -> Vec<u16> {
        vec![n]
    }
    fn pick_size(count: usize) -> u32 {
        prime_for_estimate((PORTABLE_ARITHMETIC.estimate)(count))
    }
    #[test]
    fn arithmetic_runs_at_native_lifecycle_boundaries() {
        use core::sync::atomic::{AtomicUsize, Ordering::SeqCst};
        static THRESHOLDS: AtomicUsize = AtomicUsize::new(0);
        static ESTIMATES: AtomicUsize = AtomicUsize::new(0);
        static OBSERVED: Arithmetic = Arithmetic::new(
            |bins| {
                THRESHOLDS.fetch_add(1, SeqCst);
                (PORTABLE_ARITHMETIC.thresholds)(bins)
            },
            |count| {
                ESTIMATES.fetch_add(1, SeqCst);
                (PORTABLE_ARITHMETIC.estimate)(count)
            },
        );
        let mut map = StyleMap::with_arithmetic(&OBSERVED);
        assert_eq!(THRESHOLDS.load(SeqCst), 1);
        for n in 1..=128 {
            map.insert(key(n), n);
        }
        assert_eq!(THRESHOLDS.load(SeqCst), 4); // first allocation, two growths
        assert_eq!(ESTIMATES.load(SeqCst), 2);
        for n in 1..=128 {
            assert_eq!(map.insert(key(n), n), Some(n));
        }
        let copy = map.clone();
        assert_eq!(map, copy);
        map.clear();
        assert_eq!(THRESHOLDS.load(SeqCst), 4);
        map.insert(key(7), 7);
        assert_eq!(THRESHOLDS.load(SeqCst), 5);
        map.remove(&[7]); // shrinks 163 -> 17, keeps logical bins allocated
        assert_eq!(THRESHOLDS.load(SeqCst), 6);
        assert_eq!(ESTIMATES.load(SeqCst), 3);
        map.insert(key(8), 8);
        map.remove(&[8]);
        map.insert(key(9), 9);
        assert_eq!(THRESHOLDS.load(SeqCst), 6);
    }
    #[test]
    fn raw_keys_and_retired_values() {
        let mut map = StyleMap::new();
        assert_eq!(map.insert(vec![65, 0, 0xd800], 1), None);
        assert_eq!(map.insert(vec![65, 0, 66], 2), Some(1));
        assert_eq!(map.iter().next(), Some((&[65][..], &2)));
        *map.get_mut(&[65, 0, 99]).unwrap() = 3;
        assert_eq!(map.remove(&[65]), Some(3));
        map.insert(vec![0, 88], 4);
        assert_eq!(map.get(&[]), Some(&4));
        map.insert(vec![0xd800], 5);
        map.insert(vec![0xdc00], 6);
        assert_eq!(map.count(), 3);
        assert_ne!(string_hash(&[0xd800]), string_hash(&[0xdc00]));
    }
    #[test]
    fn prepend_collision_and_ordered_reinsert() {
        let mut map = StyleMap::new();
        map.insert(key(1), 1);
        map.insert(key(18), 18);
        map.insert(key(35), 35);
        assert_eq!(
            map.iter().map(|(_, v)| *v).collect::<Vec<_>>(),
            vec![35, 18, 1]
        );
        let mut copy = StyleMap::new();
        for (k, v) in map.iter() {
            copy.insert(k.to_vec(), *v);
        }
        assert_eq!(
            copy.iter().map(|(_, v)| *v).collect::<Vec<_>>(),
            vec![1, 18, 35]
        );
        assert_eq!(map.remove(&[18]), Some(18));
        map.insert(key(18), 18);
        assert_eq!(
            map.iter().map(|(_, v)| *v).collect::<Vec<_>>(),
            vec![18, 35, 1]
        );
    }
    #[test]
    fn growth_shrink_clear_reuse() {
        let mut map = StyleMap::new();
        for n in 1..=128 {
            map.insert(key(n), n);
        }
        assert_eq!(map.bin_count(), 163);
        let retired = map.clear();
        assert_eq!(retired.len(), 128);
        assert_eq!(map.bin_count(), 163);
        map.insert(key(7), 7);
        assert_eq!(map.bin_count(), 163);
        // Delete triggers shrink even after clear/reuse; RemoveAll itself does not.
        assert_eq!(map.remove(&[7]), Some(7));
        assert_eq!(map.bin_count(), 17);
        for n in 1..=128 {
            map.insert(key(n), n);
        }
        for n in 1..=108 {
            map.remove(&[n]);
        }
        assert_eq!(map.count(), 20);
        assert_eq!(map.bin_count(), 53);
    }
    #[test]
    fn largest_prime_sentinel_returns_estimate_without_allocating() {
        // count / 0.75 rounds in f32, just as the native estimate does.
        let count = 10_000_000usize;
        let estimate = (count as f32 / 0.75f32) as u32;
        assert!(estimate > 13_316_089);
        assert_eq!(pick_size(count), estimate);
        assert_ne!(pick_size(count), u32::MAX);
        assert_eq!(pick_size(usize::MAX), u32::MAX);
    }

    #[test]
    fn exact_thresholds_and_hash_wrap() {
        let mut map = StyleMap::new();
        for n in 1..=38 {
            map.insert(key(n), n);
        }
        assert_eq!(map.bin_count(), 17);
        map.insert(key(39), 39);
        assert_eq!(map.bin_count(), 53);
        assert_eq!(
            string_hash(&[0xffffu16; 20]),
            [0xffffu16; 20]
                .iter()
                .fold(0u32, |h, &u| h.wrapping_mul(33).wrapping_add(u32::from(u)))
        );
    }
}
