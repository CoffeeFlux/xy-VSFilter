// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::arc_queue::{Item, Queue};
#[no_mangle]
pub extern "C" fn xy_vsfilter_arc_create(capacity: usize) -> *mut Queue {
    Queue::new(capacity).map_or(core::ptr::null_mut(), |q| Box::into_raw(Box::new(q)))
}
/// # Safety
/// owner is null or a live immutable queue for the duration of this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_arc_copy(owner: *const Queue) -> *mut Queue {
    if owner.is_null() {
        return core::ptr::null_mut();
    }
    // SAFETY: caller keeps the source immutable and live through this copy.
    Box::into_raw(Box::new(unsafe { &*owner }.clone()))
}
/// # Safety
/// owner is a live immutable queue. No pointer into its storage escapes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_arc_query(
    owner: *const Queue,
    field: u32,
    index: usize,
) -> u64 {
    // SAFETY: immutable native query ends before any edit or release.
    let q = unsafe { &*owner };
    match field {
        0 => q.capacity() as u64,
        1 => q.len() as u64,
        2 => u64::from(q.is_empty()),
        3 => u64::from(q.full()),
        4 => q.free() as u64,
        5 => q.at(index).center,
        6 => q.at(index).deadline as u32 as u64,
        7 => q.back().center,
        8 => q.back().deadline as u32 as u64,
        _ => 0,
    }
}
/// # Safety
/// owner is a live exclusively owned queue, with no outstanding loans.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_arc_edit(
    owner: *mut Queue,
    action: u32,
    index: usize,
    center: u64,
    deadline: i32,
) -> i32 {
    // SAFETY: mutation has scalar arguments and invokes no native callbacks.
    let q = unsafe { &mut *owner };
    match action {
        0 => q.append(Item { center, deadline }),
        1 => {
            return if q.push(Item { center, deadline }) {
                0
            } else {
                -1
            }
        }
        2 => q.pop_front(),
        3 => q.pop_back(),
        4 => q.pop_last(index),
        5 => q.center(index, center),
        6 => q.deadline(index, deadline),
        _ => {}
    }
    0
}
/// # Safety
/// owner is null or the unique allocation from create/copy, released once after
/// all other operations finish.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_arc_release(owner: *mut Queue) {
    if !owner.is_null() {
        // SAFETY: the native owner returns its unique allocation exactly once.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn independent_owners_and_scalar_queries() {
        let q = xy_vsfilter_arc_create(4);
        // SAFETY: unique live owners; all arguments are scalars, release last.
        unsafe {
            xy_vsfilter_arc_edit(q, 0, 0, u64::MAX, i32::MIN);
            let copy = xy_vsfilter_arc_copy(q);
            xy_vsfilter_arc_edit(q, 5, 0, 7, 0);
            assert_eq!(xy_vsfilter_arc_query(copy, 5, 0), u64::MAX);
            assert_eq!(xy_vsfilter_arc_query(q, 5, 0), 7);
            assert_eq!(xy_vsfilter_arc_query(copy, 6, 0), i32::MIN as u32 as u64);
            xy_vsfilter_arc_release(q);
            xy_vsfilter_arc_release(copy);
            assert!(xy_vsfilter_arc_copy(core::ptr::null()).is_null());
            xy_vsfilter_arc_release(core::ptr::null_mut());
        }
        assert!(xy_vsfilter_arc_create(0).is_null());
    }
}
