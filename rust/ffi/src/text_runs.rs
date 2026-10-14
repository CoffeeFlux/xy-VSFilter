// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::text_runs::Runs;
/// # Safety
/// Nonempty input spans length initialized aligned u16 values, immutable during
/// this call; length*2 fits isize. The input is copied before native callbacks.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_text_runs_create(input: *const u16, length: u32) -> *mut Runs {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: initialized fresh native line copy, borrowed until return.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    Box::into_raw(Box::new(Runs::new(input)))
}
/// # Safety
/// owner is live and exclusively accessible throughout preparation.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_text_runs_prepare(owner: *mut Runs, wrap: i32) -> u32 {
    // SAFETY: no native callback or input borrow overlaps this mutation.
    let state = unsafe { &mut *owner };
    u32::from(state.prepare(wrap))
}
/// # Safety
/// owner is live and exclusive; nonempty input spans length initialized aligned
/// u16 values, immutable and disjoint from owner; length*2 fits isize.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_text_runs_reset(
    owner: *mut Runs,
    input: *const u16,
    length: u32,
) {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: a fresh native-prepared copy, borrowed only until return.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    // SAFETY: exclusive owner, disjoint from the copied input.
    let state = unsafe { &mut *owner };
    *state = Runs::new(input);
}
/// # Safety
/// owner is live and exclusive throughout this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_text_runs_next(owner: *mut Runs) -> u32 {
    // SAFETY: no loan crosses native word construction or insertion callbacks.
    let state = unsafe { &mut *owner };
    state.next_run()
}
/// # Safety
/// owner is live and immutable throughout this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_text_runs_field(
    owner: *const Runs,
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
pub unsafe extern "C" fn xy_vsfilter_text_runs_release(owner: *mut Runs) {
    if !owner.is_null() {
        // SAFETY: native RAII adapter transfers ownership once.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn token_data_is_owned_across_native_word_callbacks() {
        let mut input = [65u16, 32, 0, 0xd800];
        // SAFETY: disjoint initialized input; unique live owner and final release.
        unsafe {
            let owner = xy_vsfilter_text_runs_create(input.as_ptr(), 4);
            input.fill(7);
            assert_eq!(xy_vsfilter_text_runs_next(owner), 1);
            assert_eq!(xy_vsfilter_text_runs_field(owner, 1, 0), 65);
            assert_eq!(xy_vsfilter_text_runs_next(owner), 3);
            assert_eq!(xy_vsfilter_text_runs_next(owner), 1);
            assert_eq!(xy_vsfilter_text_runs_field(owner, 1, 0), 0xd800);
            assert_eq!(xy_vsfilter_text_runs_next(owner), 0);
            assert_eq!(xy_vsfilter_text_runs_field(owner, 0, 0), 0);
            xy_vsfilter_text_runs_release(owner);
        }
    }
    #[test]
    fn native_preparation_fallback_is_reimported_before_token_callbacks() {
        let input = [65u16, 0, 92, 78];
        let mut prepared = [65u16, 10, 66];
        // SAFETY: initialized disjoint arrays, live unique owner, final release.
        unsafe {
            let owner = xy_vsfilter_text_runs_create(input.as_ptr(), 4);
            assert_eq!(xy_vsfilter_text_runs_prepare(owner, 2), 0);
            xy_vsfilter_text_runs_reset(owner, prepared.as_ptr(), 3);
            prepared.fill(7);
            assert_eq!(xy_vsfilter_text_runs_next(owner), 1);
            assert_eq!(xy_vsfilter_text_runs_field(owner, 1, 0), 65);
            assert_eq!(xy_vsfilter_text_runs_next(owner), 2);
            assert_eq!(xy_vsfilter_text_runs_next(owner), 1);
            assert_eq!(xy_vsfilter_text_runs_field(owner, 1, 0), 66);
            xy_vsfilter_text_runs_release(owner);
        }
    }
}
