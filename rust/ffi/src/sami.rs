// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::sami::Sami;
/// # Safety
/// Nonempty spans contain the stated number of initialized aligned u16 values,
/// immutable for this call; byte sizes fit isize. The result retains no loans.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_sami_create(
    text: *const u16,
    text_len: u32,
    lower: *const u16,
    lower_len: u32,
) -> *mut Sami {
    // SAFETY: caller loans initialized immutable spans, with empty loans optional.
    let text = if text_len == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(text, text_len as usize) }
    };
    // SAFETY: same contract for the independent or aliased lowercase input.
    let lower = if lower_len == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(lower, lower_len as usize) }
    };
    Sami::new(text, lower).map_or(core::ptr::null_mut(), |state| {
        Box::into_raw(Box::new(state))
    })
}
/// # Safety
/// owner is live and uniquely accessible, with no field-query borrow alive.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_sami_step(owner: *mut Sami) -> u32 {
    // SAFETY: native adapter provides exclusive access between scalar queries.
    let state = unsafe { &mut *owner };
    state.step()
}
/// # Safety
/// owner is live and uniquely accessible. Nonempty input spans length initialized
/// u16 values, disjoint from owner, and length*2 fits isize. Called only after
/// step requests font parsing; advance is the native fragment's cursor increment.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_sami_insert(
    owner: *mut Sami,
    input: *const u16,
    length: u32,
    advance: u32,
) {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: native temporary loans initialized, immutable units.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    // SAFETY: unique state is disjoint from the native fragment loan.
    let state = unsafe { &mut *owner };
    state.insert(input, advance);
}
/// # Safety
/// owner stays live and immutable during this bounded scalar query. group zero
/// selects current text; group one selects saved arguments. field zero is length.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_sami_field(
    owner: *const Sami,
    group: u32,
    field: u32,
    index: u32,
) -> u32 {
    // SAFETY: query precedes unique release and overlaps no mutation.
    let state = unsafe { &*owner };
    let text = if group == 0 {
        &state.text
    } else {
        &state.arguments
    };
    match field {
        0 => text.len() as u32,
        1 => u32::from(text.get(index as usize).copied().unwrap_or(0)),
        _ => 0,
    }
}
/// # Safety
/// Release the unique create result exactly once after all calls complete.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_sami_release(owner: *mut Sami) {
    if !owner.is_null() {
        // SAFETY: noncopyable adapter returns unique ownership.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn owned_text_and_arguments_survive_input_changes() {
        let mut input = [60u16, 102, 111, 110, 116, 32, 62];
        // SAFETY: aliased immutable inputs, mutation only after creation, live
        // unique owner throughout subsequent calls and one release afterward.
        unsafe {
            let owner = xy_vsfilter_sami_create(input.as_ptr(), 7, input.as_ptr(), 7);
            input.fill(0);
            assert!(!owner.is_null());
            assert_eq!(xy_vsfilter_sami_step(owner), 1);
            assert_eq!(xy_vsfilter_sami_field(owner, 1, 1, 0), 62);
            let mut piece = [65u16];
            xy_vsfilter_sami_insert(owner, piece.as_ptr(), 1, 1);
            piece[0] = 66;
            assert_eq!(xy_vsfilter_sami_step(owner), 0);
            assert_eq!(xy_vsfilter_sami_field(owner, 0, 1, 0), 65);
            assert_eq!(xy_vsfilter_sami_field(owner, 0, 1, u32::MAX), 0);
            xy_vsfilter_sami_release(owner);
        }
    }
}
