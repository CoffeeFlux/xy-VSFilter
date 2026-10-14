// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::ass_headers::Headers;
#[no_mangle]
pub extern "C" fn xy_vsfilter_ass_headers_create() -> *mut Headers {
    Box::into_raw(Box::default())
}
/// # Safety
/// owner is live and exclusive; nonempty input spans length initialized aligned
/// u16 values, immutable and disjoint from owner; length*2 fits isize.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_headers_classify(
    owner: *mut Headers,
    input: *const u16,
    length: u32,
) -> u32 {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: native adapter supplies an initialized immutable span.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    // SAFETY: uniquely owned state, disjoint from input.
    let state = unsafe { &mut *owner };
    state.classify(input)
}
/// # Safety
/// owner is live and exclusively accessible during this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_headers_version(owner: *mut Headers, version: i32) {
    // SAFETY: native RAII adapter holds exclusive ownership.
    let state = unsafe { &mut *owner };
    state.script_version(version);
}
/// # Safety
/// owner is live and immutable during this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_headers_field(owner: *const Headers, field: u32) -> i32 {
    // SAFETY: scalar query completes before owner release.
    let state = unsafe { &*owner };
    match field {
        0 => state.version,
        1 => state.style_version,
        2 => i32::from(state.accepted),
        _ => 0,
    }
}
/// # Safety
/// Nonempty input spans length initialized aligned u16 values, immutable during
/// this call; length*2 fits isize. No input borrow escapes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_header_value(
    input: *const u16,
    length: u32,
    mode: u32,
) -> u32 {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: initialized immutable native input, loaned only for this call.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    xy_vsfilter_compat::ass_headers::value(input, mode)
}
/// # Safety
/// Release the unique create result once, after all operations have completed.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_headers_release(owner: *mut Headers) {
    if !owner.is_null() {
        // SAFETY: unique ownership returned once by native RAII adapter.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn native_loans_end_between_header_updates() {
        let owner = xy_vsfilter_ass_headers_create();
        let mut input: alloc::vec::Vec<u16> = "[v4 styles]\0tail".encode_utf16().collect();
        // SAFETY: initialized disjoint input, live owner and unique release.
        unsafe {
            xy_vsfilter_ass_headers_version(owner, 5);
            assert_eq!(
                xy_vsfilter_ass_headers_classify(owner, input.as_ptr(), input.len() as u32),
                12
            );
            input.fill(0);
            assert_eq!(xy_vsfilter_ass_headers_field(owner, 0), 5);
            assert_eq!(xy_vsfilter_ass_headers_field(owner, 1), 4);
            assert_eq!(xy_vsfilter_ass_headers_field(owner, 2), 1);
            assert_eq!(xy_vsfilter_ass_header_value(core::ptr::null(), 0, 2), 0);
            assert_eq!(
                xy_vsfilter_ass_headers_classify(owner, core::ptr::null(), 0),
                0
            );
            xy_vsfilter_ass_headers_release(owner);
        }
    }
}
