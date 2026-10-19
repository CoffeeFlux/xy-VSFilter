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
    UnknownRun,
    StyleAddRun,
    StyleCopyRun,
    OpenRun,
    AssRun,
    XombieRun,
    TextRun,
    MicroDvdRun,
    RealTextRun,
    UsfRun,
    UsfSource,
    UsfScalar,
    VttRun,
    SamiRun,
    SearchRun,
    Effects,
}

// Inline sessions avoid a new fallible box allocation after core ownership transfer.
// Registry slot growth is reserved before Begin; compact boxing can be revisited
// only with an explicit preallocated ownership envelope.
#[allow(clippy::large_enum_variant)]
pub(crate) enum Resource {
    Effects {
        revision: u64,
        state: xy_vsfilter_compat::effects::State,
    },
    Document(Box<Document>),
    Snapshot(Vec<u8>),
    UnknownRun {
        owner: Handle,
        run: xy_vsfilter_compat::document_unknown::Run,
    },
    StyleAddRun {
        owner: Handle,
        run: xy_vsfilter_compat::document_style_add::Session,
    },
    StyleCopyRun {
        destination: Handle,
        source: Handle,
        run: xy_vsfilter_compat::document_style_copy::Run,
    },
    OpenRun {
        owner: Handle,
        run: xy_vsfilter_compat::document_open::Run,
    },
    AssRun {
        owner: Handle,
        run: xy_vsfilter_compat::ass_document::Run,
    },
    XombieRun {
        owner: Handle,
        run: xy_vsfilter_compat::xombie::Run,
    },
    TextRun {
        owner: Handle,
        run: xy_vsfilter_compat::text_document::Run,
    },
    MicroDvdRun {
        owner: Handle,
        run: xy_vsfilter_compat::microdvd_document::Run,
    },
    UsfSource(xy_vsfilter_compat::usf_model::State),
    UsfScalar(xy_vsfilter_compat::usf_scalar::Run),
    UsfRun {
        owner: Handle,
        source: Option<Handle>,
        run: xy_vsfilter_compat::usf_conversion::Run,
    },
    RealTextRun {
        owner: Handle,
        run: xy_vsfilter_compat::realtext_engine::Run,
    },
    VttRun {
        owner: Handle,
        run: xy_vsfilter_compat::vtt_document::Run,
    },
    SamiRun {
        owner: Handle,
        run: xy_vsfilter_compat::sami_document::Run,
    },
    SearchRun {
        owner: Handle,
        run: xy_vsfilter_compat::document_search::Run,
    },
}
impl Resource {
    fn kind(&self) -> Kind {
        match self {
            Self::Document(_) => Kind::Document,
            Self::Effects { .. } => Kind::Effects,
            Self::Snapshot(_) => Kind::Snapshot,
            Self::UnknownRun { .. } => Kind::UnknownRun,
            Self::StyleAddRun { .. } => Kind::StyleAddRun,
            Self::StyleCopyRun { .. } => Kind::StyleCopyRun,
            Self::OpenRun { .. } => Kind::OpenRun,
            Self::AssRun { .. } => Kind::AssRun,
            Self::XombieRun { .. } => Kind::XombieRun,
            Self::TextRun { .. } => Kind::TextRun,
            Self::MicroDvdRun { .. } => Kind::MicroDvdRun,
            Self::RealTextRun { .. } => Kind::RealTextRun,
            Self::UsfRun { .. } => Kind::UsfRun,
            Self::UsfSource(_) => Kind::UsfSource,
            Self::UsfScalar(_) => Kind::UsfScalar,
            Self::VttRun { .. } => Kind::VttRun,
            Self::SamiRun { .. } => Kind::SamiRun,
            Self::SearchRun { .. } => Kind::SearchRun,
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
    // Return owned rejected storage directly for destruction after admission,
    // without introducing an allocation on the error path.
    #[allow(clippy::result_large_err)]
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
            _ => Err(Error::TypeMismatch),
        }
    }

