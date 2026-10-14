// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::find_char::Search;
#[no_mangle]
pub extern "C" fn xy_vsfilter_find_char_create(
    target: u16,
    position: i32,
    length: u32,
) -> *mut Search {
    Box::into_raw(Box::new(Search::new(target, position, length)))
}
/// # Safety
/// owner is live and uniquely accessible. Nonempty input spans length aligned,
/// initialized u16 values, immutable during this call and disjoint from owner.
/// length * 2 fits isize. The original CString length stays fixed; its NUL sentinel is implicit.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_find_char_step(
    owner: *mut Search,
    input: *const u16,
    length: u32,
    reply: u32,
) -> u32 {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: initialized native span is loaned only for this call.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    // SAFETY: noncopyable native adapter owns this state exclusively.
    let state = unsafe { &mut *owner };
    state.step(input, reply != 0)
}
/// # Safety
/// owner is live and immutable during this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_find_char_value(owner: *const Search) -> i32 {
    // SAFETY: query precedes unique release and does not overlap mutation.
    unsafe { (*owner).value }
}
/// # Safety
/// Return the unique create result exactly once, after all calls finish.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_find_char_release(owner: *mut Search) {
    if !owner.is_null() {
        // SAFETY: adapter returns ownership once.
        drop(unsafe { Box::from_raw(owner) });
    }
}
/// # Safety
/// Nonempty input spans length initialized u16 values; length*2 fits isize.
/// The input is immutable throughout this call. No borrow escapes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_find_char_unicode(
    input: *const u16,
    length: u32,
    target: u16,
    position: i32,
) -> i32 {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: the native adapter loans initialized, immutable code units.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    xy_vsfilter_compat::find_char::unicode(input, target, position)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn unicode_loan_handles_sentinel_without_reading_past_span() {
        let mut input = [0u16, 65, 0xd800];
        // SAFETY: initialized span, and an empty loan need not have storage.
        unsafe {
            assert_eq!(
                xy_vsfilter_find_char_unicode(core::ptr::null(), 0, 0, 0),
                -1
            );
            assert_eq!(xy_vsfilter_find_char_unicode(input.as_ptr(), 3, 0, 1), 3);
            input[1] = 0;
            assert_eq!(xy_vsfilter_find_char_unicode(input.as_ptr(), 3, 0, 1), 1);
        }
    }
    #[test]
    fn native_loan_ends_between_steps() {
        let owner = xy_vsfilter_find_char_create(124, 0, 2);
        let mut input = [65u16, 66];
        // SAFETY: live owner, initialized independent input, unique release.
        unsafe {
            assert_eq!(xy_vsfilter_find_char_step(owner, input.as_ptr(), 2, 0), 1);
            input[1] = 124;
            assert_eq!(xy_vsfilter_find_char_step(owner, input.as_ptr(), 2, 0), 1);
            assert_eq!(xy_vsfilter_find_char_step(owner, input.as_ptr(), 2, 0), 0);
            assert_eq!(xy_vsfilter_find_char_value(owner), 1);
            xy_vsfilter_find_char_release(owner);
        }
    }
}
