// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::ass_style::Style;
/// # Safety
/// Nonempty input spans length initialized aligned u16 values immutable during
/// this call; length*2 fits isize. No input borrow escapes creation.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_style_create(
    input: *const u16,
    length: u32,
    version: i32,
) -> *mut Style {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: initialized immutable input span supplied by native adapter.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    Box::into_raw(Box::new(Style::new(input, version)))
}
/// # Safety
/// owner is live and uniquely accessible throughout this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_style_next(owner: *mut Style) -> u32 {
    // SAFETY: noncopyable adapter retains exclusive ownership.
    let state = unsafe { &mut *owner };
    state.next_field()
}
/// # Safety
/// owner is live and exclusive; input spans length initialized aligned u16 values,
/// immutable and disjoint from owner; length*2 fits isize. The loan ends on return.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_style_accept(
    owner: *mut Style,
    input: *const u16,
    length: u32,
) {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: fresh native remainder, copied before this call returns.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    // SAFETY: uniquely owned state, disjoint from input.
    let state = unsafe { &mut *owner };
    state.accept(input);
}
/// # Safety
/// owner is live and exclusive; colors spans four initialized aligned u32 values,
/// immutable during this call and disjoint from owner.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_style_normalize(
    owner: *mut Style,
    colors: *const u32,
    alpha: i32,
    encoding: i32,
    border: i32,
    alignment: i32,
) {
    // SAFETY: native adapter supplies four initialized color values.
    let colors = unsafe { *colors.cast::<[u32; 4]>() };
    // SAFETY: uniquely owned state with no overlapping native callback.
    let state = unsafe { &mut *owner };
    state.normalize(colors, alpha, encoding, border, alignment);
}
/// # Safety
/// owner is live and immutable throughout this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_style_field(
    owner: *const Style,
    field: u32,
    index: u32,
) -> u32 {
    // SAFETY: the adapter queries before releasing ownership.
    let state = unsafe { &*owner };
    match field {
        0 => state.input().len() as u32,
        1 => u32::from(state.input().get(index as usize).copied().unwrap_or(0)),
        2 => state.number(index as usize),
        _ => 0,
    }
}
/// # Safety
/// Release the unique create result once, after all calls have completed.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_style_release(owner: *mut Style) {
    if !owner.is_null() {
        // SAFETY: the RAII adapter returns unique ownership once.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn owned_remainder_and_colors_do_not_retain_native_loans() {
        let mut input = [65u16, 0, 66];
        // SAFETY: initialized disjoint loans, live owner, unique release.
        unsafe {
            let owner = xy_vsfilter_ass_style_create(input.as_ptr(), 3, 4);
            input.fill(7);
            assert_eq!(xy_vsfilter_ass_style_field(owner, 1, 2), 66);
            assert_eq!(xy_vsfilter_ass_style_next(owner), 1);
            xy_vsfilter_ass_style_accept(owner, input.as_ptr(), 3);
            input.fill(9);
            assert_eq!(xy_vsfilter_ass_style_field(owner, 1, 2), 7);
            let mut colors = [1u32, 2, 3, 4];
            xy_vsfilter_ass_style_normalize(owner, colors.as_ptr(), 999, -1, 3, -1);
            colors.fill(8);
            assert_eq!(xy_vsfilter_ass_style_field(owner, 2, 2), 4);
            assert_eq!(xy_vsfilter_ass_style_field(owner, 2, 4), 255);
            assert_eq!(xy_vsfilter_ass_style_field(owner, 2, 10), 9);
            assert_eq!(xy_vsfilter_ass_style_field(owner, 2, u32::MAX), 0);
            xy_vsfilter_ass_style_accept(owner, core::ptr::null(), 0);
            assert_eq!(xy_vsfilter_ass_style_field(owner, 0, 0), 0);
            xy_vsfilter_ass_style_release(owner);
        }
    }
}
