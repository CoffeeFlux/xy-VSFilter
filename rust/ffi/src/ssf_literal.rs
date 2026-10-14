// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::ssf_literal::Literal;
use xy_vsfilter_compat::ssf_literal::Text;

#[no_mangle]
pub extern "C" fn xy_vsfilter_ssf_literal_text_create() -> *mut Text {
    Box::into_raw(Box::new(Text::default()))
}
/// # Safety
/// owner is unique and live. Any loan from data has ended before this mutation.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssf_literal_text_append(owner: *mut Text, value: u16) -> u32 {
    // SAFETY: native RAII lends exclusive ownership for one append.
    u32::from(unsafe { &mut *owner }.append(value))
}
/// # Safety
/// owner is live and immutable for this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssf_literal_text_length(owner: *const Text) -> i32 {
    // SAFETY: append bounds length to the native CStringW range.
    unsafe { &*owner }.units().len() as i32
}
/// # Safety
/// owner is live. The returned read-only loan covers length initialized units;
/// it must not be used after the next append or release. Empty data is not read.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssf_literal_text_data(owner: *const Text) -> *const u16 {
    // SAFETY: the native caller only copies these units into a CStringW.
    unsafe { &*owner }.units().as_ptr()
}
/// # Safety
/// owner is null or the unique create result. All data loans have ended.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssf_literal_text_release(owner: *mut Text) {
    if !owner.is_null() {
        // SAFETY: this returns ownership exactly once after native copies end.
        drop(unsafe { Box::from_raw(owner) });
    }
}
/// # Safety
/// state points to aligned exclusive storage for Literal. No old Rust value
/// needs dropping; this record owns no resources. No borrow escapes this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssf_literal_init(state: *mut Literal, block: u32) {
    // SAFETY: native stack storage has the matching C layout and size.
    unsafe { state.write(Literal::new(block != 0)) };
}
/// # Safety
/// state is initialized and exclusive for this call. All native callbacks run
/// after return. The result packs an action and a copied raw character value.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssf_literal_step(state: *mut Literal, last: i32) -> u64 {
    // SAFETY: the transient mutable borrow ends before the native callback.
    let state = unsafe { &mut *state };
    let action = state.step(last) as u64;
    action | ((state.value() as u32 as u64) << 32)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn text_loans_end_before_mutation_and_release() {
        // SAFETY: one live owner, initialized immutable loans copied before any
        // further append, no native references or loans survive release.
        unsafe {
            let owner = xy_vsfilter_ssf_literal_text_create();
            assert_eq!(xy_vsfilter_ssf_literal_text_length(owner), 0);
            for unit in [0, 0xd800, 0xdc00, 0xffff, 65] {
                assert_eq!(xy_vsfilter_ssf_literal_text_append(owner, unit), 1);
            }
            let data = xy_vsfilter_ssf_literal_text_data(owner);
            let length = xy_vsfilter_ssf_literal_text_length(owner) as usize;
            let snapshot = core::slice::from_raw_parts(data, length).to_vec();
            for _ in 0..256 {
                assert_eq!(xy_vsfilter_ssf_literal_text_append(owner, 0), 1);
            }
            xy_vsfilter_ssf_literal_text_release(owner);
            assert_eq!(snapshot, [0, 0xd800, 0xdc00, 0xffff, 65]);
            xy_vsfilter_ssf_literal_text_release(core::ptr::null_mut());
        }
    }
    #[test]
    fn caller_owned_record_can_be_reinitialized_after_interruption() {
        let mut record = core::mem::MaybeUninit::<Literal>::uninit();
        // SAFETY: aligned writable storage, initialized before stepping. Native
        // callbacks are represented by scalar results between temporary borrows.
        unsafe {
            xy_vsfilter_ssf_literal_init(record.as_mut_ptr(), 1);
            assert_eq!(
                xy_vsfilter_ssf_literal_step(record.as_mut_ptr(), 0) as u32,
                1
            );
            assert_eq!(
                xy_vsfilter_ssf_literal_step(record.as_mut_ptr(), 123) as u32,
                2
            );
            xy_vsfilter_ssf_literal_init(record.as_mut_ptr(), 0);
            for (last, action) in [(0, 0), (34, 2), (34, 3), (92, 2), (92, 2), (0xd800, 4)] {
                let result = xy_vsfilter_ssf_literal_step(record.as_mut_ptr(), last);
                assert_eq!(result as u32, action);
                if action == 4 {
                    assert_eq!(result >> 32, 0xd800);
                }
            }
        }
    }
}
