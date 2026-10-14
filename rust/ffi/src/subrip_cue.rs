// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::subrip_cue::Cue;
#[no_mangle]
pub extern "C" fn xy_vsfilter_subrip_cue_create() -> *mut Cue {
    Box::into_raw(Box::default())
}
/// # Safety
/// owner is live and exclusively accessible throughout this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_subrip_cue_empty_line(owner: *mut Cue) {
    // SAFETY: native adapter retains unique ownership.
    let state = unsafe { &mut *owner };
    state.empty_line();
}
/// # Safety
/// owner is live and immutable throughout this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_subrip_cue_boundary(owner: *const Cue, numeric: u32) -> u32 {
    // SAFETY: query precedes release and cannot overlap mutation.
    let state = unsafe { &*owner };
    u32::from(state.boundary(numeric != 0))
}
/// # Safety
/// owner is live and exclusive; fragment spans length initialized aligned u16
/// values, immutable and disjoint from owner; length*2 fits isize.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_subrip_cue_append(
    owner: *mut Cue,
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
/// owner is live and immutable throughout this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_subrip_cue_field(
    owner: *const Cue,
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
pub unsafe extern "C" fn xy_vsfilter_subrip_cue_release(owner: *mut Cue) {
    if !owner.is_null() {
        // SAFETY: native RAII adapter transfers ownership once.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn fragments_are_copied_and_boundary_state_is_owned() {
        let owner = xy_vsfilter_subrip_cue_create();
        let mut input = [65u16, 0, 0xd800];
        // SAFETY: initialized disjoint input, exclusive owner and unique release.
        unsafe {
            xy_vsfilter_subrip_cue_append(owner, input.as_ptr(), 3);
            input.fill(7);
            assert_eq!(xy_vsfilter_subrip_cue_field(owner, 1, 2), 0xd800);
            assert_eq!(xy_vsfilter_subrip_cue_boundary(owner, 1), 0);
            xy_vsfilter_subrip_cue_empty_line(owner);
            assert_eq!(xy_vsfilter_subrip_cue_boundary(owner, 0), 0);
            assert_eq!(xy_vsfilter_subrip_cue_boundary(owner, 1), 1);
            assert_eq!(xy_vsfilter_subrip_cue_field(owner, 0, 0), 3);
            assert_eq!(xy_vsfilter_subrip_cue_field(owner, 1, u32::MAX), 0);
            xy_vsfilter_subrip_cue_release(owner);
        }
    }
}
