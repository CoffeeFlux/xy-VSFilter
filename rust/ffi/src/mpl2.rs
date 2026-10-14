// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::{boxed::Box, vec::Vec};

/// # Safety
/// Nonempty input spans length initialized aligned u16 values, immutable during
/// this call. The returned unique owner retains no borrow of the input.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_mpl2_create(input: *const u16, length: u32) -> *mut Vec<u16> {
    if length > i32::MAX as u32 || length as usize > isize::MAX as usize / 2 {
        return core::ptr::null_mut();
    }
    let input = if length == 0 {
        &[]
    } else {
        if input.is_null() {
            return core::ptr::null_mut();
        }
        // SAFETY: caller supplies the initialized, immutable input span.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    xy_vsfilter_compat::mpl2::prepare(input)
        .map_or(core::ptr::null_mut(), |text| Box::into_raw(Box::new(text)))
}

/// # Safety
/// owner remains live and immutable. Field 0 is length, field 1 is a bounded
/// code-unit query. Out-of-range queries return zero.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_mpl2_field(
    owner: *const Vec<u16>,
    field: u32,
    index: u32,
) -> u32 {
    // SAFETY: bridge retains the owner until all scalar queries finish.
    let text = unsafe { &*owner };
    match field {
        0 => text.len() as u32,
        1 => u32::from(text.get(index as usize).copied().unwrap_or(0)),
        _ => 0,
    }
}

/// # Safety
/// Release a unique owner from create exactly once, after all queries.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_mpl2_release(owner: *mut Vec<u16>) {
    if !owner.is_null() {
        // SAFETY: the C++ noncopyable RAII bridge returns unique ownership.
        drop(unsafe { Box::from_raw(owner) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn prepared_text_owns_input_and_declines_nul() {
        let mut input = [47u16, 0xd800];
        // SAFETY: input is initialized and owner is released once after queries.
        unsafe {
            let owner = xy_vsfilter_mpl2_create(input.as_ptr(), 2);
            assert!(!owner.is_null());
            input.fill(0);
            assert_eq!(xy_vsfilter_mpl2_field(owner, 0, 0), 6);
            assert_eq!(xy_vsfilter_mpl2_field(owner, 1, 5), 0xd800);
            assert_eq!(xy_vsfilter_mpl2_field(owner, 1, u32::MAX), 0);
            xy_vsfilter_mpl2_release(owner);
            assert!(xy_vsfilter_mpl2_create(input.as_ptr(), 2).is_null());
        }
    }
}
