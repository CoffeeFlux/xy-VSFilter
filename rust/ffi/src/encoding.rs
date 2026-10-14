// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::encoding::Encoding;

/// # Safety
/// Nonempty input spans length initialized aligned u16 values, immutable during
/// creation. length*2 fits isize. No input borrow escapes this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_encoding_create(
    input: *const u16,
    length: u32,
) -> *mut Encoding {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: native bridge provides an initialized immutable span.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    Box::into_raw(Box::new(Encoding::new(input)))
}
/// # Safety
/// owner is live and exclusively accessible until this call returns.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_encoding_take(owner: *mut Encoding) -> i32 {
    // SAFETY: noncopyable native adapter owns the state exclusively.
    let state = unsafe { &mut *owner };
    state.take()
}
/// # Safety
/// owner is live and exclusively accessible until this call returns.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_encoding_append_unit(owner: *mut Encoding, unit: u16) {
    // SAFETY: exclusive owner access; no native callback occurs during the loan.
    let state = unsafe { &mut *owner };
    state.append_unit(unit);
}
/// # Safety
/// owner is live and exclusive; bytes spans length initialized bytes immutable
/// during this call and disjoint from owner. Only the native API's returned
/// initialized length (0..8) may be passed, never the entire temporary buffer.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_encoding_append_bytes(
    owner: *mut Encoding,
    bytes: *const u8,
    length: u32,
) {
    let bytes = if length == 0 {
        &[]
    } else {
        // SAFETY: initialized conversion result, loaned only for this call.
        unsafe { core::slice::from_raw_parts(bytes, length as usize) }
    };
    // SAFETY: disjoint, uniquely owned state.
    let state = unsafe { &mut *owner };
    state.append_bytes(bytes);
}
/// # Safety
/// owner is live and immutable throughout this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_encoding_field(
    owner: *const Encoding,
    field: u32,
    index: u32,
) -> u32 {
    // SAFETY: native adapter retains ownership until queries finish.
    let state = unsafe { &*owner };
    match field {
        0 => state.output().len() as u32,
        1 => u32::from(state.output().get(index as usize).copied().unwrap_or(0)),
        _ => 0,
    }
}
/// # Safety
/// Release the unique create result exactly once, after all calls complete.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_encoding_release(owner: *mut Encoding) {
    if !owner.is_null() {
        // SAFETY: native RAII adapter transfers ownership once.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn input_and_conversion_loans_end_before_native_callbacks() {
        let mut input = [0u16, 0xd800, 0xff81];
        // SAFETY: initialized spans; independent live owner, released once.
        unsafe {
            let owner = xy_vsfilter_encoding_create(input.as_ptr(), 3);
            input.fill(7);
            assert_eq!(xy_vsfilter_encoding_take(owner), 0);
            assert_eq!(xy_vsfilter_encoding_take(owner), 0xd800);
            let mut bytes = [0u8, 255];
            xy_vsfilter_encoding_append_bytes(owner, bytes.as_ptr(), 2);
            bytes.fill(7);
            xy_vsfilter_encoding_append_bytes(owner, core::ptr::null(), 0);
            xy_vsfilter_encoding_append_unit(owner, 0xff81);
            assert_eq!(xy_vsfilter_encoding_take(owner), 0xff81);
            assert_eq!(xy_vsfilter_encoding_take(owner), -1);
            assert_eq!(xy_vsfilter_encoding_field(owner, 0, 0), 4);
            for (index, value) in [0, 255, 63, 0xff81].into_iter().enumerate() {
                assert_eq!(xy_vsfilter_encoding_field(owner, 1, index as u32), value);
            }
            assert_eq!(xy_vsfilter_encoding_field(owner, 1, u32::MAX), 0);
            xy_vsfilter_encoding_release(owner);
            let empty = xy_vsfilter_encoding_create(core::ptr::null(), 0);
            assert_eq!(xy_vsfilter_encoding_take(empty), -1);
            xy_vsfilter_encoding_release(empty);
        }
    }
}

use xy_vsfilter_compat::encoding::Ssa;
/// # Safety
/// Nonempty input spans length initialized aligned u16 values, immutable during
/// creation. length*2 fits isize. No input borrow escapes this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssa_encoding_create(
    input: *const u16,
    length: u32,
) -> *mut Ssa {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: caller supplies initialized immutable code units.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    Box::into_raw(Box::new(Ssa::new(input)))
}
/// # Safety
/// owner is live and uniquely accessible throughout this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssa_encoding_step(owner: *mut Ssa, reply: i32) -> u32 {
    // SAFETY: native noncopyable adapter retains exclusive ownership.
    let state = unsafe { &mut *owner };
    state.step(reply)
}
/// # Safety
/// owner is live and exclusive; fragment spans length initialized aligned u16
/// values, immutable, disjoint from owner and with length*2 fitting isize.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssa_encoding_append(
    owner: *mut Ssa,
    fragment: *const u16,
    length: u32,
) {
    let fragment = if length == 0 {
        &[]
    } else {
        // SAFETY: fresh native append result, borrowed only until this returns.
        unsafe { core::slice::from_raw_parts(fragment, length as usize) }
    };
    // SAFETY: exclusive owner access, disjoint from the fragment loan.
    let state = unsafe { &mut *owner };
    state.append(fragment);
}
/// # Safety
/// owner is live and immutable throughout this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssa_encoding_field(
    owner: *const Ssa,
    field: u32,
    index: u32,
) -> u32 {
    // SAFETY: native adapter queries before releasing ownership.
    let state = unsafe { &*owner };
    match field {
        0 => state.input().len() as u32,
        1 => u32::from(state.input().get(index as usize).copied().unwrap_or(0)),
        2 => state.output().len() as u32,
        3 => u32::from(state.output().get(index as usize).copied().unwrap_or(0)),
        4 => state.count as u32,
        _ => 0,
    }
}
/// # Safety
/// Release the unique create result once, after all calls finish.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssa_encoding_release(owner: *mut Ssa) {
    if !owner.is_null() {
        // SAFETY: native RAII adapter transfers unique ownership once.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod ssa_tests {
    use super::*;
    #[test]
    fn owned_buffers_survive_native_input_and_fragment_changes() {
        let mut input = [65u16, 0, 0xd800];
        // SAFETY: initialized, disjoint loans; unique live owner released once.
        unsafe {
            let owner = xy_vsfilter_ssa_encoding_create(input.as_ptr(), 3);
            input.fill(7);
            assert_eq!(xy_vsfilter_ssa_encoding_step(owner, 0), 1);
            assert_eq!(xy_vsfilter_ssa_encoding_step(owner, -1), 6);
            assert_eq!(xy_vsfilter_ssa_encoding_field(owner, 1, 0), 65);
            assert_eq!(xy_vsfilter_ssa_encoding_field(owner, 1, 2), 0xd800);
            xy_vsfilter_ssa_encoding_append(owner, input.as_ptr(), 3);
            input.fill(9);
            xy_vsfilter_ssa_encoding_append(owner, core::ptr::null(), 0);
            assert_eq!(xy_vsfilter_ssa_encoding_step(owner, 0), 0);
            assert_eq!(xy_vsfilter_ssa_encoding_field(owner, 2, 0), 3);
            assert_eq!(xy_vsfilter_ssa_encoding_field(owner, 3, 1), 7);
            assert_eq!(xy_vsfilter_ssa_encoding_field(owner, 3, u32::MAX), 0);
            xy_vsfilter_ssa_encoding_release(owner);
        }
    }
}
