// SPDX-License-Identifier: GPL-2.0-or-later
use xy_vsfilter_compat::wrap_width;

/// # Safety
/// output is aligned, writable for four i32 values and exclusively accessible.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_wrap_begin(full: i32, maximum: i32, output: *mut i32) -> u32 {
    if let Some(state) = wrap_width::begin(full, maximum) {
        // SAFETY: all four output values are writable, with no overlapping loan.
        unsafe { output.cast::<[i32; 4]>().write(state) };
        1
    } else {
        0
    }
}
/// # Safety
/// state points to four initialized aligned i32 values, immutable during call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_wrap_needs_word(state: *const i32) -> u32 {
    // SAFETY: live, immutable four-value native state, no callback during borrow.
    u32::from(wrap_width::needs_word(unsafe {
        &*state.cast::<[i32; 4]>()
    }))
}
/// # Safety
/// state points to four initialized aligned i32 values, exclusive during call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_wrap_push(state: *mut i32, word: i32) -> u32 {
    // SAFETY: no native reference or callback overlaps this exclusive borrow.
    u32::from(wrap_width::push(unsafe { &mut *state.cast::<[i32; 4]>() }, word).is_some())
}
/// # Safety
/// state points to four initialized aligned immutable i32 values. output is
/// aligned, writable for one i32, exclusively accessible and disjoint from state.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_wrap_finish(
    state: *const i32,
    full: i32,
    style: i32,
    output: *mut i32,
) -> u32 {
    // SAFETY: native state stays live and immutable for this call.
    if let Some(width) = wrap_width::finish(unsafe { &*state.cast::<[i32; 4]>() }, full, style) {
        // SAFETY: fresh disjoint output owned by the native caller.
        unsafe { output.write(width) };
        1
    } else {
        0
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn native_state_and_outputs_stay_intact_on_decline() {
        let mut state = [37i32; 4];
        let mut result = 91;
        // SAFETY: live aligned arrays and disjoint output, no overlapping borrows.
        unsafe {
            assert_eq!(xy_vsfilter_wrap_begin(i32::MIN, 1, state.as_mut_ptr()), 0);
            assert_eq!(state, [37; 4]);
            assert_eq!(xy_vsfilter_wrap_begin(12, 10, state.as_mut_ptr()), 1);
            assert_eq!(xy_vsfilter_wrap_needs_word(state.as_ptr()), 1);
            let before = state;
            assert_eq!(xy_vsfilter_wrap_push(state.as_mut_ptr(), i32::MIN), 0);
            assert_eq!(state, before);
            assert_eq!(xy_vsfilter_wrap_push(state.as_mut_ptr(), 7), 1);
            assert_eq!(xy_vsfilter_wrap_needs_word(state.as_ptr()), 0);
            assert_eq!(
                xy_vsfilter_wrap_finish(state.as_ptr(), 12, 0, &mut result),
                1
            );
            assert_eq!(result, 7);
            state = [1, i32::MAX, 5, 10];
            assert_eq!(
                xy_vsfilter_wrap_finish(state.as_ptr(), 10, 3, &mut result),
                0
            );
            assert_eq!(result, 7);
        }
    }
}
