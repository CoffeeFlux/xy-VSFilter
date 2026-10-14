// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::pipe_line::Line;
/// # Safety
/// Nonempty input spans length initialized aligned u16 values, immutable during
/// this call; length*2 fits isize and length does not exceed i32::MAX.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_pipe_line_create(input: *const u16, length: u32) -> *mut Line {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: the native adapter lends a fresh initialized input copy.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    Box::into_raw(Box::new(Line::new(input)))
}
/// # Safety
/// owner is live and immutable throughout this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_pipe_line_position(owner: *const Line) -> i32 {
    // SAFETY: query precedes release and cannot overlap mutation.
    let state = unsafe { &*owner };
    state.position().map_or(-1, |index| index as i32)
}
/// # Safety
/// owner is live and exclusively accessible throughout this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_pipe_line_found(owner: *mut Line, found: i32) {
    // SAFETY: no input borrow crosses the preceding native search callback.
    let state = unsafe { &mut *owner };
    state.found(found);
}
/// # Safety
/// owner is live and immutable throughout this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_pipe_line_field(
    owner: *const Line,
    field: u32,
    index: u32,
) -> u32 {
    // SAFETY: native scalar queries precede unique release.
    let state = unsafe { &*owner };
    match field {
        0 => state.text().len() as u32,
        1 => u32::from(state.text().get(index as usize).copied().unwrap_or(0)),
        _ => 0,
    }
}
/// # Safety
/// Release the unique create result once, after all calls finish.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_pipe_line_release(owner: *mut Line) {
    if !owner.is_null() {
        // SAFETY: native RAII adapter transfers ownership once.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn input_is_copied_before_native_search_and_each_reply_is_owned() {
        let mut input = [65u16, 124, 0, 0xd800];
        // SAFETY: initialized input and unique live owner, queries before release.
        unsafe {
            let owner = xy_vsfilter_pipe_line_create(input.as_ptr(), 4);
            input.fill(7);
            assert_eq!(xy_vsfilter_pipe_line_field(owner, 1, 3), 0xd800);
            assert_eq!(xy_vsfilter_pipe_line_position(owner), 0);
            xy_vsfilter_pipe_line_found(owner, 1);
            assert_eq!(xy_vsfilter_pipe_line_position(owner), 2);
            assert_eq!(xy_vsfilter_pipe_line_field(owner, 1, 1), 10);
            xy_vsfilter_pipe_line_found(owner, -1);
            assert_eq!(xy_vsfilter_pipe_line_position(owner), -1);
            assert_eq!(xy_vsfilter_pipe_line_field(owner, 1, u32::MAX), 0);
            xy_vsfilter_pipe_line_release(owner);
        }
    }
}
