// SPDX-License-Identifier: GPL-2.0-or-later
//! Detached bitmap packets under the existing path/overlay owner admission.
use super::PATHS;
use xy_vsfilter_compat::bitmap::Bitmap;

#[cfg(test)]
pub(super) fn check_create_draw(overlay: u64, alpha: u64) -> u64 {
    let mut input = [0u8; 21];
    input[9..13].copy_from_slice(&2i32.to_le_bytes());
    input[13..17].copy_from_slice(&2i32.to_le_bytes());
    let mut id = [0xa5; 10];
    // SAFETY: initialized fixed packets, sufficient unaligned output with guard
    // bytes; the only dangling pointer is on proven empty geometry and not read.
    unsafe {
        assert_eq!(
            xy_vsfilter_bitmap_create_v1(core::ptr::null(), id.as_mut_ptr()),
            3
        );
        assert_eq!(id, [0xa5; 10]);
        assert_eq!(
            xy_vsfilter_bitmap_create_v1(input.as_ptr().add(1), id.as_mut_ptr().add(1)),
            0
        );
        assert_eq!((id[0], id[9]), (0xa5, 0xa5));
        let owner = u64::from_le_bytes(id[1..9].try_into().unwrap());
        let mut params = [0u8; 29];
        params[9..13].copy_from_slice(&2i32.to_le_bytes());
        params[13..17].copy_from_slice(&2i32.to_le_bytes());
        params[25] = 1;
        let mut switches = [0u8; 8];
        switches[..4].copy_from_slice(&0xff224466u32.to_le_bytes());
        switches[4..].fill(255);
        assert_eq!(
            xy_vsfilter_bitmap_draw_v1(
                owner,
                overlay,
                alpha,
                params.as_ptr().add(1),
                switches.as_ptr()
            ),
            0
        );
        params[1..5].copy_from_slice(&9i32.to_le_bytes());
        assert_eq!(
            xy_vsfilter_bitmap_draw_v1(
                owner,
                overlay,
                0,
                params.as_ptr().add(1),
                core::ptr::NonNull::<u8>::dangling().as_ptr()
            ),
            0
        );
        owner
    }
}
#[cfg(test)]
pub(super) fn check_survival_release(id: u64) {
    let mut size = [0u8; 8];
    let mut bytes = [0xa5; 290];
    // SAFETY: initialized outputs sized for a 2x2 packed image packet and guards.
    unsafe {
        assert_eq!(xy_vsfilter_bitmap_info_v1(id, size.as_mut_ptr()), 0);
        assert_eq!(u64::from_le_bytes(size), 288);
        assert_eq!(
            xy_vsfilter_bitmap_read_v1(id, bytes.as_mut_ptr().add(1), 287),
            4
        );
        assert_eq!(bytes, [0xa5; 290]);
        assert_eq!(
            xy_vsfilter_bitmap_read_v1(id, bytes.as_mut_ptr().add(1), 288),
            0
        );
        assert_eq!((bytes[0], bytes[289]), (0xa5, 0xa5));
        assert_eq!(&bytes[33..41], &[0, 0, 0, 255, 102, 68, 34, 0]);
        assert_eq!(&bytes[97..105], &[1, 1, 0, 252, 99, 66, 33, 7]);
        assert_eq!(xy_vsfilter_bitmap_action_v1(id, 0, 0), 0);
        assert_eq!(xy_vsfilter_bitmap_action_v1(id, 1, 0x80402010), 0);
        assert_eq!(xy_vsfilter_bitmap_action_v1(id, 2, 0), 6);
        assert_eq!(xy_vsfilter_bitmap_destroy_v1(id), 0);
        assert_eq!(xy_vsfilter_bitmap_destroy_v1(id), 1);
        bytes.fill(0xa5);
        assert_eq!(xy_vsfilter_bitmap_read_v1(id, bytes.as_mut_ptr(), 290), 1);
        assert_eq!(bytes, [0xa5; 290]);
    }
}

