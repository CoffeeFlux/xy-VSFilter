// SPDX-License-Identifier: GPL-2.0-or-later
//! Fixed-slot outline arc queue. Removed slots retain their contents; advancing
//! the tail is deliberately independent of the capacity-minus-one full check.
use alloc::vec::Vec;
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub struct Item {
    pub center: u64,
    pub deadline: i32,
}
#[derive(Clone)]
pub struct Queue {
    slots: Vec<Item>,
    head: usize,
    tail: usize,
}
impl Queue {
    pub fn new(capacity: usize) -> Option<Self> {
        // Native indices are signed 32-bit and inc_1_at_tail adds capacity.
        if capacity == 0 || capacity > (i32::MAX as usize) / 2 {
            return None;
        }
        let mut slots = Vec::new();
        slots.try_reserve_exact(capacity).ok()?;
        slots.resize(capacity, Item::default());
        Some(Self {
            slots,
            head: 0,
            tail: 0,
        })
    }
    pub fn capacity(&self) -> usize {
        self.slots.len()
    }
    pub fn len(&self) -> usize {
        (self.capacity() - self.head + self.tail) % self.capacity()
    }
    pub fn is_empty(&self) -> bool {
        self.head == self.tail
    }
    pub fn full(&self) -> bool {
        self.len() >= self.capacity() - 1
    }
    pub fn free(&self) -> usize {
        self.capacity() - self.len()
    }
    pub fn at(&self, index: usize) -> Item {
        self.slots[(self.head + index % self.capacity()) % self.capacity()]
    }
    pub fn back(&self) -> Item {
        self.slots[(self.capacity() + self.tail - 1) % self.capacity()]
    }
    pub fn center(&mut self, index: usize, value: u64) {
        let at = (self.head + index % self.capacity()) % self.capacity();
        self.slots[at].center = value;
    }
    pub fn deadline(&mut self, index: usize, value: i32) {
        let at = (self.head + index % self.capacity()) % self.capacity();
        self.slots[at].deadline = value;
    }
    pub fn append(&mut self, item: Item) {
        self.slots[self.tail] = item;
        self.tail = (self.tail + 1) % self.capacity();
    }
    pub fn push(&mut self, item: Item) -> bool {
        if self.full() {
            false
        } else {
            self.append(item);
            true
        }
    }
    pub fn pop_front(&mut self) {
        if !self.is_empty() {
            self.head = (self.head + 1) % self.capacity();
        }
    }
    pub fn pop_back(&mut self) {
        if !self.is_empty() {
            self.tail = (self.capacity() + self.tail - 1) % self.capacity();
        }
    }
    pub fn pop_last(&mut self, count: usize) {
        if self.len() < count {
            self.head = 0;
            self.tail = 0;
        } else {
            self.tail = (self.capacity() + self.tail - count) % self.capacity();
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn tail_wrap_retains_removed_slots_and_can_make_queue_empty() {
        let mut q = Queue::new(3).unwrap();
        for i in 0..3 {
            q.append(Item {
                center: i + 1,
                deadline: i as i32,
            });
        }
        assert!(q.is_empty());
        assert_eq!(q.free(), 3);
        assert_eq!(q.back().center, 3);
        q.pop_front();
        q.pop_back();
        assert_eq!(q.at(0).center, 1);
        q.append(Item {
            center: 4,
            deadline: 9,
        });
        q.pop_front();
        assert_eq!(q.at(2).center, 4);
        q.pop_last(1);
        assert_eq!(q.at(0).center, 4);
    }
    #[test]
    fn failed_push_keeps_full_queue_and_copy_is_independent() {
        let mut q = Queue::new(2).unwrap();
        q.append(Item {
            center: u64::MAX,
            deadline: i32::MIN,
        });
        assert!(!q.push(Item::default()));
        assert!(q.full());
        assert_eq!(q.free(), 1);
        let mut copy = q.clone();
        copy.center(0, 42);
        copy.deadline(0, i32::MAX);
        assert_eq!(
            q.at(0),
            Item {
                center: u64::MAX,
                deadline: i32::MIN
            }
        );
        assert_eq!(
            copy.at(0),
            Item {
                center: 42,
                deadline: i32::MAX
            }
        );
        assert!(Queue::new(0).is_none());
        assert!(Queue::new(usize::MAX).is_none());
    }
}
