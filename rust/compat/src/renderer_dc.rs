// SPDX-License-Identifier: GPL-2.0-or-later
//! Shared renderer context lifetime, independent of Windows handle layout.
//! A null native context is still an owned value. Complete operations acquire a
//! checked token; ownership cannot change until that operation ends. Neither a
//! resource reference nor a mutable state loan crosses the native boundary.
use alloc::vec::Vec;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    InvalidHandle,
    Busy,
    Exhausted,
}

pub struct State<T> {
    next: Option<u64>,
    owners: Vec<u64>,
    value: Option<T>,
    operation: Option<u64>,
}
impl<T> Default for State<T> {
    fn default() -> Self {
        Self::new()
    }
}
impl<T> State<T> {
    pub const fn new() -> Self {
        Self {
            next: Some(1),
            owners: Vec::new(),
            value: None,
            operation: None,
        }
    }
    pub fn acquire(&mut self, create: impl FnOnce() -> T) -> Result<u64, Error> {
        if self.operation.is_some() {
            return Err(Error::Busy);
        }
        let id = self.next.ok_or(Error::Exhausted)?;
        // Prepare fallible bookkeeping before native creation/setup calls.
        self.owners.try_reserve(1).map_err(|_| Error::Exhausted)?;
        if self.value.is_none() {
            self.value = Some(create());
        }
        self.owners.push(id);
        self.next = id.checked_add(1);
        Ok(id)
    }
    /// Return the final native owner for destruction at the caller's explicit
    /// release point, while its admission/operation serialization still holds.
    pub fn release(&mut self, id: u64) -> Result<Option<T>, Error> {
        let index = self
            .owners
            .iter()
            .position(|v| *v == id)
            .ok_or(Error::InvalidHandle)?;
        if self.operation.is_some() {
            return Err(Error::Busy);
        }
        self.owners.swap_remove(index);
        Ok(if self.owners.is_empty() {
            self.value.take()
        } else {
            None
        })
    }
    /// The boundary copies a native handle scalar from this reference, never a
    /// pointer into Rust. The resource remains owned until matching end().
    pub fn begin(&mut self) -> Result<(u64, &T), Error> {
        if self.operation.is_some() {
            return Err(Error::Busy);
        }
        let value = self.value.as_ref().ok_or(Error::InvalidHandle)?;
        let id = self.next.ok_or(Error::Exhausted)?;
        self.next = id.checked_add(1);
        self.operation = Some(id);
        Ok((id, value))
    }
    pub fn end(&mut self, token: u64) -> Result<(), Error> {
        if self.operation != Some(token) {
            return Err(Error::InvalidHandle);
        }
        self.operation = None;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::rc::Rc;
    use core::cell::RefCell;

    struct Resource(Rc<RefCell<Vec<u32>>>, u32);
    impl Drop for Resource {
        fn drop(&mut self) {
            self.0.borrow_mut().push(self.1);
        }
    }
    #[test]
    fn nested_owners_null_resource_and_recreation_release_exactly_once() {
        let drops = Rc::new(RefCell::new(Vec::new()));
        let mut state = State::new();
        let a = state.acquire(|| Resource(drops.clone(), 0)).unwrap();
        let b = state.acquire(|| panic!("nested creation")).unwrap();
        let (token, value) = state.begin().unwrap();
        assert_eq!(value.1, 0); // Null is an owned, borrowable resource.
        state.end(token).unwrap();
        assert!(state.release(a).unwrap().is_none());
        let c = state.acquire(|| panic!("nested creation")).unwrap();
        assert!(state.release(b).unwrap().is_none());
        let retired = state.release(c).unwrap();
        assert!(drops.borrow().is_empty());
        drop(retired);
        assert_eq!(*drops.borrow(), [0]);
        let d = state.acquire(|| Resource(drops.clone(), 17)).unwrap();
        assert!(d > c);
        drop(state.release(d).unwrap());
        assert_eq!(*drops.borrow(), [0, 17]);
    }
    #[test]
    fn operation_excludes_lifetime_changes_and_checks_token_types_and_staleness() {
        let mut state = State::new();
        assert_eq!(state.begin(), Err(Error::InvalidHandle));
        let owner = state.acquire(|| 23).unwrap();
        let (token, value) = state.begin().unwrap();
        assert_eq!(*value, 23);
        assert_eq!(state.begin(), Err(Error::Busy));
        assert_eq!(state.acquire(|| panic!()), Err(Error::Busy));
        assert_eq!(state.release(owner), Err(Error::Busy));
        assert_eq!(state.release(token), Err(Error::InvalidHandle));
        assert_eq!(state.end(owner), Err(Error::InvalidHandle));
        state.end(token).unwrap();
        assert_eq!(state.end(token), Err(Error::InvalidHandle));
        let (next, _) = state.begin().unwrap();
        assert_ne!(token, next);
        assert_eq!(state.end(token), Err(Error::InvalidHandle));
        state.end(next).unwrap();
        assert_eq!(state.release(owner), Ok(Some(23)));
        assert_eq!(state.release(owner), Err(Error::InvalidHandle));
    }
    #[test]
    fn identity_exhaustion_never_rewinds_and_existing_owners_can_release() {
        let mut state = State::new();
        state.next = Some(u64::MAX - 1);
        let owner = state.acquire(|| 9).unwrap();
        let (token, _) = state.begin().unwrap();
        assert_eq!(token, u64::MAX);
        state.end(token).unwrap();
        assert_eq!(state.begin(), Err(Error::Exhausted));
        assert_eq!(state.acquire(|| panic!()), Err(Error::Exhausted));
        assert_eq!(state.release(owner), Ok(Some(9)));
        assert_eq!(state.acquire(|| panic!()), Err(Error::Exhausted));
    }
}