    pub(crate) fn document_mut(&mut self, id: Handle) -> Result<&mut Document, Error> {
        let index = self.position(id)?;
        match &mut self.slots[index].1 {
            Resource::Document(document) => Ok(document),
            _ => Err(Error::TypeMismatch),
        }
    }

    pub(crate) fn effects(
        &mut self,
        id: Handle,
    ) -> Result<(&mut u64, &mut xy_vsfilter_compat::effects::State), Error> {
        let index = self.position(id)?;
        match &mut self.slots[index].1 {
            Resource::Effects { revision, state } => Ok((revision, state)),
            _ => Err(Error::TypeMismatch),
        }
    }

    /// Snapshots have no mutable accessor. Replacement requires a new ID.
    pub(crate) fn snapshot(&self, id: Handle) -> Result<&[u8], Error> {
        match &self.slots[self.position(id)?].1 {
            Resource::Snapshot(bytes) => Ok(bytes),
            _ => Err(Error::TypeMismatch),
        }
    }

    /// Reserve before session mutation; no status-returning work follows commit.
    pub(crate) fn reserve_insert(&mut self) -> Result<Handle, Error> {
        let id = self.next.ok_or(Error::Exhausted)?;
        self.slots.reserve(1);
        Ok(id)
    }
    pub(crate) fn reserve_two(&mut self) -> Result<(Handle, Handle), Error> {
        let first = self.next.ok_or(Error::Exhausted)?;
        let second = first.checked_add(1).ok_or(Error::Exhausted)?;
        self.slots.reserve(2);
        Ok((first, second))
    }
    pub(crate) fn insert_reserved(&mut self, id: Handle, resource: Resource) {
        assert_eq!(self.next, Some(id));
        self.slots.push((id, resource));
        self.next = id.checked_add(1);
    }
    pub(crate) fn unknown_owner(&self, id: Handle) -> Result<Handle, Error> {
        match &self.slots[self.position(id)?].1 {
            Resource::UnknownRun { owner, .. } => Ok(*owner),
            _ => Err(Error::TypeMismatch),
        }
    }
    pub(crate) fn unknown_pair(
        &mut self,
        id: Handle,
    ) -> Result<
        (
            &mut Document,
            &mut xy_vsfilter_compat::document_unknown::Run,
        ),
        Error,
    > {
        let run_index = self.position(id)?;
        let owner = self.unknown_owner(id)?;
        let owner_index = self.position(owner)?;
        if run_index == owner_index {
            return Err(Error::TypeMismatch);
        }
        let (owner_resource, run_resource) = if owner_index < run_index {
            let (before, after) = self.slots.split_at_mut(run_index);
            (&mut before[owner_index].1, &mut after[0].1)
        } else {
            let (before, after) = self.slots.split_at_mut(owner_index);
            (&mut after[0].1, &mut before[run_index].1)
        };
        match (owner_resource, run_resource) {
            (Resource::Document(document), Resource::UnknownRun { run, .. }) => Ok((document, run)),
            _ => Err(Error::TypeMismatch),
        }
    }

    pub(crate) fn style_add_pair(
        &mut self,
        id: Handle,
    ) -> Result<
        (
            &mut Document,
            &mut xy_vsfilter_compat::document_style_add::Session,
        ),
        Error,
    > {
        let run_index = self.position(id)?;
        let owner = match &self.slots[run_index].1 {
            Resource::StyleAddRun { owner, .. } => *owner,
            _ => return Err(Error::TypeMismatch),
        };
        let owner_index = self.position(owner)?;
        if run_index == owner_index {
            return Err(Error::TypeMismatch);
        }
        let (owner_resource, run_resource) = if owner_index < run_index {
            let (before, after) = self.slots.split_at_mut(run_index);
            (&mut before[owner_index].1, &mut after[0].1)
        } else {
            let (before, after) = self.slots.split_at_mut(owner_index);
            (&mut after[0].1, &mut before[run_index].1)
        };
        match (owner_resource, run_resource) {
            (Resource::Document(document), Resource::StyleAddRun { run, .. }) => {
                Ok((document, run))
            }
            _ => Err(Error::TypeMismatch),
        }
    }

