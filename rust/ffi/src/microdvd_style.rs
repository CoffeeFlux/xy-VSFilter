// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::microdvd_style::Tags;
/// # Safety
/// Nonempty input spans length initialized aligned u16 values immutable during
/// this call; length*2 fits isize. No borrow escapes creation.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_microdvd_style_create(
    input: *const u16,
    length: u32,
) -> *mut Tags {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: native adapter supplies initialized immutable code units.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    Box::into_raw(Box::new(Tags::new(input)))
}
/// # Safety
/// owner is live and exclusively accessible throughout this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_microdvd_style_next(owner: *mut Tags) -> u32 {
    // SAFETY: noncopyable native adapter owns this state exclusively.
    let state = unsafe { &mut *owner };
    u32::from(state.advance())
}
/// # Safety
/// owner is live and immutable throughout this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_microdvd_style_field(
    owner: *const Tags,
    field: u32,
    index: u32,
) -> u32 {
    // SAFETY: native scalar queries precede unique release.
    let state = unsafe { &*owner };
    match field {
        0 => state.code().len() as u32,
        1 => u32::from(state.code().get(index as usize).copied().unwrap_or(0)),
        _ => 0,
    }
}
/// # Safety
/// Return the unique create result once, after all calls finish.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_microdvd_style_release(owner: *mut Tags) {
    if !owner.is_null() {
        // SAFETY: native RAII adapter returns unique ownership once.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn owned_tag_survives_input_changes_and_queries_are_bounded() {
        let mut input = [123u16, 102, 58, 0xd800];
        // SAFETY: initialized input, live owner, exclusive calls, unique release.
        unsafe {
            let owner = xy_vsfilter_microdvd_style_create(input.as_ptr(), 4);
            input.fill(7);
            assert_eq!(xy_vsfilter_microdvd_style_next(owner), 1);
            assert_eq!(xy_vsfilter_microdvd_style_field(owner, 0, 0), 4);
            assert_eq!(xy_vsfilter_microdvd_style_field(owner, 1, 3), 0xd800);
            assert_eq!(xy_vsfilter_microdvd_style_field(owner, 1, u32::MAX), 0);
            assert_eq!(xy_vsfilter_microdvd_style_next(owner), 0);
            xy_vsfilter_microdvd_style_release(owner);
        }
    }
}
