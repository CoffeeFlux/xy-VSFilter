// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::ellipse_arc::{self, Arc};
#[no_mangle]
pub extern "C" fn xy_vsfilter_ellipse_create(rx: i32, ry: i32) -> *mut Arc {
    ellipse_arc::generate(rx, ry).map_or(core::ptr::null_mut(), |a| Box::into_raw(Box::new(a)))
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_ellipse_owned_create(rx: i32, ry: i32) -> *mut Arc {
    ellipse_arc::generate(rx, ry)
        .filter(Arc::is_complete)
        .map_or(core::ptr::null_mut(), |a| Box::into_raw(Box::new(a)))
}
/// # Safety
/// owner is live and immutable until this scalar query returns.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ellipse_cell(owner: *const Arc, index: usize) -> i64 {
    // SAFETY: immutable owner, no pointer or storage loan escapes.
    unsafe { &*owner }.cell(index).map_or(i64::MIN, i64::from)
}
/// # Safety
/// owner is null or the unique result of create, released once after all queries.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ellipse_release(owner: *mut Arc) {
    if !owner.is_null() {
        // SAFETY: native RAII owner relinquishes its unique allocation.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn persistent_table_owners_are_independent() {
        let first = xy_vsfilter_ellipse_owned_create(2, 2);
        let second = xy_vsfilter_ellipse_owned_create(0, 2);
        assert!(!first.is_null() && !second.is_null());
        // SAFETY: each owner remains immutable until its unique release.
        unsafe {
            assert_eq!(xy_vsfilter_ellipse_cell(first, 2), -2);
            assert_eq!(xy_vsfilter_ellipse_cell(second, 2), 0);
            xy_vsfilter_ellipse_release(first);
            assert_eq!(xy_vsfilter_ellipse_cell(second, 2), 0);
            xy_vsfilter_ellipse_release(second);
        }
        assert!(xy_vsfilter_ellipse_owned_create(i32::MAX, 2).is_null());
    }
    #[test]
    fn cells_are_copied_and_unwritten_sentinel_is_outside_i32() {
        let owner = xy_vsfilter_ellipse_create(0, 1);
        assert!(!owner.is_null());
        // SAFETY: unique owner remains live during all scalar queries.
        unsafe {
            assert_eq!(xy_vsfilter_ellipse_cell(owner, 0), 0);
            assert_eq!(xy_vsfilter_ellipse_cell(owner, 3), i64::MIN);
            xy_vsfilter_ellipse_release(owner);
            xy_vsfilter_ellipse_release(core::ptr::null_mut());
        }
        assert!(xy_vsfilter_ellipse_create(-1, 1).is_null());
    }
}
