// SPDX-License-Identifier: GPL-2.0-or-later
//! Experimental private document admission, not a production host contract.
//!
//! Integer IDs are validated while holding one nonblocking registry guard. No
//! native pointer is a resource handle. Busy is deliberately not mapped to a
//! public HRESULT. Caller buffers, native wrapper lifetime, allocator/new-handler
//! reentry and compatible host waiting remain separate integration contracts.
use alloc::boxed::Box;
use alloc::vec::Vec;
use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicBool, Ordering};
use xy_vsfilter_compat::document::Document;

pub(crate) type Handle = u64;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Error {
    Busy,
    InvalidHandle,
    TypeMismatch,
    Exhausted,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Kind {
    Document,
    Snapshot,
}

pub(crate) enum Resource {
    Document(Box<Document>),
    Snapshot(Vec<u8>),
}
impl Resource {
    fn kind(&self) -> Kind {
        match self {
            Self::Document(_) => Kind::Document,
            Self::Snapshot(_) => Kind::Snapshot,
        }
    }
}

pub(crate) struct Registry {
    // None is permanent exhaustion. Removing resources never rewinds the ID.
    next: Option<Handle>,
    slots: Vec<(Handle, Resource)>,
}
impl Registry {
    const fn new() -> Self {
        Self {
            next: Some(1),
            slots: Vec::new(),
        }
    }

    /// On exhaustion ownership is returned, allowing the caller to drop outside
    /// admission. Allocation failure is still governed by the existing allocator.
    pub(crate) fn insert(&mut self, resource: Resource) -> Result<Handle, (Error, Resource)> {
        let Some(id) = self.next else {
            return Err((Error::Exhausted, resource));
        };
        self.slots.push((id, resource));
        self.next = id.checked_add(1);
        Ok(id)
    }

    fn position(&self, id: Handle) -> Result<usize, Error> {
        if id == 0 {
            return Err(Error::InvalidHandle);
        }
        self.slots
            .iter()
            .position(|(handle, _)| *handle == id)
            .ok_or(Error::InvalidHandle)
    }

    pub(crate) fn document(&self, id: Handle) -> Result<&Document, Error> {
        match &self.slots[self.position(id)?].1 {
            Resource::Document(document) => Ok(document),
            Resource::Snapshot(_) => Err(Error::TypeMismatch),
        }
    }

    pub(crate) fn document_mut(&mut self, id: Handle) -> Result<&mut Document, Error> {
        let index = self.position(id)?;
        match &mut self.slots[index].1 {
            Resource::Document(document) => Ok(document),
            Resource::Snapshot(_) => Err(Error::TypeMismatch),
        }
    }

    /// Snapshots have no mutable accessor. Replacement requires a new ID.
    pub(crate) fn snapshot(&self, id: Handle) -> Result<&[u8], Error> {
        match &self.slots[self.position(id)?].1 {
            Resource::Snapshot(bytes) => Ok(bytes),
            Resource::Document(_) => Err(Error::TypeMismatch),
        }
    }

    /// Deep storage copy within this acquisition. This is not native Copy's
    /// metadata, virtual Empty, self-copy orchestration or partial-failure model.
    /// Returns retired destination storage to be dropped after admission ends.
    pub(crate) fn copy_document(
        &mut self,
        destination: Handle,
        source: Handle,
    ) -> Result<Resource, Error> {
        self.document(destination)?;
        let mut copy: Box<Document> = Box::default();
        copy.copy_storage_from(self.document(source)?);
        // Clone before the swap, including self-copy. Only the prepared vectors
        // move; all current/future destination metadata stays in place.
        self.document_mut(destination)?
            .swap_prepared_storage(&mut copy);
        Ok(Resource::Document(copy))
    }

    /// Selected native Copy header only, after the caller's virtual Empty.
    /// Both typed handles are validated before cloning; retired payloads leave
    /// this acquisition. Self-copy observes current state and needs no clone.
    pub(crate) fn copy_selected_state(
        &mut self,
        destination: Handle,
        source: Handle,
    ) -> Result<Option<xy_vsfilter_compat::document::RetiredSelectedState>, Error> {
        self.document(destination)?;
        let source_document = self.document(source)?;
        if destination == source {
            return Ok(None);
        }
        let state = source_document.selected_state();
        Ok(Some(
            self.document_mut(destination)?
                .replace_selected_state(state),
        ))
    }

    /// Removal transfers ownership; callers perform destruction after unlocking.
    pub(crate) fn remove(&mut self, id: Handle, kind: Kind) -> Result<Resource, Error> {
        let index = self.position(id)?;
        if self.slots[index].1.kind() != kind {
            return Err(Error::TypeMismatch);
        }
        Ok(self.slots.swap_remove(index).1)
    }
}

pub(crate) struct Boundary {
    occupied: AtomicBool,
    registry: UnsafeCell<Registry>,
}

// SAFETY: Registry resources are Send and only accessed after an Acquire CAS
// grants the sole guard. A Release store occurs after the closure's access ends.
// There is no unguarded registry accessor, recursive admission or escaping guard.
unsafe impl Sync for Boundary {}

struct Guard<'a> {
    boundary: &'a Boundary,
}
impl Drop for Guard<'_> {
    fn drop(&mut self) {
        self.boundary.occupied.store(false, Ordering::Release);
    }
}

