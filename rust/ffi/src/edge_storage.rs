// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::edge_storage::Storage;
use xy_vsfilter_compat::scan_row::Row;
/// # Safety
/// owner is live and exclusive for the entire operation. output spans eight
/// writable bytes, does not overlap owner storage, and may be unaligned. No
/// pointer is retained. Status 3 is unresolved shape; 5 is allocation failure.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_edges_line_v1(
    owner: *mut Storage,
    next: u32,
    x0: i32,
    y0: i32,
    x1: i32,
    y1: i32,
    up: u32,
    output: *mut u8,
) -> u32 {
    if owner.is_null() || output.is_null() || up > 1 {
        return 3;
    }
    use xy_vsfilter_compat::path_align::Point;
    // SAFETY: caller's exclusive live owner; mutation uses checked safe indices.
    let result = unsafe { &mut *owner }.line(
        next,
        Point { x: x0, y: y0 },
        Point { x: x1, y: y1 },
        up != 0,
    );
    match result {
        Ok(values) => {
            let mut bytes = [0; 8];
            bytes[..4].copy_from_slice(&values[0].to_le_bytes());
            bytes[4..].copy_from_slice(&values[1].to_le_bytes());
            // SAFETY: detached initialized scalar packet and writable output.
            unsafe {
                core::ptr::copy_nonoverlapping(bytes.as_ptr(), output, 8);
            }
            0
        }
        Err(status) => status,
    }
}
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
    fn line_output_accepts_unaligned_bytes_and_failure_leaves_it_untouched() {
        let owner = xy_vsfilter_edges_create(2, 2);
        assert!(!owner.is_null());
        let mut out = [0xa5; 10];
        // SAFETY: unique initialized owner and disjoint writable unaligned output.
        unsafe {
            assert_eq!(
                xy_vsfilter_edges_line_v1(owner, 1, 0, 0, 16, 16, 1, out.as_mut_ptr().add(1)),
                0
            );
            assert_eq!(&out[1..9], &[3, 0, 0, 0, 4, 0, 0, 0]);
            out.fill(0xa5);
            assert_eq!(
                xy_vsfilter_edges_line_v1(owner, 3, 0, -16, 16, 16, 1, out.as_mut_ptr().add(1)),
                3
            );
            assert_eq!(out, [0xa5; 10]);
            assert_eq!(xy_vsfilter_edges_query(owner, 0, 1), 2);
            xy_vsfilter_edges_release(owner);
        }
    }
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
