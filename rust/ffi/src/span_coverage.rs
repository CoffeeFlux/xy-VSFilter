// SPDX-License-Identifier: GPL-2.0-or-later
/// # Safety
/// For a nonempty span, plane covers len initialized, exclusively writable bytes
/// in one live allocation, len <= isize::MAX. No callback or pointer is retained.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_span_coverage(
    plane: *mut u8,
    len: usize,
    pitch: i32,
    y: i32,
    x1: i32,
    x2: i32,
) -> u32 {
    if x2 <= x1 {
        return 1;
    }
    if plane.is_null() || len > isize::MAX as usize {
        return 0;
    }
    // SAFETY: native caller establishes the complete initialized plane extent.
    let plane = unsafe { core::slice::from_raw_parts_mut(plane, len) };
    u32::from(xy_vsfilter_compat::span_coverage::accumulate(
        plane, pitch, y, x1, x2,
    ))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn mutable_plane_access_ends_at_return_and_decline_is_transactional() {
        let mut backing = [0xcc; 32];
        // SAFETY: the interior is live, initialized and exclusively borrowed.
        unsafe {
            assert_eq!(
                xy_vsfilter_span_coverage(backing.as_mut_ptr().add(8), 16, 4, 8, -3, 10),
                1
            );
            assert_eq!(&backing[11..14], &[207, 212, 206]);
            assert!(backing[..8]
                .iter()
                .chain(&backing[24..])
                .all(|v| *v == 0xcc));
            let before = backing;
            assert_eq!(
                xy_vsfilter_span_coverage(backing.as_mut_ptr().add(8), 16, 4, 0, -3, 10),
                0
            );
            assert_eq!(backing, before);
            assert_eq!(
                xy_vsfilter_span_coverage(core::ptr::null_mut(), 0, 0, 0, 1, 0),
                1
            );
        }
    }
}
