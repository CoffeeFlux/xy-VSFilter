// SPDX-License-Identifier: GPL-2.0-or-later
//! Canonical rectangle values used by style ownership. Equality is four raw
//! Windows LONG coordinates; an interned value exists while any owner retains it.
use alloc::collections::BTreeMap;
use alloc::sync::{Arc, Weak};

#[derive(Debug)]
pub struct Value {
    identity: u64,
    coordinates: [i32; 4],
}
impl Value {
    pub fn identity(&self) -> u64 {
        self.identity
    }
    pub fn coordinates(&self) -> [i32; 4] {
        self.coordinates
    }
}
pub struct Table {
    values: BTreeMap<[i32; 4], Weak<Value>>,
}
impl Default for Table {
    fn default() -> Self {
        Self::new()
    }
}
impl Table {
    pub const fn new() -> Self {
        Self {
            values: BTreeMap::new(),
        }
    }
    /// A checked registry reserves a fresh nonzero identity before insertion.
    /// A hit keeps the existing canonical identity and creates another owner.
    pub fn intern(&mut self, identity: u64, coordinates: [i32; 4]) -> Arc<Value> {
        // Nested style owners can drop outside registry admission. Weak entries
        // never keep a canonical payload alive; reclaim expired bookkeeping on
        // insertion so repeated distinct histories do not accumulate entries.
        self.values.retain(|_, value| value.strong_count() != 0);
        if let Some(value) = self.lookup(coordinates) {
            return value;
        }
        let value = Arc::new(Value {
            identity,
            coordinates,
        });
        self.values.insert(coordinates, Arc::downgrade(&value));
        value
    }
    pub fn lookup(&self, coordinates: [i32; 4]) -> Option<Arc<Value>> {
        self.values.get(&coordinates).and_then(Weak::upgrade)
    }
    /// Called under exclusive registry admission immediately before an owner's
    /// removal. Explicit rectangle/style resources can retire their entry before
    /// dropping outside admission. Nested owners need no callback: their final
    /// Arc drop expires the weak entry. Remove bookkeeping for the last owner. The removed
    /// owner remains alive until destruction outside registry admission. Complete
    /// release/drop operations must be serialized, not only this method's call.
    pub fn release(&mut self, value: &Arc<Value>) {
        if Arc::strong_count(value) == 1
            && self
                .values
                .get(&value.coordinates)
                .is_some_and(|v| Weak::ptr_eq(v, &Arc::downgrade(value)))
        {
            self.values.remove(&value.coordinates);
        }
    }
    pub fn count(&self) -> usize {
        self.values
            .values()
            .filter(|value| value.strong_count() != 0)
            .count()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn nested_owners_expire_without_callbacks_and_reintern_with_fresh_identity() {
        let mut table = Table::new();
        let first = table.intern(1, [1, 2, 3, 4]);
        let alias = first.clone();
        drop(first);
        assert_eq!(table.count(), 1);
        assert!(Arc::ptr_eq(&alias, &table.lookup([1, 2, 3, 4]).unwrap()));
        drop(alias);
        assert_eq!(table.count(), 0);
        assert!(table.lookup([1, 2, 3, 4]).is_none());
        let fresh = table.intern(2, [1, 2, 3, 4]);
        assert_eq!(fresh.identity(), 2);
        drop(fresh);
        for i in 3..1000 {
            drop(table.intern(i, [i as i32; 4]));
            assert_eq!(table.count(), 0);
            assert_eq!(table.values.len(), 1); // Prior expired metadata reclaimed.
        }
    }
    #[test]
    fn aliases_survive_independent_owner_release_and_reintern() {
        let mut table = Table::new();
        let first = table.intern(1, [i32::MIN, -1, i32::MAX, 0]);
        let alias = table.intern(2, first.coordinates());
        assert!(Arc::ptr_eq(&first, &alias));
        assert_eq!(alias.identity(), 1);
        assert_eq!(table.count(), 1);
        table.release(&first);
        drop(first);
        assert_eq!(table.count(), 1);
        let snapshot = alias.coordinates();
        table.release(&alias);
        assert_eq!(table.count(), 0);
        assert_eq!(alias.coordinates(), snapshot);
        drop(alias);
        assert_eq!(table.intern(3, snapshot).identity(), 3);
    }
    #[test]
    fn default_and_distinct_raw_coordinates_are_not_normalized() {
        let mut table = Table::new();
        let a = table.intern(1, [0; 4]);
        let b = table.intern(2, [1, 2, -3, -4]);
        let c = table.intern(3, [-3, -4, 1, 2]);
        assert_eq!(table.count(), 3);
        assert_ne!(b.identity(), c.identity());
        assert_eq!(b.coordinates(), [1, 2, -3, -4]);
        for value in [a, b, c] {
            table.release(&value);
            drop(value);
        }
        assert_eq!(table.count(), 0);
    }
    #[test]
    fn release_does_not_remove_another_tables_equal_key() {
        let mut a = Table::new();
        let mut b = Table::new();
        let first = a.intern(1, [0; 4]);
        let second = b.intern(1, [0; 4]);
        a.release(&second);
        assert_eq!(a.count(), 1);
        a.release(&first);
        b.release(&second);
        assert_eq!((a.count(), b.count()), (0, 0));
    }
}
