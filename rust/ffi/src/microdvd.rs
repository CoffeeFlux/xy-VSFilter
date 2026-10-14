// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::microdvd::Converter;
#[no_mangle]
pub extern "C" fn xy_vsfilter_microdvd_create(length: i32) -> *mut Converter {
    Box::into_raw(Box::new(Converter::new(length)))
}
/// # Safety
/// owner is the live, uniquely accessible create result. reply comes from the
/// requested native FindChar or CString length operation on the current input.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_microdvd_step(owner: *mut Converter, reply: i32) -> u32 {
    // SAFETY: adapter holds unique owner across this call.
    let state = unsafe { &mut *owner };
    state.step(reply)
}
/// # Safety
/// owner is uniquely accessible. Nonempty input spans length initialized u16
/// values, disjoint from owner and immutable during this call; length*2 fits isize.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_microdvd_append(
    owner: *mut Converter,
    input: *const u16,
    length: u32,
    restore: u32,
) {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: native temporary loans its initialized units for this call.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    // SAFETY: adapter holds unique state, disjoint from the native input loan.
    let state = unsafe { &mut *owner };
    state.append(input, restore);
}
/// # Safety
/// owner remains live and immutable during this scalar field/code-unit query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_microdvd_field(
    owner: *const Converter,
    field: u32,
    index: u32,
) -> i32 {
    // SAFETY: owner remains live until the adapter releases it.
    let state = unsafe { &*owner };
    match field {
        0 => state.start,
        1 => state.count,
        2 => state.restore as i32,
        3 => state.output.len() as i32,
        4 => i32::from(state.output.get(index as usize).copied().unwrap_or(0)),
        _ => 0,
    }
}
/// # Safety
/// Return the unique create result exactly once after all calls complete.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_microdvd_release(owner: *mut Converter) {
    if !owner.is_null() {
        // SAFETY: native noncopyable RAII adapter returns unique ownership.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn output_owns_appended_units_and_bounds_queries() {
        let owner = xy_vsfilter_microdvd_create(0);
        // SAFETY: initialized temporary and live unique owner, released once.
        unsafe {
            {
                let mut input = [0xd800, 0, 65];
                xy_vsfilter_microdvd_append(owner, input.as_ptr(), 3, 5);
                input.fill(1);
            }
            assert_eq!(xy_vsfilter_microdvd_step(owner, 0), 0);
            assert_eq!(xy_vsfilter_microdvd_field(owner, 3, 0), 3);
            assert_eq!(xy_vsfilter_microdvd_field(owner, 4, 0), 0xd800);
            assert_eq!(xy_vsfilter_microdvd_field(owner, 4, u32::MAX), 0);
            assert_eq!(xy_vsfilter_microdvd_field(owner, 2, 0), 5);
            xy_vsfilter_microdvd_release(owner);
        }
    }
}
