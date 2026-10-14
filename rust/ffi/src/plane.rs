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
    fn two_word_history_loans_survive_fixed_blur_phases_and_reverse_release() {
        let pixels = xy_vsfilter_plane_create(16);
        let sums = xy_vsfilter_plane_create(16);
        assert!(!pixels.is_null() && !sums.is_null());
        // SAFETY: separately allocated initialized word views, each bounded
        // filter borrow ends before the next phase and final owner release.
        unsafe {
            let p = xy_vsfilter_plane_loan(pixels).cast::<u16>();
            let s = xy_vsfilter_plane_loan(sums).cast::<u16>();
            core::ptr::write_bytes(p, 0, 8);
            core::ptr::write_bytes(s, 0, 8);
            for phase in 0..3 {
                let output = crate::be_blur::xy_vsfilter_be_blur_group(
                    0x1010_1010_1010_1010,
                    8,
                    16 | (32 << 16),
                    p,
                    s,
                    phase,
                    1,
                );
                if phase == 2 {
                    assert_eq!(output.pixels, 0x1010_1010_1010_1010);
                }
            }
            xy_vsfilter_plane_release(sums);
            xy_vsfilter_plane_release(pixels);
        }
    }
    #[test]
    fn aligned_output_loan_supports_independent_word_scratch_in_bilinear_filter() {
        let pixels = xy_vsfilter_plane_create(4);
        let columns = xy_vsfilter_plane_create(8);
        assert!(!pixels.is_null() && !columns.is_null());
        // SAFETY: disjoint live allocations, initialized byte/word views, no
        // owner access until both loans' final native/FFI accesses have ended.
        unsafe {
            let data = xy_vsfilter_plane_loan(pixels);
            let scratch = xy_vsfilter_plane_loan(columns).cast::<u16>();
            core::ptr::copy_nonoverlapping([0, 64, 128, 255].as_ptr(), data, 4);
            core::ptr::write_bytes(scratch, 0, 4);
            crate::bilinear::xy_vsfilter_bilinear_row(data, scratch, 4, 4, 4);
            assert_eq!(core::slice::from_raw_parts(data, 4), &[0, 16, 48, 96]);
            assert_eq!(
                core::slice::from_raw_parts(scratch, 4),
                &[0, 1024, 3072, 6128]
            );
            xy_vsfilter_plane_release(columns);
            xy_vsfilter_plane_release(pixels);
        }
    }
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
