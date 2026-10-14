// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::ass_dialogue::Dialogue;
/// # Safety
/// Nonempty input spans length initialized aligned u16 values, immutable during
/// creation; length*2 fits isize. The returned owner retains no input borrow.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_dialogue_create(
    input: *const u16,
    length: u32,
    version: i32,
) -> *mut Dialogue {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: caller supplies initialized immutable code units.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    Box::into_raw(Box::new(Dialogue::new(input, version)))
}
/// # Safety
/// owner is live and exclusively accessible until this call returns.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_dialogue_step(owner: *mut Dialogue, reply: i32) -> u32 {
    // SAFETY: noncopyable native adapter holds unique access.
    let state = unsafe { &mut *owner };
    state.step(reply)
}
/// # Safety
/// owner is live and immutable throughout this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_dialogue_number(
    owner: *const Dialogue,
    index: u32,
) -> i32 {
    // SAFETY: scalar queries precede release and cannot overlap mutation.
    let state = unsafe { &*owner };
    state.number(index as usize)
}
/// # Safety
/// owner is live and immutable throughout this scalar query. Fields: length,
/// bounded unit, numeric separator; groups: token, style, actor, effect, text.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_dialogue_field(
    owner: *const Dialogue,
    group: u32,
    field: u32,
    index: u32,
) -> u32 {
    // SAFETY: native adapter retains the owner until queries finish.
    let state = unsafe { &*owner };
    match field {
        0 => state.string(group as usize).len() as u32,
        1 => u32::from(
            state
                .string(group as usize)
                .get(index as usize)
                .copied()
                .unwrap_or(0),
        ),
        2 => u32::from(state.separator),
        _ => 0,
    }
}
/// # Safety
/// Return the unique create result exactly once after all calls complete.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_dialogue_release(owner: *mut Dialogue) {
    if !owner.is_null() {
        // SAFETY: unique ownership is returned once by the native RAII adapter.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn token_owner_survives_input_mutation_and_scalar_queries_are_bounded() {
        let mut input = [32u16, 49, 0, 44, 50];
        // SAFETY: initialized input, exclusive owner operations and one release.
        unsafe {
            let owner = xy_vsfilter_ass_dialogue_create(input.as_ptr(), 5, 5);
            input.fill(9);
            assert_eq!(xy_vsfilter_ass_dialogue_step(owner, 0), 1);
            assert_eq!(xy_vsfilter_ass_dialogue_field(owner, 0, 0, 0), 1);
            assert_eq!(xy_vsfilter_ass_dialogue_field(owner, 0, 1, 0), 49);
            assert_eq!(xy_vsfilter_ass_dialogue_field(owner, 0, 1, u32::MAX), 0);
            assert_eq!(xy_vsfilter_ass_dialogue_step(owner, 11), 1);
            assert_eq!(xy_vsfilter_ass_dialogue_number(owner, 0), 11);
            assert_eq!(xy_vsfilter_ass_dialogue_number(owner, u32::MAX), 0);
            xy_vsfilter_ass_dialogue_release(owner);
        }
    }
}
