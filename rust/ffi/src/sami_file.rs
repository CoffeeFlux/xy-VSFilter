// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::sami_file::File;
#[no_mangle]
pub extern "C" fn xy_vsfilter_sami_file_create() -> *mut File {
    Box::into_raw(Box::default())
}
/// # Safety
/// owner is live and exclusive; nonempty input spans length initialized aligned
/// u16 values, immutable and disjoint from owner; length*2 fits isize.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_sami_file_begin(
    owner: *mut File,
    input: *const u16,
    length: u32,
) -> u32 {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: fresh initialized uppercase line, borrowed until return.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    // SAFETY: uniquely owned state, disjoint from input.
    let state = unsafe { &mut *owner };
    state.begin(input)
}
/// # Safety
/// owner is live and exclusive; fragment spans length initialized aligned u16
/// values, immutable and disjoint from owner; length*2 fits isize.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_sami_file_append(
    owner: *mut File,
    fragment: *const u16,
    length: u32,
) {
    let fragment = if length == 0 {
        &[]
    } else {
        // SAFETY: native counted fragment, copied before returning.
        unsafe { core::slice::from_raw_parts(fragment, length as usize) }
    };
    // SAFETY: exclusive access, without native callbacks during this loan.
    let state = unsafe { &mut *owner };
    state.append(fragment);
}
/// # Safety
/// owner is live and exclusively accessible throughout this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_sami_file_flushed(owner: *mut File, time: i32) {
    // SAFETY: native adapter retains unique ownership.
    let state = unsafe { &mut *owner };
    state.flushed(time);
}
/// # Safety
/// owner is live and exclusively accessible throughout this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_sami_file_end(owner: *mut File) {
    // SAFETY: native adapter retains unique ownership.
    let state = unsafe { &mut *owner };
    state.end();
}
/// # Safety
/// owner is live and immutable throughout this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_sami_file_start(owner: *const File) -> i32 {
    // SAFETY: query precedes release and cannot overlap mutation.
    unsafe { (*owner).start }
}
/// # Safety
/// owner is live and immutable throughout this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_sami_file_field(
    owner: *const File,
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
pub unsafe extern "C" fn xy_vsfilter_sami_file_release(owner: *mut File) {
    if !owner.is_null() {
        // SAFETY: native RAII adapter transfers ownership once.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn native_lines_and_fragments_are_not_borrowed_across_callbacks() {
        let owner = xy_vsfilter_sami_file_create();
        let mut input = [65u16, 0, 0xd800];
        // SAFETY: initialized disjoint spans, unique live owner and release.
        unsafe {
            assert_eq!(xy_vsfilter_sami_file_begin(owner, input.as_ptr(), 3), 1);
            xy_vsfilter_sami_file_append(owner, input.as_ptr(), 3);
            input.fill(7);
            assert_eq!(xy_vsfilter_sami_file_field(owner, 1, 2), 0xd800);
            xy_vsfilter_sami_file_end(owner);
            xy_vsfilter_sami_file_flushed(owner, 123);
            assert_eq!(xy_vsfilter_sami_file_start(owner), 123);
            assert_eq!(xy_vsfilter_sami_file_field(owner, 0, 0), 0);
            assert_eq!(xy_vsfilter_sami_file_field(owner, 1, u32::MAX), 0);
            xy_vsfilter_sami_file_release(owner);
        }
    }
}
