// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::layout_rects::Records;
#[no_mangle]
pub extern "C" fn xy_vsfilter_layout_create() -> *mut Records {
    Box::into_raw(Box::default())
}
/// # Safety
/// owner is live and exclusive; input spans seven initialized aligned i32 values,
/// immutable during this call and disjoint from owner.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_layout_add(owner: *mut Records, input: *const i32) {
    // SAFETY: fixed initialized native record, copied before return.
    let record = unsafe { *(input.cast::<[i32; 7]>()) };
    // SAFETY: unique owner, disjoint from the input record.
    let state = unsafe { &mut *owner };
    state.add(record);
}
/// # Safety
/// owner is live and exclusive throughout this operation.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_layout_edit(
    owner: *mut Records,
    action: u32,
    index: usize,
    value: i32,
) {
    // SAFETY: no native callback or input loan overlaps this mutation.
    let state = unsafe { &mut *owner };
    match action {
        0 => state.clear(),
        1 => state.remove(index),
        2 => state.segment(index, value),
        _ => {}
    }
}
/// # Safety
/// owner is live and immutable throughout this query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_layout_count(owner: *const Records) -> usize {
    // SAFETY: query precedes release and cannot overlap mutation.
    let state = unsafe { &*owner };
    state.count()
}
/// # Safety
/// owner is live and immutable throughout this query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_layout_field(
    owner: *const Records,
    index: usize,
    field: u32,
) -> i32 {
    // SAFETY: immutable scalar query; no pointer into storage escapes.
    let state = unsafe { &*owner };
    state.field(index, field as usize)
}
/// # Safety
/// owner is live and immutable throughout this query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_layout_find(
    owner: *const Records,
    segment: i32,
    entry: i32,
) -> usize {
    // SAFETY: immutable scalar query, with usize::MAX as the missing sentinel.
    let state = unsafe { &*owner };
    state.find(segment, entry).unwrap_or(usize::MAX)
}
/// # Safety
/// Release the unique create result once, after all calls finish.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_layout_release(owner: *mut Records) {
    if !owner.is_null() {
        // SAFETY: native owner returns its unique allocation after all calls.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn records_are_copied_before_native_geometry_and_remain_owned() {
        let owner = xy_vsfilter_layout_create();
        let mut record = [1, 2, 3, 4, 5, 6, 7];
        // SAFETY: initialized record, exclusive owner, queries before release.
        unsafe {
            xy_vsfilter_layout_add(owner, record.as_ptr());
            record.fill(9);
            assert_eq!(xy_vsfilter_layout_field(owner, 0, 0), 1);
            assert_eq!(xy_vsfilter_layout_find(owner, 5, 6), 0);
            xy_vsfilter_layout_edit(owner, 2, 0, 8);
            assert_eq!(xy_vsfilter_layout_find(owner, 5, 6), usize::MAX);
            assert_eq!(xy_vsfilter_layout_find(owner, 8, 6), 0);
            xy_vsfilter_layout_edit(owner, 1, 0, 0);
            assert_eq!(xy_vsfilter_layout_count(owner), 0);
            xy_vsfilter_layout_add(owner, record.as_ptr());
            xy_vsfilter_layout_edit(owner, 0, 0, 0);
            assert_eq!(xy_vsfilter_layout_count(owner), 0);
            xy_vsfilter_layout_release(owner);
        }
    }
}