/// # Safety
/// input lends 20 initialized bytes (rectangle and layout), output lends eight
/// writable bytes. Both are disjoint from private owner storage.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_bitmap_create_v1(input: *const u8, output: *mut u8) -> u32 {
    if input.is_null() || output.is_null() {
        return 3;
    }
    let mut bytes = [0u8; 20];
    // SAFETY: caller lends the fixed packet; detach before writing any output.
    unsafe {
        core::ptr::copy_nonoverlapping(input, bytes.as_mut_ptr(), 20);
    }
    let fields: [i32; 5] =
        core::array::from_fn(|i| i32::from_le_bytes(bytes[i * 4..i * 4 + 4].try_into().unwrap()));
    match PATHS.with_owners(|o| {
        o.bitmaps
            .insert(Bitmap::new(fields[..4].try_into().unwrap(), fields[4])?)
    }) {
        Ok(id) => {
            // SAFETY: detached scalar and caller's writable output span.
            unsafe {
                core::ptr::copy_nonoverlapping(id.to_le_bytes().as_ptr(), output, 8);
            }
            0
        }
        Err(e) => e,
    }
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_bitmap_destroy_v1(id: u64) -> u32 {
    PATHS
        .with_owners(|o| o.bitmaps.remove(id))
        .err()
        .unwrap_or(0)
}
/// # Safety
/// output lends eight writable bytes disjoint from private owner storage.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_bitmap_info_v1(id: u64, output: *mut u8) -> u32 {
    if output.is_null() {
        return 3;
    }
    match PATHS.with_owners(|o| o.bitmaps.get(id)?.wire_len()) {
        Ok(n) => {
            // SAFETY: checked caller packet and detached length.
            unsafe {
                core::ptr::copy_nonoverlapping((n as u64).to_le_bytes().as_ptr(), output, 8);
            }
            0
        }
        Err(e) => e,
    }
}
/// # Safety
/// output lends capacity writable bytes disjoint from private owner storage.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_bitmap_read_v1(
    id: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    if output.is_null() || capacity > isize::MAX as usize {
        return 3;
    }
    PATHS
        .with_owners(|o| {
            let value = o.bitmaps.get(id)?;
            if value.wire_len()? > capacity {
                return Err(4);
            }
            let bytes = value.encode()?;
            // SAFETY: checked capacity and detached initialized packet.
            unsafe {
                core::ptr::copy_nonoverlapping(bytes.as_ptr(), output, bytes.len());
            }
            Ok(())
        })
        .err()
        .unwrap_or(0)
}
/// # Safety
/// parameters lends 28 initialized bytes (clip, subpixel origin, body/border and
/// addition flags). For an active nonempty draw, switches lends two initialized
/// DWORDs for single color or six for multiple colors. No private storage alias.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_bitmap_draw_v1(
    id: u64,
    overlay: u64,
    alpha: u64,
    parameters: *const u8,
    switches: *const u8,
) -> u32 {
    if parameters.is_null() {
        return 3;
    }
    let mut bytes = [0u8; 28];
    // SAFETY: caller lends the fixed input packet, detached before admission.
    unsafe {
        core::ptr::copy_nonoverlapping(parameters, bytes.as_mut_ptr(), 28);
    }
    let f: [i32; 7] =
        core::array::from_fn(|i| i32::from_le_bytes(bytes[i * 4..i * 4 + 4].try_into().unwrap()));
    if f[6] & !7 != 0 {
        return 6;
    }
    PATHS
        .with_owners(|o| {
            if switches.is_null() || f[6] & 3 == 0 || id == 0 {
                return Ok(());
            }
            let image = if overlay == 0 {
                None
            } else {
                Some(o.rasters.get(overlay)?)
            };
            let bitmap = o.bitmaps.get_mut(id)?;
            let Some(plan) = bitmap.prepare(
                image,
                f[..4].try_into().unwrap(),
                [f[4], f[5]],
                true,
                f[6] & 4 != 0,
            )?
            else {
                return Ok(());
            };
            let mut raw = [0u8; 24];
            // SAFETY: nonempty active caller supplies the first two DWORDs.
            unsafe {
                core::ptr::copy_nonoverlapping(switches, raw.as_mut_ptr(), 8);
            }
            if raw[4..8] != [255; 4] {
                // SAFETY: production multi-color arrays contain six initialized DWORDs.
                unsafe {
                    core::ptr::copy_nonoverlapping(switches.add(8), raw.as_mut_ptr().add(8), 16);
                }
            }
            let words: [u32; 6] = core::array::from_fn(|i| {
                u32::from_le_bytes(raw[i * 4..i * 4 + 4].try_into().unwrap())
            });
            bitmap.draw(&plan, o.composite_masks.get(alpha)?, &words)
        })
        .err()
        .unwrap_or(0)
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_bitmap_action_v1(id: u64, action: u32, color: u32) -> u32 {
    PATHS
        .with_owners(|o| {
            let value = o.bitmaps.get_mut(id)?;
            match action {
                0 => value.flip_alpha(),
                1 => value.debug_box(color),
                _ => Err(6),
            }
        })
        .err()
        .unwrap_or(0)
}
