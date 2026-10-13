// SPDX-License-Identifier: GPL-2.0-or-later
//! Private segment builder. Only scalar values cross the native boundary.
use alloc::{boxed::Box, vec::Vec};
use xy_vsfilter_compat::segments::{create, Segment};

#[derive(Default)]
pub struct Segments {
    entries: Vec<(i64, i64)>,
    segments: Vec<Segment>,
}

#[no_mangle]
pub extern "C" fn xy_vsfilter_segments_create() -> *mut Segments {
    Box::into_raw(Box::new(Segments::default()))
}

/// # Safety
/// owner is the unique live pointer from create; no concurrent access.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_segments_add(owner: *mut Segments, start: i64, end: i64) {
    // SAFETY: the noncopyable C++ wrapper supplies exclusive access.
    unsafe { &mut *owner }.entries.push((start, end));
}

/// Rebuild from all supplied entries. May be called repeatedly.
/// # Safety
/// owner is the unique live pointer from create; no concurrent access.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_segments_build(owner: *mut Segments) -> usize {
    // SAFETY: the noncopyable C++ wrapper supplies exclusive access.
    let owner = unsafe { &mut *owner };
    owner.segments = create(&owner.entries);
    owner.segments.len()
}

/// Fields: 0=start, 1=end, 2=entry count, 3=entry index. Signed times are returned
/// as their u64 bit pattern. Invalid fields/indices return zero.
/// # Safety
/// owner is live and immutable for this call; no borrowed storage escapes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_segments_field(
    owner: *const Segments,
    segment: usize,
    field: u32,
    entry: usize,
) -> u64 {
    // SAFETY: the wrapper retains the owner without mutation during queries.
    let Some(segment) = (unsafe { &*owner }).segments.get(segment) else {
        return 0;
    };
    match field {
        0 => segment.start as u64,
        1 => segment.end as u64,
        2 => segment.entries.len() as u64,
        3 => segment.entries.get(entry).copied().unwrap_or(0) as u64,
        _ => 0,
    }
}

/// # Safety
/// Release the unique live pointer from create exactly once, after all calls.
/// Null is accepted. Native code never frees Rust allocations.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_segments_release(owner: *mut Segments) {
    if !owner.is_null() {
        // SAFETY: ownership transfers back to Rust with no outstanding borrows.
        drop(unsafe { Box::from_raw(owner) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lifetime_rebuild_and_signed_time_bits() {
        let owner = xy_vsfilter_segments_create();
        // SAFETY: one live owner, sequential calls, released exactly once.
        unsafe {
            assert_eq!(xy_vsfilter_segments_build(owner), 0);
            xy_vsfilter_segments_add(owner, i64::MIN, i64::MAX);
            assert_eq!(xy_vsfilter_segments_build(owner), 1);
            assert_eq!(xy_vsfilter_segments_field(owner, 0, 0, 0), 1 << 63);
            assert_eq!(xy_vsfilter_segments_field(owner, 0, 1, 0), i64::MAX as u64);
            xy_vsfilter_segments_add(owner, -1, 1);
            assert_eq!(xy_vsfilter_segments_build(owner), 3);
            assert_eq!(xy_vsfilter_segments_field(owner, 1, 2, 0), 2);
            assert_eq!(xy_vsfilter_segments_field(owner, 1, 3, 1), 1);
            assert_eq!(xy_vsfilter_segments_field(owner, usize::MAX, 0, 0), 0);
            assert_eq!(xy_vsfilter_segments_field(owner, 1, 3, usize::MAX), 0);
            xy_vsfilter_segments_release(owner);
            xy_vsfilter_segments_release(core::ptr::null_mut());
        }
    }
}
