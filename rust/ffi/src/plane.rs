// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::plane::Plane;
#[no_mangle]
pub extern "C" fn xy_vsfilter_plane_create(count: usize) -> *mut Plane {
    Plane::new(count).map_or(core::ptr::null_mut(), |p| Box::into_raw(Box::new(p)))
}
/// # Safety
/// owner is live and exclusive. After this loan, native access is confined to
/// the requested create count; initialization precedes reads. No Rust owner
/// access occurs until all native views have ended and release is called.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_plane_loan(owner: *mut Plane) -> *mut u8 {
    // SAFETY: one exclusive loan; Block is a contiguous aligned array of bytes
    // whose uninitialized state is represented by MaybeUninit in the safe core.
    unsafe { &mut *owner }.blocks_mut().as_mut_ptr().cast()
}
/// # Safety
/// owner is null or a unique create result, returned once after all native
/// aliases and FFI plane operations have ceased.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_plane_release(owner: *mut Plane) {
    if !owner.is_null() {
        // SAFETY: last native shared reference releases its unique Rust owner.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn loan_survives_scalar_pixel_calls_until_the_last_native_alias_ends() {
        for count in [1, 15, 16, 17, 129] {
            let owner = xy_vsfilter_plane_create(count);
            assert!(!owner.is_null());
            // SAFETY: one exclusive loan, initialized before reads, no owner
            // borrow while data or aliases are used, and release exactly once.
            unsafe {
                let data = xy_vsfilter_plane_loan(owner);
                assert_eq!(data as usize % 16, 0);
                core::ptr::write_bytes(data, 250, count);
                let alias = data;
                assert_eq!(
                    crate::span_coverage::xy_vsfilter_span_coverage(data, count, 1, 0, 0, 8),
                    1
                );
                assert_eq!(*alias, 2);
                assert_eq!(*alias.add(count - 1), if count == 1 { 2 } else { 250 });
                xy_vsfilter_plane_release(owner);
            }
        }
    }
}
