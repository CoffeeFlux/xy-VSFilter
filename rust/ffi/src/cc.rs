// SPDX-License-Identifier: GPL-2.0-or-later
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