impl Boundary {
    const fn new() -> Self {
        // Keep the unsafe Sync proof tied to the actual resource types, including
        // future enum variants. A non-Send addition must fail compilation here.
        const fn assert_send<T: Send>() {}
        assert_send::<Resource>();
        assert_send::<Registry>();
        Self {
            occupied: AtomicBool::new(false),
            registry: UnsafeCell::new(Registry::new()),
        }
    }

    fn try_lock(&self) -> Result<Guard<'_>, Error> {
        self.occupied
            .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
            .map_err(|_| Error::Busy)?;
        Ok(Guard { boundary: self })
    }

    /// Internal closures must perform only registry/core/value work, never host
    /// callbacks or nested acquisition. The higher-ranked argument prevents a
    /// borrowed registry/resource reference from appearing in the return value.
    /// Returning raw canonical pointers is forbidden by the internal contract.
    /// Allocation can still call the existing CRT; reentry observes Busy rather
    /// than spinning, but its externally compatible handling is not settled.
    pub(crate) fn try_with<R>(
        &self,
        operation: impl for<'a> FnOnce(&'a mut Registry) -> R,
    ) -> Result<R, Error> {
        let guard = self.try_lock()?;
        // SAFETY: guard uniquely owns admission; no references precede locking.
        // operation cannot return a reference borrowing its arbitrary lifetime.
        let result = operation(unsafe { &mut *guard.boundary.registry.get() });
        drop(guard);
        Ok(result)
    }

    pub(crate) fn destroy(&self, id: Handle, kind: Kind) -> Result<(), Error> {
        let removed = self.try_with(|registry| registry.remove(id, kind))??;
        // Registry no longer owns this payload; admission has already ended.
        drop(removed);
        Ok(())
    }
}

