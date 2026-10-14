// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::font_records::Records;
#[no_mangle]
pub extern "C" fn xy_vsfilter_font_records_create() -> *mut Records {
    Box::into_raw(Box::default())
}
/// # Safety
/// Nonempty input spans length initialized aligned u16 values, immutable during
/// this call; length*2 fits isize. No borrow escapes the classification.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_font_records_classify(input: *const u16, length: u32) -> u32 {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: native adapter supplies an initialized immutable span.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    Records::classify(input)
}
/// # Safety
/// owner is live and exclusive; input spans length initialized aligned u16
/// values, immutable and disjoint from owner; length*2 fits isize.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_font_records_append(
    owner: *mut Records,
    input: *const u16,
    length: u32,
) {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: freshly captured native fragment, copied before returning.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    // SAFETY: uniquely owned state is disjoint from the fragment loan.
    let state = unsafe { &mut *owner };
    state.append(input);
}
/// # Safety
/// owner is live and uniquely accessible throughout this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_font_records_clear(owner: *mut Records) {
    // SAFETY: native RAII adapter retains unique ownership.
    let state = unsafe { &mut *owner };
    state.clear();
}
/// # Safety
/// owner is live and immutable throughout this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_font_records_field(
    owner: *const Records,
    field: u32,
    index: u32,
) -> u32 {
    // SAFETY: scalar queries complete before owner release.
    let state = unsafe { &*owner };
    match field {
        0 => state.text().len() as u32,
        1 => u32::from(state.text().get(index as usize).copied().unwrap_or(0)),
        _ => 0,
    }
}
/// # Safety
/// Release the unique create result once, after all calls complete.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_font_records_release(owner: *mut Records) {
    if !owner.is_null() {
        // SAFETY: native adapter returns unique ownership once.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn record_retains_no_native_fragment_borrow() {
        let owner = xy_vsfilter_font_records_create();
        let mut input = [65u16, 0, 0xd800];
        // SAFETY: initialized disjoint loans; unique owner released once.
        unsafe {
            assert_eq!(xy_vsfilter_font_records_classify(input.as_ptr(), 3), 2);
            xy_vsfilter_font_records_append(owner, input.as_ptr(), 3);
            input.fill(7);
            assert_eq!(xy_vsfilter_font_records_field(owner, 0, 0), 3);
            assert_eq!(xy_vsfilter_font_records_field(owner, 1, 2), 0xd800);
            assert_eq!(xy_vsfilter_font_records_field(owner, 1, u32::MAX), 0);
            xy_vsfilter_font_records_clear(owner);
            assert_eq!(xy_vsfilter_font_records_field(owner, 0, 0), 0);
            assert_eq!(xy_vsfilter_font_records_classify(core::ptr::null(), 0), 0);
            xy_vsfilter_font_records_release(owner);
        }
    }
}