    pub(crate) fn style_copy_pair(
        &mut self,
        id: Handle,
    ) -> Result<
        (
            &mut Document,
            Option<&Document>,
            &mut xy_vsfilter_compat::document_style_copy::Run,
        ),
        Error,
    > {
        let run_index = self.position(id)?;
        let (destination, source) = match &self.slots[run_index].1 {
            Resource::StyleCopyRun {
                destination,
                source,
                ..
            } => (*destination, *source),
            _ => return Err(Error::TypeMismatch),
        };
        let destination_index = self.position(destination)?;
        let source_index = self.position(source)?;
        if destination_index == run_index || source_index == run_index {
            return Err(Error::TypeMismatch);
        }
        if destination_index == source_index {
            let (document_resource, run_resource) = if destination_index < run_index {
                let (before, after) = self.slots.split_at_mut(run_index);
                (&mut before[destination_index].1, &mut after[0].1)
            } else {
                let (before, after) = self.slots.split_at_mut(destination_index);
                (&mut after[0].1, &mut before[run_index].1)
            };
            return match (document_resource, run_resource) {
                (Resource::Document(document), Resource::StyleCopyRun { run, .. }) => {
                    Ok((document, None, run))
                }
                _ => Err(Error::TypeMismatch),
            };
        }
        let mut positions = [
            (destination_index, 0usize),
            (source_index, 1),
            (run_index, 2),
        ];
        positions.sort_unstable_by_key(|row| row.0);
        let (before_last, last) = self.slots.split_at_mut(positions[2].0);
        let (before_middle, middle) = before_last.split_at_mut(positions[1].0);
        let mut resources = [
            Some(&mut before_middle[positions[0].0].1),
            Some(&mut middle[0].1),
            Some(&mut last[0].1),
        ];
        let rank = |tag| positions.iter().position(|row| row.1 == tag).unwrap();
        let destination = resources[rank(0)].take().unwrap();
        let source = resources[rank(1)].take().unwrap();
        let run = resources[rank(2)].take().unwrap();
        match (destination, source, run) {
            (
                Resource::Document(destination),
                Resource::Document(source),
                Resource::StyleCopyRun { run, .. },
            ) => Ok((destination, Some(source), run)),
            _ => Err(Error::TypeMismatch),
        }
    }

    pub(crate) fn open_pair(
        &mut self,
        id: Handle,
    ) -> Result<(&mut Document, &mut xy_vsfilter_compat::document_open::Run), Error> {
        let run_index = self.position(id)?;
        let owner = match &self.slots[run_index].1 {
            Resource::OpenRun { owner, .. } => *owner,
            _ => return Err(Error::TypeMismatch),
        };
        let owner_index = self.position(owner)?;
        if owner_index == run_index {
            return Err(Error::TypeMismatch);
        }
        let (document_resource, run_resource) = if owner_index < run_index {
            let (before, after) = self.slots.split_at_mut(run_index);
            (&mut before[owner_index].1, &mut after[0].1)
        } else {
            let (before, after) = self.slots.split_at_mut(owner_index);
            (&mut after[0].1, &mut before[run_index].1)
        };
        match (document_resource, run_resource) {
            (Resource::Document(document), Resource::OpenRun { run, .. }) => Ok((document, run)),
            _ => Err(Error::TypeMismatch),
        }
    }

    pub(crate) fn ass_pair(
        &mut self,
        id: Handle,
    ) -> Result<(&mut Document, &mut xy_vsfilter_compat::ass_document::Run), Error> {
        let run_index = self.position(id)?;
        let owner = match &self.slots[run_index].1 {
            Resource::AssRun { owner, .. } => *owner,
            _ => return Err(Error::TypeMismatch),
        };
        let owner_index = self.position(owner)?;
        if owner_index == run_index {
            return Err(Error::TypeMismatch);
        }
        let (document_resource, run_resource) = if owner_index < run_index {
            let (before, after) = self.slots.split_at_mut(run_index);
            (&mut before[owner_index].1, &mut after[0].1)
        } else {
            let (before, after) = self.slots.split_at_mut(owner_index);
            (&mut after[0].1, &mut before[run_index].1)
        };
        match (document_resource, run_resource) {
            (Resource::Document(document), Resource::AssRun { run, .. }) => Ok((document, run)),
            _ => Err(Error::TypeMismatch),
        }
    }

