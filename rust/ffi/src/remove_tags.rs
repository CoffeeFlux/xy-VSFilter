// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::remove_tags::Remover;
/// # Safety
/// Nonempty input spans length initialized, aligned u16 values; length*2 fits
/// isize. This call retains no borrow. Null return selects the native NUL path.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_remove_tags_create(
    input: *const u16,
    length: u32,
) -> *mut Remover {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: native adapter supplies initialized input for this call.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    Remover::new(input).map_or(core::ptr::null_mut(), |state| {
        Box::into_raw(Box::new(state))
    })
}
/// # Safety
/// owner is live and uniquely accessible; reply is a valid native search result
/// on the current text, as requested by the previous step. No native loan lives
/// through this mutation of the owned text.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_remove_tags_step(owner: *mut Remover, reply: i32) -> u32 {
    // SAFETY: noncopyable adapter provides unique access between field queries.
    let state = unsafe { &mut *owner };
    state.step(reply)
}
/// # Safety
/// owner is live and immutable throughout this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_remove_tags_field(
    owner: *const Remover,
    field: u32,
    index: u32,
) -> i32 {
    // SAFETY: the native adapter releases ownership after all queries finish.
    let state = unsafe { &*owner };
    match field {
        0 => state.position,
        1 => state.text.len() as i32,
        2 => i32::from(state.text.get(index as usize).copied().unwrap_or(0)),
        _ => 0,
    }
}
/// # Safety
/// Return the unique create result exactly once, after all calls finish.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_remove_tags_release(owner: *mut Remover) {
    if !owner.is_null() {
        // SAFETY: native adapter returns unique ownership.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn text_outlives_input_and_queries_remain_bounded() {
        let mut input = [123u16, 125, 0xd800];
        // SAFETY: input is initialized, replies come from searches on the owned
        // text, and the live owner is uniquely released after bounded queries.
        unsafe {
            let owner = xy_vsfilter_remove_tags_create(input.as_ptr(), 3);
            assert!(!owner.is_null());
            input.fill(0);
            assert_eq!(xy_vsfilter_remove_tags_step(owner, 0), 1);
            assert_eq!(xy_vsfilter_remove_tags_step(owner, 0), 2);
            assert_eq!(xy_vsfilter_remove_tags_step(owner, 1), 1);
            assert_eq!(xy_vsfilter_remove_tags_field(owner, 1, 0), 1);
            assert_eq!(xy_vsfilter_remove_tags_field(owner, 2, 0), 0xd800);
            assert_eq!(xy_vsfilter_remove_tags_field(owner, 2, u32::MAX), 0);
            assert_eq!(xy_vsfilter_remove_tags_step(owner, -1), 0);
            xy_vsfilter_remove_tags_release(owner);
            assert!(xy_vsfilter_remove_tags_create(input.as_ptr(), 3).is_null());
        }
    }
}
