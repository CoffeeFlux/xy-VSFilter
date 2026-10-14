// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::ellipse_matrix::Matrix;
#[no_mangle]
pub extern "C" fn xy_vsfilter_ellipse_matrix_create(rx: i32, ry: i32) -> *mut Matrix {
    Matrix::new(rx, ry).map_or(core::ptr::null_mut(), |matrix| {
        Box::into_raw(Box::new(matrix))
    })
}
/// # Safety
/// owner is live and exclusive for this scalar edit; no native callback occurs.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ellipse_matrix_set(
    owner: *mut Matrix,
    dy: i32,
    dx: i32,
    value: i32,
) -> u32 {
    // SAFETY: only the unique native owner can edit the table, without loans.
    u32::from(unsafe { &mut *owner }.set(dy, dx, value))
}
/// # Safety
/// owner is live and immutable for the duration of this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ellipse_matrix_get(
    owner: *const Matrix,
    dy: i32,
    dx: i32,
) -> i64 {
    // SAFETY: no storage pointer escapes or overlaps mutation/release.
    unsafe { &*owner }.get(dy, dx).map_or(i64::MIN, i64::from)
}
/// # Safety
/// owner is null or the unique create result, released once after all queries.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ellipse_matrix_release(owner: *mut Matrix) {
    if !owner.is_null() {
        // SAFETY: native RAII transfers its unique allocation back to Rust.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn scalar_writes_preserve_absent_cells_and_native_sentinels() {
        let owner = xy_vsfilter_ellipse_matrix_create(1, 1);
        assert!(!owner.is_null());
        // SAFETY: exclusive owner, scalar calls, unique release last.
        unsafe {
            assert_eq!(xy_vsfilter_ellipse_matrix_get(owner, 0, 0), i64::MIN);
            assert_eq!(xy_vsfilter_ellipse_matrix_set(owner, -1, 0, i32::MIN), 1);
            assert_eq!(
                xy_vsfilter_ellipse_matrix_get(owner, -1, 0),
                i64::from(i32::MIN)
            );
            assert_eq!(xy_vsfilter_ellipse_matrix_set(owner, -3, 0, 12), 0);
            assert_eq!(
                xy_vsfilter_ellipse_matrix_get(owner, -1, 0),
                i64::from(i32::MIN)
            );
            xy_vsfilter_ellipse_matrix_release(owner);
            xy_vsfilter_ellipse_matrix_release(core::ptr::null_mut());
        }
    }
}

use xy_vsfilter_compat::ellipse_matrix::Scratch;
#[no_mangle]
pub extern "C" fn xy_vsfilter_ellipse_scratch_create(ry: i32) -> *mut Scratch {
    Scratch::new(ry).map_or(core::ptr::null_mut(), |s| Box::into_raw(Box::new(s)))
}
/// # Safety
/// owner is a live exclusive scratch owner, without outstanding storage loans.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ellipse_scratch_set(
    owner: *mut Scratch,
    y: i32,
    value: i32,
) -> u32 {
    // SAFETY: unique scalar mutation, no callback or storage loan.
    u32::from(unsafe { &mut *owner }.set(y, value))
}
/// # Safety
/// owner is live and immutable for this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ellipse_scratch_get(owner: *const Scratch, y: i32) -> i64 {
    // SAFETY: immutable query ends before any edit or release.
    unsafe { &*owner }.get(y).map_or(i64::MIN, i64::from)
}
/// # Safety
/// owner is null or the unique create result, released once after all calls.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ellipse_scratch_release(owner: *mut Scratch) {
    if !owner.is_null() {
        // SAFETY: native scope relinquishes its unique allocation.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod scratch_tests {
    use super::*;
    #[test]
    fn native_scalars_do_not_borrow_scratch_storage() {
        let owner = xy_vsfilter_ellipse_scratch_create(1);
        assert!(!owner.is_null());
        // SAFETY: unique live owner and ordered scalar calls before release.
        unsafe {
            assert_eq!(xy_vsfilter_ellipse_scratch_get(owner, -2), 0);
            assert_eq!(xy_vsfilter_ellipse_scratch_set(owner, -2, i32::MIN), 1);
            let copy = xy_vsfilter_ellipse_scratch_get(owner, -2);
            assert_eq!(xy_vsfilter_ellipse_scratch_set(owner, -2, 42), 1);
            assert_eq!(copy, i64::from(i32::MIN));
            assert_eq!(xy_vsfilter_ellipse_scratch_get(owner, -2), 42);
            assert_eq!(xy_vsfilter_ellipse_scratch_set(owner, -3, 0), 0);
            xy_vsfilter_ellipse_scratch_release(owner);
        }
    }
}
