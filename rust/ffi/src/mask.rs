// SPDX-License-Identifier: GPL-2.0-or-later
fn fade_length(width: i32, height: i32) -> Option<usize> {
    if width <= 0 || height <= 0 {
        return None;
    }
    usize::try_from(width.checked_mul(height)?).ok()
}

/// # Safety
/// data exclusively addresses width*height initialized writable bytes for a
/// positive accepted layout. Rejected layouts do not borrow or change data.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_banner_fade(
    data: *mut u8,
    width: i32,
    height: i32,
    fade: i32,
    delta: i32,
) -> u32 {
    let Some(length) = fade_length(width, height) else {
        return 0;
    };
    if data.is_null() {
        return 0;
    }
    // SAFETY: checked positive layout; native mask is initialized in full.
    let bytes = unsafe { core::slice::from_raw_parts_mut(data, length) };
    u32::from(xy_vsfilter_compat::mask::banner_fade(
        bytes,
        width as usize,
        fade,
        delta,
    ))
}

/// # Safety
/// Same initialized exclusive plane contract as banner_fade. The native caller
/// retains geometry/conversion ordering and invokes one pass at a time.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_scroll_fade(
    data: *mut u8,
    width: i32,
    height: i32,
    first: i32,
    last: i32,
    alpha: i32,
    delta: i32,
    flags: u32,
) -> u32 {
    let Some(length) = fade_length(width, height) else {
        return 0;
    };
    if data.is_null() {
        return 0;
    }
    // SAFETY: checked layout; core preflights the complete pass before writes.
    let bytes = unsafe { core::slice::from_raw_parts_mut(data, length) };
    u32::from(xy_vsfilter_compat::mask::scroll_fade(
        bytes,
        width as usize,
        first,
        last,
        alpha,
        delta,
        flags & 1 != 0,
        flags & 2 != 0,
    ))
}

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
    fn initialized_plane_loan_survives_ordered_scroll_passes() {
        let owner = crate::plane::xy_vsfilter_plane_create(12);
        assert!(!owner.is_null());
        // SAFETY: one live plane loan, initialized in full, then two sequential
        // exclusive FFI borrows; native view ends before owner release.
        unsafe {
            let data = crate::plane::xy_vsfilter_plane_loan(owner);
            core::ptr::write_bytes(data, 64, 12);
            assert_eq!(xy_vsfilter_scroll_fade(data, 3, 4, 1, 3, 0, 8192, 1), 1);
            assert_eq!(
                core::slice::from_raw_parts(data, 12),
                &[0, 0, 0, 0, 0, 0, 32, 32, 32, 64, 64, 64]
            );
            assert_eq!(
                xy_vsfilter_scroll_fade(data, 3, 4, 2, 4, 16384, -8192, 2),
                1
            );
            assert_eq!(core::slice::from_raw_parts(data.add(6), 6), &[32; 6]);
            assert_eq!(xy_vsfilter_banner_fade(data, 3, 4, 4, 4096), 1);
            assert_eq!(core::slice::from_raw_parts(data.add(6), 3), &[0, 4, 4]);
            crate::plane::xy_vsfilter_plane_release(owner);
        }
    }
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
