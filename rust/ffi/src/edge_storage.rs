// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::edge_storage::Storage;
use xy_vsfilter_compat::scan_row::Row;
/// # Safety
/// owner is live and immutable during construction; no storage pointer escapes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_edges_spans(owner: *const Storage, row: i32) -> *mut Row {
    // SAFETY: the native owner outlives this immutable scalar query.
    unsafe { &*owner }
        .spans(row)
        .map_or(core::ptr::null_mut(), |s| Box::into_raw(Box::new(s)))
}
/// # Safety
/// row is a live spans result and immutable until the query returns.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_row_field(row: *const Row, field: u32, index: usize) -> u64 {
    // SAFETY: queries finish before release; absent fields have a separate sentinel.
    unsafe { &*row }.field(field, index).unwrap_or(u64::MAX)
}
/// # Safety
/// row is null or the unique spans result, released exactly once.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_row_release(row: *mut Row) {
    if !row.is_null() {
        // SAFETY: the native RAII owner returns its unique row allocation.
        drop(unsafe { Box::from_raw(row) });
    }
}
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
    fn row_result_is_independent_of_edge_owner_mutations_and_release() {
        let owner = xy_vsfilter_edges_create(1, 3);
        assert!(!owner.is_null());
        // SAFETY: exclusive edits, immutable query, then independent releases.
        unsafe {
            assert_eq!(xy_vsfilter_edges_insert(owner, 1, 0, 1), 1);
            assert_eq!(xy_vsfilter_edges_insert(owner, 2, 0, 4), 1);
            let row = xy_vsfilter_edges_spans(owner, 0);
            assert!(!row.is_null());
            assert!(xy_vsfilter_edges_spans(owner, 1).is_null());
            assert_eq!(xy_vsfilter_edges_resize(owner, 8), 1);
            xy_vsfilter_edges_release(owner);
            assert_eq!(xy_vsfilter_row_field(row, 0, 0), 1);
            assert_eq!(xy_vsfilter_row_field(row, 1, 0), 0x4000_0000_4000_0000);
            assert_eq!(xy_vsfilter_row_field(row, 2, 0), 0x4000_0000_4000_0002);
            assert_eq!(xy_vsfilter_row_field(row, 1, 1), u64::MAX);
            xy_vsfilter_row_release(row);
        }
    }
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
