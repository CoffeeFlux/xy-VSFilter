// SPDX-License-Identifier: GPL-2.0-or-later
/// # Safety
/// Nonempty input spans length initialized aligned u16 values, immutable during
/// this call. No borrow is retained across subsequent native Delete/TrimLeft.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_webvtt_prefix(input: *const u16, length: u32) -> u32 {
    if length == 0 || length > i32::MAX as u32 || length as usize > isize::MAX as usize / 2 {
        return 0;
    }
    // SAFETY: caller supplies initialized, immutable input storage.
    xy_vsfilter_compat::webvtt::cue_prefix(unsafe {
        core::slice::from_raw_parts(input, length as usize)
    }) as u32
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn native_input_can_change_after_the_scalar_result_returns() {
        let mut input = [97u16, 108, 105, 103, 110, 58, 10, 0xd800];
        // SAFETY: initialized input remains immutable during each call; empty
        // input needs no allocation. No native storage borrow escapes a call.
        unsafe {
            assert_eq!(xy_vsfilter_webvtt_prefix(core::ptr::null(), 0), 0);
            assert_eq!(xy_vsfilter_webvtt_prefix(input.as_ptr(), 8), 6);
            input[6] = 0;
            assert_eq!(xy_vsfilter_webvtt_prefix(input.as_ptr(), 8), 0);
        }
    }
}