    pub(crate) fn xombie_pair(
        &mut self,
        id: Handle,
    ) -> Result<(&mut Document, &mut xy_vsfilter_compat::xombie::Run), Error> {
        let run_index = self.position(id)?;
        let owner = match &self.slots[run_index].1 {
            Resource::XombieRun { owner, .. } => *owner,
            _ => return Err(Error::TypeMismatch),
        };
        let owner_index = self.position(owner)?;
        if owner_index == run_index {
            return Err(Error::TypeMismatch);
        }
        let (document_resource, run_resource) = if owner_index < run_index {
            let (before, after) = self.slots.split_at_mut(run_index);
            (&mut before[owner_index].1, &mut after[0].1)
        } else {
            let (before, after) = self.slots.split_at_mut(owner_index);
            (&mut after[0].1, &mut before[run_index].1)
        };
        match (document_resource, run_resource) {
            (Resource::Document(document), Resource::XombieRun { run, .. }) => Ok((document, run)),
            _ => Err(Error::TypeMismatch),
        }
    }

    pub(crate) fn text_pair(
        &mut self,
        id: Handle,
    ) -> Result<(&mut Document, &mut xy_vsfilter_compat::text_document::Run), Error> {
        let run_index = self.position(id)?;
        let owner = match &self.slots[run_index].1 {
            Resource::TextRun { owner, .. } => *owner,
            _ => return Err(Error::TypeMismatch),
        };
        let owner_index = self.position(owner)?;
        if owner_index == run_index {
            return Err(Error::TypeMismatch);
        }
        let (document_resource, run_resource) = if owner_index < run_index {
            let (before, after) = self.slots.split_at_mut(run_index);
            (&mut before[owner_index].1, &mut after[0].1)
        } else {
            let (before, after) = self.slots.split_at_mut(owner_index);
            (&mut after[0].1, &mut before[run_index].1)
        };
        match (document_resource, run_resource) {
            (Resource::Document(document), Resource::TextRun { run, .. }) => Ok((document, run)),
            _ => Err(Error::TypeMismatch),
        }
    }

    pub(crate) fn microdvd_pair(
        &mut self,
        id: Handle,
    ) -> Result<
        (
            &mut Document,
            &mut xy_vsfilter_compat::microdvd_document::Run,
        ),
        Error,
    > {
        let run_index = self.position(id)?;
        let owner = match &self.slots[run_index].1 {
            Resource::MicroDvdRun { owner, .. } => *owner,
            _ => return Err(Error::TypeMismatch),
        };
        let owner_index = self.position(owner)?;
        if owner_index == run_index {
            return Err(Error::TypeMismatch);
        }
        let (document_resource, run_resource) = if owner_index < run_index {
            let (before, after) = self.slots.split_at_mut(run_index);
            (&mut before[owner_index].1, &mut after[0].1)
        } else {
            let (before, after) = self.slots.split_at_mut(owner_index);
            (&mut after[0].1, &mut before[run_index].1)
        };
        match (document_resource, run_resource) {
            (Resource::Document(document), Resource::MicroDvdRun { run, .. }) => {
                Ok((document, run))
            }
            _ => Err(Error::TypeMismatch),
        }
    }

