// SPDX-License-Identifier: GPL-2.0-or-later
use xy_vsfilter_compat::ssf_literal::Literal;
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
