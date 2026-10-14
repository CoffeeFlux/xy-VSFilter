// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::cc::Decoder;

#[no_mangle]
pub extern "C" fn xy_vsfilter_cc_decoder_create(length: i32) -> *mut Decoder {
    Box::into_raw(Box::new(Decoder::new(length)))
}

/// # Safety
/// owner is live and exclusive. Nonempty input is a disjoint initialized byte
/// span, loaned only for this call and never retained through native operations.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_cc_decoder_next(
    owner: *mut Decoder,
    input: *const u8,
    length: usize,
) -> u32 {
    // SAFETY: initialized native input disjoint from the unique live owner.
    let data = if length == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(input, length) }
    };
    // SAFETY: native RAII owns this live decoder exclusively.
    let state = unsafe { &mut *owner };
    state.next(data)
}

/// # Safety
/// owner is live and immutable. Invalid queries return zero.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_cc_decoder_field(owner: *const Decoder, field: u32) -> i32 {
    // SAFETY: immutable scalar query before the unique release.
    let state = unsafe { &*owner };
    match field {
        0 => state.action_position as i32,
        1 => i32::from(state.value),
        2 => state.x,
        3 => state.y,
        _ => 0,
    }
}

/// # Safety
/// Release the unique owner exactly once after all other calls.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_cc_decoder_release(owner: *mut Decoder) {
    if !owner.is_null() {
        // SAFETY: native RAII returns its uniquely owned allocation once.
        drop(unsafe { Box::from_raw(owner) });
    }
}

/// # Safety
/// For accepted coordinates, output provides two writable aligned i32 slots.
/// Declined arithmetic never accesses output, which may then be null.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_cc_move(x: i32, y: i32, output: *mut i32) -> u32 {
    let Some(position) = xy_vsfilter_compat::cc::move_cursor(x, y) else {
        return 0;
    };
    // SAFETY: accepted calls provide two writable slots; neither is read.
    unsafe {
        output.write(position[0]);
        output.add(1).write(position[1]);
    }
    1
}

/// # Safety
/// For valid coordinates, buffer is an initialized, aligned, exclusively
/// borrowed array of 528 u16 values. Invalid coordinates do not access it.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_cc_put(buffer: *mut u16, x: i32, y: i32, value: u16) -> u32 {
    if !(0..32).contains(&x) || !(0..16).contains(&y) {
        return 0;
    }
    // SAFETY: native initialized 16 by 33 backing is loaned exclusively for this
    // call. No borrow survives to cursor movement or native subtitle callbacks.
    let data = unsafe { core::slice::from_raw_parts_mut(buffer, 528) };
    u32::from(xy_vsfilter_compat::cc::put(data, x, y, value))
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn decoder_reads_live_bytes_only_between_native_actions() {
        let owner = xy_vsfilter_cc_decoder_create(2);
        let mut input = [b'A', b'B'];
        // SAFETY: unique live owner; initialized input borrowed only during each
        // call, changed afterward, then a single release.
        unsafe {
            assert_eq!(xy_vsfilter_cc_decoder_next(owner, input.as_ptr(), 2), 1);
            input[1] = b'Z';
            assert_eq!(xy_vsfilter_cc_decoder_next(owner, input.as_ptr(), 2), 1);
            assert_eq!(xy_vsfilter_cc_decoder_field(owner, 1), i32::from(b'Z'));
            assert_eq!(xy_vsfilter_cc_decoder_next(owner, input.as_ptr(), 2), 0);
            xy_vsfilter_cc_decoder_release(owner);
        }
    }

    #[test]
    fn writes_are_bounded_and_declines_leave_foreign_storage_alone() {
        let mut cells = [0xaaaau16; 530];
        let mut position = [core::mem::MaybeUninit::<i32>::uninit(); 2];
        // SAFETY: initialized 528-cell interior; disjoint output with two slots;
        // invalid requests return before touching the null pointer.
        unsafe {
            assert_eq!(
                xy_vsfilter_cc_put(cells.as_mut_ptr().add(1), 31, 15, 0xffff),
                1
            );
            assert_eq!(cells[527], 0xffff);
            assert_eq!(cells[528], 0xaaaa);
            assert_eq!(cells[529], 0xaaaa);
            assert_eq!(cells[0], 0xaaaa);
            assert_eq!(xy_vsfilter_cc_put(core::ptr::null_mut(), 32, 15, 0), 0);
            assert_eq!(xy_vsfilter_cc_move(32, 15, position.as_mut_ptr().cast()), 1);
            assert_eq!(position.map(|part| part.assume_init()), [0, 0]);
            assert_eq!(xy_vsfilter_cc_move(32, i32::MAX, core::ptr::null_mut()), 0);
        }
    }
}
