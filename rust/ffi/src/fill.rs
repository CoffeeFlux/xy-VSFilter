// SPDX-License-Identifier: GPL-2.0-or-later
use xy_vsfilter_compat::fill::{packed_row, plane_row};

fn byte_length(width: i32, bytes_per_pixel: usize) -> Option<usize> {
    let length = usize::try_from(width).ok()?.checked_mul(bytes_per_pixel)?;
    (length > 0 && length <= isize::MAX as usize).then_some(length)
}

/// # Safety
/// For positive width, dst points to width * 4 live, initialized, exclusively
/// accessible bytes. The borrow ends on return; no storage is retained.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_fill_packed(dst: *mut u8, width: i32, alpha: u8, color: u32) {
    if let Some(length) = byte_length(width, 4) {
        // SAFETY: the native row adapter lends exactly this live writable range.
        packed_row(
            unsafe { core::slice::from_raw_parts_mut(dst, length) },
            alpha,
            color,
        );
    }
}

/// # Safety
/// For positive width, dst points to width live, initialized, exclusively
/// accessible bytes. Separate row calls may overlap; no borrow survives a call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_fill_plane(dst: *mut u8, width: i32, alpha: u8, value: u8) {
    if let Some(length) = byte_length(width, 1) {
        // SAFETY: the native row adapter lends exactly this live writable range.
        plane_row(
            unsafe { core::slice::from_raw_parts_mut(dst, length) },
            alpha,
            value,
        );
    }
}

/// # Safety
/// dst points to 64 live, initialized, exclusively accessible bytes. Alpha is
/// supplied by value and may have been copied from this same destination before
/// the call. No borrow or pointer survives the return.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_fill_masked16(dst: *mut u8, low: u64, high: u64, color: u32) {
    let mut alpha = [0; 16];
    alpha[..8].copy_from_slice(&low.to_le_bytes());
    alpha[8..].copy_from_slice(&high.to_le_bytes());
    // SAFETY: the native SIMD-group adapter lends a live exclusive 64-byte row.
    xy_vsfilter_compat::fill::masked_block(
        unsafe { core::slice::from_raw_parts_mut(dst, 64) },
        alpha,
        color,
    );
}

#[no_mangle]
pub extern "C" fn xy_vsfilter_add_pixel(dst: u32, alpha: u8, color: u32) -> u32 {
    xy_vsfilter_compat::fill::add_pixel(dst, alpha, color)
}

/// # Safety
/// dst lends 64 initialized writable exclusive bytes for this call. Alpha was
/// copied to scalars before the call, including when it overlaps destination.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_add_masked16(dst: *mut u8, low: u64, high: u64, color: u32) {
    let mut alpha = [0; 16];
    alpha[..8].copy_from_slice(&low.to_le_bytes());
    alpha[8..].copy_from_slice(&high.to_le_bytes());
    // SAFETY: the native group adapter lends exactly 16 initialized pixels.
    xy_vsfilter_compat::fill::add_block(
        unsafe { core::slice::from_raw_parts_mut(dst, 64) },
        alpha,
        color,
    );
}

/// # Safety
/// dst lends 16 initialized writable exclusive bytes for this call. No source
/// pointer crosses the boundary; alpha snapshots may come from the destination.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_plane_masked16(dst: *mut u8, low: u64, high: u64, value: u8) {
    let mut alpha = [0; 16];
    alpha[..8].copy_from_slice(&low.to_le_bytes());
    alpha[8..].copy_from_slice(&high.to_le_bytes());
    // SAFETY: the native group adapter lends exactly 16 initialized bytes.
    xy_vsfilter_compat::fill::masked_plane_block(
        unsafe { core::slice::from_raw_parts_mut(dst, 16) },
        alpha,
        value,
    );
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn row_borrows_end_before_overlapping_later_calls() {
        let mut row = [1, 2, 3, 4, 5, 6, 7, 8];
        // SAFETY: each call borrows a live exclusive subrange only for its duration.
        unsafe {
            xy_vsfilter_fill_plane(row.as_mut_ptr(), 8, 255, 100);
            xy_vsfilter_fill_packed(row.as_mut_ptr().add(4), 1, 255, 0xabcdef12);
            xy_vsfilter_fill_packed(core::ptr::null_mut(), 0, 0, 0);
            xy_vsfilter_fill_plane(core::ptr::null_mut(), -1, 0, 0);
        }
        assert_eq!(row, [100, 100, 100, 100, 0x12, 0xef, 0xcd, 0]);
    }
}

#[cfg(test)]
mod masked_tests {
    use super::*;
    #[test]
    fn additive_and_plane_snapshots_end_before_overlapping_calls() {
        let mut data = [255; 64];
        let low = u64::from_le_bytes(data[..8].try_into().unwrap());
        let high = u64::from_le_bytes(data[8..16].try_into().unwrap());
        // SAFETY: snapshots precede each exclusive, bounded destination loan.
        unsafe { xy_vsfilter_add_masked16(data.as_mut_ptr(), low, high, 0x12345678) };
        for pixel in data.chunks_exact(4) {
            assert_eq!(pixel, [255, 255, 255, 0]);
        }
        let low = u64::from_le_bytes(data[..8].try_into().unwrap());
        let high = u64::from_le_bytes(data[8..16].try_into().unwrap());
        // SAFETY: the previous borrow ended, and this call borrows only 16 bytes.
        unsafe { xy_vsfilter_plane_masked16(data.as_mut_ptr().add(4), low, high, 17) };
        for pixel in data[4..20].chunks_exact(4) {
            assert_eq!(pixel, [17, 17, 17, 0]);
        }
        assert_eq!(data[..4], [255, 255, 255, 0]);
        assert_eq!(data[20..24], [255, 255, 255, 0]);
    }
    #[test]
    fn alpha_snapshot_can_come_from_the_destination() {
        let mut data = [255; 64];
        let low = u64::from_le_bytes(data[..8].try_into().unwrap());
        let high = u64::from_le_bytes(data[8..16].try_into().unwrap());
        // SAFETY: scalar alpha copies precede this exclusive destination borrow.
        unsafe { xy_vsfilter_fill_masked16(data.as_mut_ptr(), low, high, 0x12345678) };
        for pixel in data.chunks_exact(4) {
            assert_eq!(pixel, [0x78, 0x56, 0x34, 0]);
        }
    }
}