    pub(crate) fn usf_scalar_mut(
        &mut self,
        id: Handle,
    ) -> Result<&mut xy_vsfilter_compat::usf_scalar::Run, Error> {
        let index = self.position(id)?;
        match &mut self.slots[index].1 {
            Resource::UsfScalar(run) => Ok(run),
            _ => Err(Error::TypeMismatch),
        }
    }
    pub(crate) fn usf_source(
        &self,
        id: Handle,
    ) -> Result<&xy_vsfilter_compat::usf_model::State, Error> {
        match &self.slots[self.position(id)?].1 {
            Resource::UsfSource(s) => Ok(s),
            _ => Err(Error::TypeMismatch),
        }
    }
    pub(crate) fn usf_source_mut(
        &mut self,
        id: Handle,
    ) -> Result<&mut xy_vsfilter_compat::usf_model::State, Error> {
        let i = self.position(id)?;
        match &mut self.slots[i].1 {
            Resource::UsfSource(s) => Ok(s),
            _ => Err(Error::TypeMismatch),
        }
    }
    pub(crate) fn usf_source_in_use(&self, id: Handle) -> bool {
        self.slots
            .iter()
            .any(|(_, r)| matches!(r, Resource::UsfRun{source:Some(s),..} if *s==id))
    }
    pub(crate) fn usf_source_id(&self, id: Handle) -> Result<Option<Handle>, Error> {
        match &self.slots[self.position(id)?].1 {
            Resource::UsfRun { source, .. } => Ok(*source),
            _ => Err(Error::TypeMismatch),
        }
    }
    pub(crate) fn usf_pair(
        &mut self,
        id: Handle,
    ) -> Result<(&mut Document, &mut xy_vsfilter_compat::usf_conversion::Run), Error> {
        if let Some(source) = self.usf_source_id(id)? {
            self.usf_source(source)?;
        }
        let run_index = self.position(id)?;
        let owner = match &self.slots[run_index].1 {
            Resource::UsfRun { owner, .. } => *owner,
            _ => return Err(Error::TypeMismatch),
        };
        let owner_index = self.position(owner)?;
        if owner_index == run_index {
            return Err(Error::TypeMismatch);
        }
        let (document_resource, run_resource) = if owner_index < run_index {
            let (before, after) = self.slots.split_at_mut(run_index);
            (&mut before[owner_index].1, &mut after[0].1)
        } else {
            let (before, after) = self.slots.split_at_mut(owner_index);
            (&mut after[0].1, &mut before[run_index].1)
        };
        match (document_resource, run_resource) {
            (Resource::Document(document), Resource::UsfRun { run, .. }) => Ok((document, run)),
            _ => Err(Error::TypeMismatch),
        }
    }

    pub(crate) fn realtext_pair(
        &mut self,
        id: Handle,
    ) -> Result<(&mut Document, &mut xy_vsfilter_compat::realtext_engine::Run), Error> {
        let run_index = self.position(id)?;
        let owner = match &self.slots[run_index].1 {
            Resource::RealTextRun { owner, .. } => *owner,
            _ => return Err(Error::TypeMismatch),
        };
        let owner_index = self.position(owner)?;
        if owner_index == run_index {
            return Err(Error::TypeMismatch);
        }
        let (document_resource, run_resource) = if owner_index < run_index {
            let (before, after) = self.slots.split_at_mut(run_index);
            (&mut before[owner_index].1, &mut after[0].1)
        } else {
            let (before, after) = self.slots.split_at_mut(owner_index);
            (&mut after[0].1, &mut before[run_index].1)
        };
        match (document_resource, run_resource) {
            (Resource::Document(document), Resource::RealTextRun { run, .. }) => {
                Ok((document, run))
            }
            _ => Err(Error::TypeMismatch),
        }
    }

    pub(crate) fn vtt_pair(
        &mut self,
        id: Handle,
    ) -> Result<(&mut Document, &mut xy_vsfilter_compat::vtt_document::Run), Error> {
        let run_index = self.position(id)?;
        let owner = match &self.slots[run_index].1 {
            Resource::VttRun { owner, .. } => *owner,
            _ => return Err(Error::TypeMismatch),
        };
        let owner_index = self.position(owner)?;
        if owner_index == run_index {
            return Err(Error::TypeMismatch);
        }
        let (document_resource, run_resource) = if owner_index < run_index {
            let (before, after) = self.slots.split_at_mut(run_index);
            (&mut before[owner_index].1, &mut after[0].1)
        } else {
            let (before, after) = self.slots.split_at_mut(owner_index);
            (&mut after[0].1, &mut before[run_index].1)
        };
        match (document_resource, run_resource) {
            (Resource::Document(document), Resource::VttRun { run, .. }) => Ok((document, run)),
            _ => Err(Error::TypeMismatch),
        }
    }

