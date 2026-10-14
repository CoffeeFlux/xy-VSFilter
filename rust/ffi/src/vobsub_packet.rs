// SPDX-License-Identifier: GPL-3.0-or-later
use xy_vsfilter_compat::vobsub_packet::{parse, Patch};

/// # Safety
/// packet covers length readable allocated bytes; the individual bytes reached
/// by the parser must be initialized. Unread prefixes/gaps/tails may be uninitialized.
/// output is writable for one Patch and disjoint from packet. Native object
/// fields are disjoint from the packet and neither is concurrently modified.
/// Zero leaves output untouched; no native callback or unwind enters this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_vobsub_packet(
    packet: *const u8,
    length: usize,
    data_size: usize,
    time: i32,
    output: *mut Patch,
) -> u32 {
    if length > isize::MAX as usize {
        return 0;
    }
    let result = parse(length, data_size, time, |index| {
        // SAFETY: safe parser checks index against length before requesting it;
        // only an actually consumed initialized byte is read, with no slice loan.
        unsafe { *packet.add(index) }
    });
    let Some(patch) = result else {
        return 0;
    };
    // SAFETY: output is writable and no reference to its old contents exists.
    unsafe {
        output.write(patch);
    }
    1
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::mem::MaybeUninit;
    #[test]
    fn unread_packet_regions_and_patch_output_need_not_be_initialized() {
        let mut packet = [MaybeUninit::<u8>::uninit(); 16];
        for (index, byte) in [(4, 0), (5, 0), (6, 0), (7, 4), (8, 0xff)] {
            packet[index].write(byte);
        }
        let mut output = MaybeUninit::<Patch>::uninit();
        // SAFETY: exactly bytes 4..=8 are consumed, all initialized. Output is
        // writable and disjoint; no borrow includes unused packet bytes.
        unsafe {
            assert_eq!(
                xy_vsfilter_vobsub_packet(
                    packet.as_ptr().cast(),
                    16,
                    4,
                    i32::MAX,
                    output.as_mut_ptr()
                ),
                1
            );
            let patch = output.assume_init();
            assert_eq!(patch.flags, 0);
            assert_eq!(patch.current, 0);
        }
    }
    #[test]
    fn declined_parse_does_not_publish_partial_changes() {
        let packet = [0, 90, 0, 0, 0, 5];
        let mut output = Patch {
            current: -7,
            ..Patch::default()
        };
        // SAFETY: packet/output are valid and disjoint; command 5 lacks its payload.
        unsafe {
            assert_eq!(
                xy_vsfilter_vobsub_packet(packet.as_ptr(), packet.len(), 0, i32::MAX, &mut output),
                0
            );
        }
        assert_eq!(output.current, -7);
        assert_eq!(output.flags, 0);
    }
}
