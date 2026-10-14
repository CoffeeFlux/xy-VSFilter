// SPDX-License-Identifier: GPL-2.0-or-later
/// # Safety
/// For a positive accepted count, source holds count initialized bytes and
/// destination holds count writable bytes. Access is exclusive until return.
/// Overlapping ranges decline without creating references or changing bytes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_clip_row(
    destination: *mut u8,
    source: *const u8,
    count: usize,
    inverse: u32,
) -> u32 {
    if count == 0 {
        return 1;
    }
    if count > isize::MAX as usize || destination.is_null() || source.is_null() {
        return 0;
    }
    let Some(destination_end) = (destination as usize).checked_add(count) else {
        return 0;
    };
    let Some(source_end) = (source as usize).checked_add(count) else {
        return 0;
    };
    if (destination as usize) < source_end && (source as usize) < destination_end {
        return 0;
    }
    // SAFETY: disjoint ranges checked above; output may be uninitialized, and
    // source initialization and both logical extents are caller obligations.
    let output = unsafe { core::slice::from_raw_parts_mut(destination.cast(), count) };
    let input = unsafe { core::slice::from_raw_parts(source, count) };
    u32::from(xy_vsfilter_compat::mask::clip_row(
        output,
        input,
        inverse != 0,
    ))
}

#[no_mangle]
pub extern "C" fn xy_vsfilter_mask_scalar(
    source: u8,
    body: u8,
    mask: u8,
    color_alpha: i32,
    flags: u32,
) -> u8 {
    xy_vsfilter_compat::mask::scalar(source, body, mask, color_alpha, flags)
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct MaskBytes {
    pub low: u64,
    pub high: u64,
}
impl MaskBytes {
    fn bytes(self) -> [u8; 16] {
        let mut bytes = [0; 16];
        bytes[..8].copy_from_slice(&self.low.to_le_bytes());
        bytes[8..].copy_from_slice(&self.high.to_le_bytes());
        bytes
    }
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_mask_block(
    source: MaskBytes,
    body: MaskBytes,
    mask: MaskBytes,
    color_alpha: i32,
    flags: u32,
    mmx: u32,
) -> MaskBytes {
    let bytes = xy_vsfilter_compat::mask::block(
        source.bytes(),
        body.bytes(),
        mask.bytes(),
        color_alpha,
        flags,
        mmx != 0,
    );
    let mut low = [0; 8];
    let mut high = [0; 8];
    low.copy_from_slice(&bytes[..8]);
    high.copy_from_slice(&bytes[8..]);
    MaskBytes {
        low: u64::from_le_bytes(low),
        high: u64::from_le_bytes(high),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn clip_rows_initialize_only_active_bytes_and_decline_aliases() {
        use core::mem::MaybeUninit;
        let mut source = [MaybeUninit::<u8>::uninit(); 272];
        let mut output = [MaybeUninit::<u8>::uninit(); 272];
        for (index, byte) in source[8..264].iter_mut().enumerate() {
            byte.write(index as u8);
        }
        // SAFETY: only active initialized source bytes are borrowed; both
        // surrounding regions remain uninitialized and are never read.
        unsafe {
            let input = source.as_ptr().add(8).cast::<u8>();
            let destination = output.as_mut_ptr().add(8).cast::<u8>();
            for inverse in [0, 1] {
                assert_eq!(xy_vsfilter_clip_row(destination, input, 256, inverse), 1);
                for i in 0..256 {
                    assert_eq!(
                        destination.add(i).read(),
                        if inverse == 0 {
                            i as u8
                        } else {
                            64u8.wrapping_sub(i as u8)
                        }
                    );
                }
            }
            assert_eq!(
                xy_vsfilter_clip_row(destination, destination.add(1), 255, 1),
                0
            );
            assert_eq!(destination.read(), 64);
            assert_eq!(
                xy_vsfilter_clip_row(core::ptr::null_mut(), core::ptr::null(), 0, 1),
                1
            );
        }
    }
    #[test]
    fn group_results_return_by_value_without_native_buffer_borrows() {
        assert_eq!(core::mem::size_of::<MaskBytes>(), 16);
        let source = MaskBytes {
            low: 0x1010101010101010,
            high: 0x1010101010101010,
        };
        let zero = MaskBytes { low: 0, high: 0 };
        let mask = MaskBytes {
            low: u64::MAX,
            high: u64::MAX,
        };
        assert_eq!(
            xy_vsfilter_mask_block(source, zero, mask, 64, 2, 0).bytes(),
            [64; 16]
        );
        assert_eq!(
            xy_vsfilter_mask_block(source, zero, mask, 64, 2, 1).bytes(),
            [255; 16]
        );
    }
}
