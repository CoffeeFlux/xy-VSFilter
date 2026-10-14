// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::edge_storage::Storage;
#[no_mangle]
pub extern "C" fn xy_vsfilter_edges_create(height: i32, capacity: u32) -> *mut Storage {
    Storage::new(height, capacity).map_or(core::ptr::null_mut(), |s| Box::into_raw(Box::new(s)))
}
/// # Safety
/// owner is live and exclusive for this scalar mutation.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_edges_insert(
    owner: *mut Storage,
    index: u32,
    row: i32,
    position: i32,
) -> u32 {
    // SAFETY: exclusive owner, scalar inputs, no native callback or loan.
    u32::from(unsafe { &mut *owner }.insert(index, row, position))
}
/// # Safety
/// owner is live and exclusive for this capacity change.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_edges_resize(owner: *mut Storage, capacity: u32) -> u32 {
    // SAFETY: no raw storage pointers escape or overlap this resize.
    u32::from(unsafe { &mut *owner }.resize(capacity))
}
/// # Safety
/// owner is live and immutable until this scalar query returns.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_edges_query(
    owner: *const Storage,
    field: u32,
    index: usize,
) -> i64 {
    // SAFETY: immutable query precedes release and cannot overlap mutation.
    unsafe { &*owner }.query(field, index).unwrap_or(i64::MIN)
}
/// # Safety
/// owner is null or the unique create result, released once after all calls.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_edges_release(owner: *mut Storage) {
    if !owner.is_null() {
        // SAFETY: native owner returns its unique allocation exactly once.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn scalar_link_edits_survive_growth_without_storage_loans() {
        let owner = xy_vsfilter_edges_create(1, 2);
        assert!(!owner.is_null());
        // SAFETY: exclusive live owner and scalar calls, release last.
        unsafe {
            assert_eq!(xy_vsfilter_edges_insert(owner, 1, 0, i32::MIN), 1);
            assert_eq!(xy_vsfilter_edges_resize(owner, 4), 1);
            assert_eq!(xy_vsfilter_edges_insert(owner, 2, 0, i32::MAX), 1);
            assert_eq!(xy_vsfilter_edges_query(owner, 0, 0), 2);
            assert_eq!(xy_vsfilter_edges_query(owner, 1, 2), 1);
            assert_eq!(xy_vsfilter_edges_query(owner, 2, 1), i64::from(i32::MIN));
            assert_eq!(xy_vsfilter_edges_query(owner, 2, 0), i64::MIN);
            xy_vsfilter_edges_release(owner);
        }
    }
}
