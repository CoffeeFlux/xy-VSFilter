// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::cc::Decoder;

pub struct CaptionText {
    text: Option<alloc::vec::Vec<u16>>,
}

/// # Safety
/// cells is an initialized aligned 528-unit native array, immutable during
/// this call. The owned output keeps no borrowed native storage.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_cc_text_create(cells: *const u16) -> *mut CaptionText {
    // SAFETY: native display has exactly 16 by 33 initialized units.
    let input = unsafe { core::slice::from_raw_parts(cells, 528) };
    Box::into_raw(Box::new(CaptionText {
        text: xy_vsfilter_compat::cc::display(input),
    }))
}

/// # Safety
/// owner is live and immutable. Bounded scalar queries expose no references.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_cc_text_field(
    owner: *const CaptionText,
    field: u32,
    index: usize,
) -> usize {
    // SAFETY: immutable access before owner release.
    let state = unsafe { &*owner };
    let text = state.text.as_deref().unwrap_or(&[]);
    match field {
        0 => usize::from(state.text.is_some()),
        1 => text.len(),
        2 => usize::from(text.get(index).copied().unwrap_or(0)),
        _ => 0,
    }
}

/// # Safety
/// Release the uniquely owned text exactly once after all queries.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_cc_text_release(owner: *mut CaptionText) {
    if !owner.is_null() {
        // SAFETY: native RAII returns its uniquely owned allocation once.
        drop(unsafe { Box::from_raw(owner) });
    }
}

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
/// # Safety
/// Positive length denotes that many initialized, immutable input bytes.
/// No native input borrow survives creation. Nonpositive lengths need no input.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_cc_packet_create(
    input: *const u8,
    length: i32,
) -> *mut xy_vsfilter_compat::cc::Packet {
    let data = if length <= 0 {
        &[]
    } else {
        // SAFETY: caller supplies the complete declared initialized span.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    xy_vsfilter_compat::cc::extract_packet(data, length).map_or(core::ptr::null_mut(), |packet| {
        Box::into_raw(Box::new(packet))
    })
}
/// # Safety
/// owner is null or a live immutable packet owner. Queries lend no references.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_cc_packet_field(
    owner: *const xy_vsfilter_compat::cc::Packet,
    field: u32,
) -> i32 {
    if owner.is_null() {
        return 0;
    }
    // SAFETY: caller keeps the unique owner live through scalar queries.
    let packet = unsafe { &*owner };
    match field {
        0 => packet.position,
        1 => packet.capacity as i32,
        2 => packet.lengths[0] as i32,
        3 => packet.lengths[1] as i32,
        _ => 0,
    }
}
/// # Safety
/// owner is live and immutable. Each nonempty output field provides writable
/// bytes for its queried length, disjoint from the owner and the other field.
/// Outputs may be uninitialized; only their initialized prefixes are written.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_cc_packet_copy(
    owner: *const xy_vsfilter_compat::cc::Packet,
    first: *mut u8,
    second: *mut u8,
) {
    // SAFETY: immutable source owner is disjoint from native output storage.
    let packet = unsafe { &*owner };
    for (field, destination) in [first, second].into_iter().enumerate() {
        if packet.lengths[field] != 0 {
            // SAFETY: outputs have the queried lengths and do not overlap.
            unsafe {
                core::ptr::copy_nonoverlapping(
                    packet.fields[field].as_ptr(),
                    destination,
                    packet.lengths[field],
                )
            };
        }
    }
}
/// # Safety
/// owner is null or the unique live create result, released once after all uses.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_cc_packet_release(owner: *mut xy_vsfilter_compat::cc::Packet) {
    if !owner.is_null() {
        // SAFETY: native RAII ends all queries and copies before release.
        drop(unsafe { Box::from_raw(owner) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn packet_plan_owns_fields_and_writes_only_the_queried_prefixes() {
        let mut input = [
            0, 0, 1, 0xb2, 0x43, 0x43, 1, 0xf8, 4, 255, 65, 66, 255, 67, 68, 255, 69, 70, 0, 7, 8,
        ];
        // SAFETY: initialized input lives through creation only; two disjoint
        // output arrays exceed the queried prefix lengths. Release ends the
        // owned plan once, after all queries and copies.
        unsafe {
            let owner = xy_vsfilter_cc_packet_create(input.as_ptr(), input.len() as i32);
            assert!(!owner.is_null());
            input.fill(0);
            assert_eq!(xy_vsfilter_cc_packet_field(owner, 0), 0);
            assert_eq!(xy_vsfilter_cc_packet_field(owner, 1), 4);
            assert_eq!(xy_vsfilter_cc_packet_field(owner, 2), 4);
            assert_eq!(xy_vsfilter_cc_packet_field(owner, 3), 2);
            let mut first = [0xa5; 8];
            let mut second = [0x5a; 8];
            xy_vsfilter_cc_packet_copy(owner, first.as_mut_ptr(), second.as_mut_ptr());
            xy_vsfilter_cc_packet_release(owner);
            assert_eq!(first, [65, 66, 69, 70, 0xa5, 0xa5, 0xa5, 0xa5]);
            assert_eq!(second, [67, 68, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a]);
            let short = [0, 0, 1, 0xb2, 0x43, 0x43, 1, 0xf8, 63, 255];
            assert!(xy_vsfilter_cc_packet_create(short.as_ptr(), short.len() as i32).is_null());
            let empty = xy_vsfilter_cc_packet_create(core::ptr::null(), 0);
            assert!(!empty.is_null());
            xy_vsfilter_cc_packet_copy(empty, core::ptr::null_mut(), core::ptr::null_mut());
            xy_vsfilter_cc_packet_release(empty);
        }
    }
    #[test]
    fn displayed_text_outlives_the_native_cells() {
        let mut cells = [0u16; 528];
        cells[0] = 0xd800;
        cells[2] = 0xffff;
        // SAFETY: initialized display loaned only to creation; subsequent
        // queries access owned text before the single release.
        unsafe {
            let owner = xy_vsfilter_cc_text_create(cells.as_ptr());
            cells.fill(7);
            assert_eq!(xy_vsfilter_cc_text_field(owner, 0, 0), 1);
            assert_eq!(xy_vsfilter_cc_text_field(owner, 1, 0), 4);
            assert_eq!(xy_vsfilter_cc_text_field(owner, 2, 0), 0xd800);
            assert_eq!(xy_vsfilter_cc_text_field(owner, 2, 1), 32);
            assert_eq!(xy_vsfilter_cc_text_field(owner, 2, usize::MAX), 0);
            xy_vsfilter_cc_text_release(owner);
        }
    }

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
