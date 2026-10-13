// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::{boxed::Box, vec::Vec};
use xy_vsfilter_compat::{
    segment_insert::{plan, Edit},
    segments::Segment,
};

#[derive(Default)]
pub struct Insertion {
    segments: Vec<Segment>,
    readorders: Vec<i32>,
    edits: Vec<Edit>,
}

#[no_mangle]
pub extern "C" fn xy_vsfilter_insert_create() -> *mut Insertion {
    Box::into_raw(Box::new(Insertion::default()))
}

/// Seed kinds: 0=readorder (a), 1=segment (start=a,end=b), 2=entry index (a)
/// appended to the last segment. Returns zero for invalid seed operations.
/// # Safety
/// owner is the unique live pointer from create, without concurrent access.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_insert_seed(
    owner: *mut Insertion,
    kind: u32,
    a: i64,
    b: i64,
) -> u32 {
    // SAFETY: the noncopyable native wrapper supplies exclusive access.
    let owner = unsafe { &mut *owner };
    match kind {
        0 => match i32::try_from(a) {
            Ok(order) => owner.readorders.push(order),
            Err(_) => return 0,
        },
        1 => owner.segments.push(Segment {
            start: a,
            end: b,
            entries: Vec::new(),
        }),
        2 => {
            let (Some(segment), Ok(entry)) = (owner.segments.last_mut(), usize::try_from(a)) else {
                return 0;
            };
            segment.entries.push(entry);
        }
        _ => return 0,
    }
    1
}

/// Consume seeded segments and generate native edits. Returns zero for invalid
/// native state; no native memory is touched. A new owner is used for each Add.
/// # Safety
/// owner is the unique live pointer from create, without concurrent access.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_insert_build(
    owner: *mut Insertion,
    start: i64,
    end: i64,
    entry: usize,
    position: usize,
) -> u32 {
    // SAFETY: the noncopyable native wrapper supplies exclusive access.
    let owner = unsafe { &mut *owner };
    owner.edits.clear();
    match plan(
        core::mem::take(&mut owner.segments),
        &owner.readorders,
        start,
        end,
        entry,
        position,
    ) {
        Some(edits) => {
            owner.edits = edits;
            1
        }
        None => 0,
    }
}

/// Fields: 0=edit kind, 1=segment index, 2=value/start/entry, 3=end/entry-position,
/// 4=inserted segment entry index, 6=total edit count.
/// Signed times are returned as u64 bit patterns. Invalid queries return MAX.
/// # Safety
/// owner is live and immutable for this call. No borrowed storage escapes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_insert_field(
    owner: *const Insertion,
    edit: usize,
    field: u32,
) -> u64 {
    // SAFETY: the native wrapper retains the owner without mutation.
    let owner = unsafe { &*owner };
    if field == 6 {
        return owner.edits.len() as u64;
    }
    let Some(edit) = owner.edits.get(edit) else {
        return u64::MAX;
    };
    match (field, edit) {
        (0, Edit::SplitStart(..)) => 0,
        (0, Edit::SplitEnd(..)) => 1,
        (0, Edit::Insert(..)) => 2,
        (0, Edit::AppendEntry(..)) => 3,
        (0, Edit::InsertEntry(..)) => 4,
        (
            1,
            Edit::SplitStart(at, _)
            | Edit::SplitEnd(at, _)
            | Edit::Insert(at, _, _, _)
            | Edit::AppendEntry(at, _)
            | Edit::InsertEntry(at, _, _),
        ) => *at as u64,
        (2, Edit::SplitStart(_, value) | Edit::SplitEnd(_, value)) => *value as u64,
        (2, Edit::Insert(_, start, _, _)) => *start as u64,
        (2, Edit::AppendEntry(_, entry) | Edit::InsertEntry(_, _, entry)) => *entry as u64,
        (3, Edit::Insert(_, _, end, _)) => *end as u64,
        (3, Edit::InsertEntry(_, position, _)) => *position as u64,
        (4, Edit::Insert(_, _, _, entry)) => *entry as u64,
        _ => u64::MAX,
    }
}

/// # Safety
/// Release the unique live pointer exactly once after all calls; null is allowed.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_insert_release(owner: *mut Insertion) {
    if !owner.is_null() {
        // SAFETY: ownership returns to Rust without outstanding borrows.
        drop(unsafe { Box::from_raw(owner) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn scalar_seed_edit_queries_and_lifetime() {
        let owner = xy_vsfilter_insert_create();
        // SAFETY: one live owner, sequential seed/build/query calls, released once.
        unsafe {
            assert_eq!(xy_vsfilter_insert_seed(owner, 2, 0, 0), 0);
            assert_eq!(xy_vsfilter_insert_seed(owner, 0, 0, 0), 1);
            assert_eq!(xy_vsfilter_insert_seed(owner, 0, -1, 0), 1);
            assert_eq!(xy_vsfilter_insert_seed(owner, 1, i64::MIN, i64::MAX), 1);
            assert_eq!(xy_vsfilter_insert_seed(owner, 2, 0, 0), 1);
            assert_eq!(xy_vsfilter_insert_build(owner, -1, 1, 1, 1), 1);
            assert_eq!(xy_vsfilter_insert_field(owner, 0, 6), 3);
            assert_eq!(xy_vsfilter_insert_field(owner, 0, 2), u64::MAX);
            assert_eq!(xy_vsfilter_insert_field(owner, 2, 0), 4);
            assert_eq!(xy_vsfilter_insert_field(owner, 2, 2), 1);
            assert_eq!(xy_vsfilter_insert_field(owner, usize::MAX, 0), u64::MAX);
            xy_vsfilter_insert_release(owner);
        }
    }
}
