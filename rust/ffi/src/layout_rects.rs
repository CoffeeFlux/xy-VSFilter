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
/// owner is live and exclusive; nonempty active spans count initialized aligned
/// i32 values, immutable and disjoint from owner; count*4 fits isize.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_layout_advance(
    owner: *mut Records,
    segment: i32,
    active: *const i32,
    count: usize,
) -> u32 {
    let active = if count == 0 {
        &[]
    } else {
        // SAFETY: native active-entry copy remains immutable until return.
        unsafe { core::slice::from_raw_parts(active, count) }
    };
    // SAFETY: owner is exclusive and disjoint from active entries.
    let state = unsafe { &mut *owner };
    u32::from(state.advance(segment, active))
}
/// # Safety
/// owner is live and immutable; rect spans four initialized aligned i32 values
/// writable and disjoint from owner. A decline leaves all four values unchanged.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_layout_place(
    owner: *const Records,
    rect: *mut i32,
    layer: i32,
    down: u32,
) -> u32 {
    // SAFETY: fixed initialized native rectangle, copied before computation.
    let input = unsafe { *(rect.cast::<[i32; 4]>()) };
    // SAFETY: immutable owner and disjoint rectangle storage.
    let state = unsafe { &*owner };
    if let Some(output) = state.place(input, layer, down != 0) {
        // SAFETY: exclusive writable fixed array; no owner borrow overlaps it.
        unsafe {
            *(rect.cast::<[i32; 4]>()) = output;
        }
        1
    } else {
        0
    }
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
    #[test]
    fn placement_decline_keeps_native_rectangle_and_records_intact() {
        let owner = xy_vsfilter_layout_create();
        let record = [0, 0, 10, i32::MAX, 0, 1, 0];
        let mut rect = [0, 0, 10, 10];
        let active = [1];
        // SAFETY: initialized disjoint arrays and exclusive live owner.
        unsafe {
            xy_vsfilter_layout_add(owner, record.as_ptr());
            assert_eq!(xy_vsfilter_layout_place(owner, rect.as_mut_ptr(), 0, 1), 0);
            assert_eq!(rect, [0, 0, 10, 10]);
            assert_eq!(xy_vsfilter_layout_advance(owner, 1, active.as_ptr(), 1), 1);
            assert_eq!(xy_vsfilter_layout_field(owner, 0, 4), 1);
            assert_eq!(
                xy_vsfilter_layout_advance(owner, i32::MIN, active.as_ptr(), 1),
                0
            );
            assert_eq!(xy_vsfilter_layout_field(owner, 0, 4), 1);
            xy_vsfilter_layout_release(owner);
        }
    }
}