pub(crate) static DOCUMENTS: Boundary = Boundary::new();

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::sync::Arc;
    use std::sync::Barrier;

    fn insert(boundary: &Boundary, resource: Resource) -> Handle {
        boundary
            .try_with(|registry| registry.insert(resource).map_err(|(error, _)| error))
            .unwrap()
            .unwrap()
    }

    #[test]
    fn typed_access_and_destroy_never_reuse_stale_handles() {
        let boundary = Boundary::new();
        let owner = insert(&boundary, Resource::Document(Box::default()));
        let snapshot = insert(&boundary, Resource::Snapshot(alloc::vec![1, 2, 3]));
        boundary
            .try_with(|registry| {
                assert_eq!(registry.snapshot(snapshot).unwrap(), [1, 2, 3]);
                assert_eq!(
                    registry.document(snapshot).unwrap_err(),
                    Error::TypeMismatch
                );
                assert_eq!(registry.snapshot(owner).unwrap_err(), Error::TypeMismatch);
                assert_eq!(registry.document(0).unwrap_err(), Error::InvalidHandle);
                assert_eq!(
                    registry.document_mut(snapshot).unwrap_err(),
                    Error::TypeMismatch
                );
            })
            .unwrap();
        assert_eq!(
            boundary.destroy(owner, Kind::Snapshot),
            Err(Error::TypeMismatch)
        );
        boundary.destroy(owner, Kind::Document).unwrap();
        assert_eq!(
            boundary.destroy(owner, Kind::Document),
            Err(Error::InvalidHandle)
        );
        let next = insert(&boundary, Resource::Document(Box::default()));
        assert!(next > snapshot);
        boundary
            .try_with(|registry| {
                assert_eq!(registry.document(owner).unwrap_err(), Error::InvalidHandle);
                assert_eq!(registry.snapshot(snapshot).unwrap(), [1, 2, 3]);
            })
            .unwrap();
        boundary.destroy(snapshot, Kind::Snapshot).unwrap();
    }

    #[test]
    fn same_thread_reentry_is_busy_and_guard_releases_after_unwind() {
        let boundary = Boundary::new();
        boundary
            .try_with(|_| assert_eq!(boundary.try_with(|_| ()), Err(Error::Busy)))
            .unwrap();
        let caught = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            let _ = boundary.try_with(|_| panic!("test guard unwind"));
        }));
        assert!(caught.is_err());
        assert_eq!(boundary.try_with(|_| ()), Ok(()));
    }

    #[test]
    fn destroy_and_lookup_interleave_only_between_acquisitions() {
        let boundary = Arc::new(Boundary::new());
        let owner = insert(&boundary, Resource::Document(Box::default()));
        let entered = Arc::new(Barrier::new(2));
        let release = Arc::new(Barrier::new(2));
        let thread_boundary = boundary.clone();
        let thread_entered = entered.clone();
        let thread_release = release.clone();
        let thread = std::thread::spawn(move || {
            thread_boundary
                .try_with(|registry| {
                    assert!(registry.document(owner).is_ok());
                    thread_entered.wait();
                    thread_release.wait();
                    assert!(registry.document(owner).is_ok());
                })
                .unwrap();
        });
        entered.wait();
        assert_eq!(boundary.try_with(|_| ()), Err(Error::Busy));
        assert_eq!(boundary.destroy(owner, Kind::Document), Err(Error::Busy));
        release.wait();
        thread.join().unwrap();
        boundary.destroy(owner, Kind::Document).unwrap();
        boundary
            .try_with(|registry| {
                assert_eq!(registry.document(owner).unwrap_err(), Error::InvalidHandle)
            })
            .unwrap();
    }

    #[test]
    fn maximum_id_is_issued_once_then_registry_stays_exhausted() {
        let boundary = Boundary::new();
        boundary
            .try_with(|registry| registry.next = Some(u64::MAX))
            .unwrap();
        let last = insert(&boundary, Resource::Snapshot(Vec::new()));
        assert_eq!(last, u64::MAX);
        boundary.destroy(last, Kind::Snapshot).unwrap();
        let failed = boundary
            .try_with(|registry| registry.insert(Resource::Snapshot(Vec::new())))
            .unwrap();
        let (error, returned) = failed.err().unwrap();
        assert_eq!(error, Error::Exhausted);
        drop(returned);
        boundary
            .try_with(|registry| {
                assert_eq!(registry.document(last).unwrap_err(), Error::InvalidHandle)
            })
            .unwrap();
    }

    #[test]
    fn copies_and_snapshot_replies_are_independent() {
        let boundary = Boundary::new();
        let source = insert(&boundary, Resource::Document(Box::default()));
        let destination = insert(&boundary, Resource::Document(Box::default()));
        let snapshot = insert(&boundary, Resource::Snapshot(alloc::vec![9, 8]));
        let retired = boundary
            .try_with(|registry| {
                use xy_vsfilter_compat::document::{AddMode, PreparedEntry};
                let entry = PreparedEntry {
                    text: alloc::vec![0, 0xd800],
                    style: alloc::vec![1],
                    actor: Vec::new(),
                    effect: Vec::new(),
                    unicode: true,
                    margins: [0; 4],
                    layer: 0,
                    start: 0,
                    end: 10,
                    readorder: -1,
                };
                registry
                    .document_mut(source)
                    .unwrap()
                    .add_prepared(entry, AddMode::Add)
                    .unwrap();
                registry
                    .document_mut(source)
                    .unwrap()
                    .feedback_animation(0, true)
                    .unwrap();
                let first_retired = registry.copy_document(destination, source).unwrap();
                let second_retired = registry.copy_document(source, source).unwrap();
                assert_eq!(
                    registry.copy_document(snapshot, source).err(),
                    Some(Error::TypeMismatch)
                );
                assert_eq!(
                    registry.copy_document(destination, snapshot).err(),
                    Some(Error::TypeMismatch)
                );
                registry
                    .document_mut(source)
                    .unwrap()
                    .clear_entries_and_segments();
                let copied = registry.document(destination).unwrap();
                assert_eq!(copied.entries()[0].text, [0, 0xd800]);
                assert!(copied.segments()[0].animated);
                assert_eq!(registry.snapshot(snapshot).unwrap(), [9, 8]);
                (first_retired, second_retired)
            })
            .unwrap();
        drop(retired);
        boundary.destroy(source, Kind::Document).unwrap();
        boundary
            .try_with(|registry| assert_eq!(registry.snapshot(snapshot).unwrap(), [9, 8]))
            .unwrap();
    }
}