    pub(crate) fn sami_pair(
        &mut self,
        id: Handle,
    ) -> Result<(&mut Document, &mut xy_vsfilter_compat::sami_document::Run), Error> {
        let run_index = self.position(id)?;
        let owner = match &self.slots[run_index].1 {
            Resource::SamiRun { owner, .. } => *owner,
            _ => return Err(Error::TypeMismatch),
        };
        let owner_index = self.position(owner)?;
        if owner_index == run_index {
            return Err(Error::TypeMismatch);
        }
        let (document_resource, run_resource) = if owner_index < run_index {
            let (before, after) = self.slots.split_at_mut(run_index);
            (&mut before[owner_index].1, &mut after[0].1)
        } else {
            let (before, after) = self.slots.split_at_mut(owner_index);
            (&mut after[0].1, &mut before[run_index].1)
        };
        match (document_resource, run_resource) {
            (Resource::Document(document), Resource::SamiRun { run, .. }) => Ok((document, run)),
            _ => Err(Error::TypeMismatch),
        }
    }

    pub(crate) fn search_pair(
        &mut self,
        id: Handle,
    ) -> Result<(&Document, &mut xy_vsfilter_compat::document_search::Run), Error> {
        let run_index = self.position(id)?;
        let owner = match &self.slots[run_index].1 {
            Resource::SearchRun { owner, .. } => *owner,
            _ => return Err(Error::TypeMismatch),
        };
        let owner_index = self.position(owner)?;
        if owner_index == run_index {
            return Err(Error::TypeMismatch);
        }
        let (document_resource, run_resource) = if owner_index < run_index {
            let (before, after) = self.slots.split_at_mut(run_index);
            (&mut before[owner_index].1, &mut after[0].1)
        } else {
            let (before, after) = self.slots.split_at_mut(owner_index);
            (&mut after[0].1, &mut before[run_index].1)
        };
        match (document_resource, run_resource) {
            (Resource::Document(document), Resource::SearchRun { run, .. }) => Ok((document, run)),
            _ => Err(Error::TypeMismatch),
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
    pub(crate) const fn new() -> Self {
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
    fn translated_search_typed_pair_both_orders_and_stale_owner() {
        let mut registry = Registry::new();
        let owner = registry
            .insert(Resource::Document(Box::default()))
            .ok()
            .unwrap();
        let run = xy_vsfilter_compat::document_search::Run::new(
            registry.document(owner).unwrap(),
            xy_vsfilter_compat::timeline_search::Mode::Entry,
            0,
        )
        .unwrap();
        let id = registry
            .insert(Resource::SearchRun { owner, run })
            .ok()
            .unwrap();
        for _ in 0..2 {
            let (document, run) = registry.search_pair(id).unwrap();
            assert!(document.is_empty());
            assert_eq!(run.state().count, 0);
            registry.slots.swap(0, 1);
        }
        assert!(matches!(
            registry.search_pair(owner),
            Err(Error::TypeMismatch)
        ));
        registry.remove(owner, Kind::Document).unwrap();
        assert!(matches!(
            registry.search_pair(id),
            Err(Error::InvalidHandle)
        ));
        assert!(registry.remove(id, Kind::SearchRun).is_ok());
    }

    #[test]
    fn open_run_safe_pair_in_both_orders_and_stale_owner() {
        let mut registry = Registry::new();
        let owner = registry
            .insert(Resource::Document(Box::default()))
            .ok()
            .unwrap();
        let id = registry
            .insert(Resource::OpenRun {
                owner,
                run: xy_vsfilter_compat::document_open::Run::new(1),
            })
            .ok()
            .unwrap();
        for _ in 0..2 {
            let (document, run) = registry.open_pair(id).unwrap();
            document.set_script_size(1, 2);
            assert_eq!(run.action().kind as u32, 0);
            registry.slots.swap(0, 1);
        }
        assert!(matches!(
            registry.open_pair(owner),
            Err(Error::TypeMismatch)
        ));
        registry.remove(owner, Kind::Document).unwrap();
        assert!(matches!(registry.open_pair(id), Err(Error::InvalidHandle)));
        assert!(registry.remove(id, Kind::OpenRun).is_ok());
    }

    #[test]
    fn copy_run_safe_borrows_cover_all_slot_orders_and_self() {
        for order in [
            [1, 2, 3],
            [1, 3, 2],
            [2, 1, 3],
            [2, 3, 1],
            [3, 1, 2],
            [3, 2, 1],
        ] {
            let mut registry = Registry::new();
            let destination = registry
                .insert(Resource::Document(Box::default()))
                .ok()
                .unwrap();
            let source = registry
                .insert(Resource::Document(Box::default()))
                .ok()
                .unwrap();
            let id = registry
                .insert(Resource::StyleCopyRun {
                    destination,
                    source,
                    run: xy_vsfilter_compat::document_style_copy::Run::new(false),
                })
                .ok()
                .unwrap();
            registry
                .slots
                .sort_by_key(|slot| order.iter().position(|handle| *handle == slot.0).unwrap());
            let (destination, source, run) = registry.style_copy_pair(id).unwrap();
            destination.set_script_size(1, 2);
            assert_ne!(destination.script_size(), source.unwrap().script_size());
            assert!(!run.initialized());
            registry.next = None;
            assert_eq!(registry.reserve_insert(), Err(Error::Exhausted));
            assert!(!registry.style_copy_pair(id).unwrap().2.initialized());
        }
        let mut registry = Registry::new();
        let owner = registry
            .insert(Resource::Document(Box::default()))
            .ok()
            .unwrap();
        let id = registry
            .insert(Resource::StyleCopyRun {
                destination: owner,
                source: owner,
                run: xy_vsfilter_compat::document_style_copy::Run::new(false),
            })
            .ok()
            .unwrap();
        for _ in 0..2 {
            assert!(registry.style_copy_pair(id).unwrap().1.is_none());
            registry.slots.swap(0, 1);
        }
    }

    #[test]
    fn two_slot_preflight_checks_exhaustion_before_commit() {
        let mut registry = Registry::new();
        registry.next = Some(u64::MAX);
        assert_eq!(registry.reserve_two(), Err(Error::Exhausted));
        assert_eq!(registry.next, Some(u64::MAX));
        assert!(registry.slots.is_empty());
        registry.next = Some(u64::MAX - 1);
        let (first, second) = registry.reserve_two().unwrap();
        registry.insert_reserved(first, Resource::Snapshot(alloc::vec![1]));
        registry.insert_reserved(second, Resource::Snapshot(alloc::vec![2]));
        assert_eq!(registry.next, None);
        assert_eq!(registry.snapshot(first).unwrap(), [1]);
        assert_eq!(registry.snapshot(second).unwrap(), [2]);
    }

    #[test]
    fn unknown_typed_owner_and_exhaustion_preflight() {
        let mut registry = Registry::new();
        let owner = registry
            .insert(Resource::Document(Box::default()))
            .ok()
            .unwrap();
        let run = registry
            .insert(Resource::UnknownRun {
                owner,
                run: xy_vsfilter_compat::document_unknown::Run::new(),
            })
            .ok()
            .unwrap();
        assert_eq!(registry.document(run).unwrap_err(), Error::TypeMismatch);
        assert_eq!(registry.unknown_owner(owner), Err(Error::TypeMismatch));
        let before = registry.unknown_pair(run).unwrap().1.clone();
        registry.next = None;
        assert_eq!(registry.reserve_insert(), Err(Error::Exhausted));
        assert_eq!(*registry.unknown_pair(run).unwrap().1, before);
        registry.remove(owner, Kind::Document).unwrap();
        assert!(matches!(
            registry.unknown_pair(run),
            Err(Error::InvalidHandle)
        ));
        assert!(registry.remove(run, Kind::UnknownRun).is_ok());
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
