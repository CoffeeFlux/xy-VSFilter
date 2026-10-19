// SPDX-License-Identifier: GPL-2.0-or-later
//! Checked font ownership. Native callers borrow only a Windows HFONT scalar,
//! never the Rust owner or cache entry. The owner must outlive new uses of the
//! borrowed handle; existing DC selection may survive final release.
use crate::document_registry::{Boundary, Error, Kind, Resource, DOCUMENTS};
use crate::windows_font::Font;
use xy_vsfilter_compat::{font_intern as core_font, font_key_wire as wire};
fn error(e: Error) -> u32 {
    match e {
        Error::Busy => 2,
        Error::Exhausted => 5,
        Error::Unresolved => 6,
        _ => 1,
    }
}
fn core_error(e: core_font::Error) -> u32 {
    match e {
        core_font::Error::Identity => 1,
        core_font::Error::Capacity => 5,
        core_font::Error::Construction => 6,
    }
}
/// # Safety
/// Input spans length initialized bytes, output spans eight writable bytes.
/// dc is a live, exclusively borrowed renderer HDC for this complete call.
/// All sharing of this DC and all font selection/release must be serialized.
/// Input/output may overlap; input is decoded and owned before output writes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_font_create_v1(
    input: *const u8,
    length: usize,
    dc: usize,
    output: *mut u8,
) -> u32 {
    if length > isize::MAX as usize {
        return 5;
    }
    if input.is_null() || output.is_null() || dc == 0 || length < wire::HEADER {
        return 3;
    }
    // SAFETY: caller supplies the checked initialized span. Decoder owns its
    // complete result before either registry mutation or overlapping output.
    let key = match wire::decode(unsafe { core::slice::from_raw_parts(input, length) }) {
        Ok(v) => v,
        Err(wire::Error::Capacity) => return 5,
        Err(_) => return 3,
    };
    let result = DOCUMENTS.try_with(|r| {
        let id = r.reserve_insert().map_err(error)?;
        let value = r
            .fonts
            .intern_with(
                id,
                key,
                |k| {
                    // SAFETY: export's exclusive DC loan persists through construction.
                    unsafe { Font::new(k, dc) }
                },
                &mut crate::windows_font::key_equal,
            )
            .map_err(core_error)?;
        r.insert_reserved(id, Resource::Font(value));
        Ok(id)
    });
    let id = match result {
        Ok(Ok(id)) => id,
        Ok(Err(e)) => return e,
        Err(e) => return error(e),
    };
    // SAFETY: detached initialized ID and caller's eight writable bytes.
    unsafe {
        core::ptr::copy_nonoverlapping(id.to_le_bytes().as_ptr(), output, 8);
    }
    0
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_font_destroy_v1(id: u64) -> u32 {
    DOCUMENTS
        .destroy(id, Kind::Font)
        .err()
        .map(error)
        .unwrap_or(0)
}
fn read(boundary: &Boundary, id: u64) -> Result<[u8; 16], u32> {
    boundary
        .try_with(|r| r.font(id).map(|v| v.payload().snapshot()).map_err(error))
        .map_err(error)?
}
/// # Safety
/// Output spans 16 writable bytes: HFONT u64 LE, ascent/descent i32 LE. This
/// borrowed Windows handle must not be externally deleted or used for new calls
/// after owner release.
/// Existing DC selection can survive final release in the cleanup release
/// build. No new operation may use this borrowed handle after owner release.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_font_read_v1(id: u64, output: *mut u8) -> u32 {
    if output.is_null() {
        return 3;
    }
    let out = match read(&DOCUMENTS, id) {
        Ok(v) => v,
        Err(e) => return e,
    };
    // SAFETY: detached initialized packet and caller's sixteen writable bytes.
    unsafe {
        core::ptr::copy_nonoverlapping(out.as_ptr(), output, 16);
    }
    0
}
/// # Safety
/// Output spans eight writable bytes for table Count, including expired keys.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_font_count_v1(output: *mut u8) -> u32 {
    if output.is_null() {
        return 3;
    }
    let n = match DOCUMENTS.try_with(|r| r.fonts.count() as u64) {
        Ok(v) => v,
        Err(e) => return error(e),
    };
    // SAFETY: detached initialized count and caller's eight writable bytes.
    unsafe {
        core::ptr::copy_nonoverlapping(n.to_le_bytes().as_ptr(), output, 8);
    }
    0
}

#[cfg(test)]
pub(crate) fn test_exports(document: u64) {
    #[link(name = "gdi32")]
    extern "system" {
        fn CreateCompatibleDC(dc: usize) -> usize;
        fn DeleteDC(dc: usize) -> i32;
        fn GetCurrentObject(dc: usize, kind: u32) -> usize;
    }
    let mut packet = alloc::vec![0u8;34];
    packet[..4].copy_from_slice(&1i32.to_le_bytes());
    packet[4..8].copy_from_slice(&400i32.to_le_bytes());
    packet[12..16].copy_from_slice(&5u32.to_le_bytes());
    packet[16..24].copy_from_slice(&20f64.to_bits().to_le_bytes());
    for (i, v) in [65u16, 114, 105, 97, 108].iter().enumerate() {
        packet[24 + i * 2..26 + i * 2].copy_from_slice(&v.to_le_bytes());
    }
    // SAFETY: this serialized export test owns the DC, initialized packets and
    // sufficient output spans. No returned HFONT escapes or remains selected.
    unsafe {
        let dc = CreateCompatibleDC(0);
        assert_ne!(dc, 0);
        let before = GetCurrentObject(dc, 6);
        let mut out = [0xa5u8; 17];
        assert_eq!(xy_vsfilter_font_read_v1(document, out[1..].as_mut_ptr()), 1);
        assert_eq!(out, [0xa5; 17]);
        assert_eq!(
            xy_vsfilter_font_create_v1(core::ptr::null(), packet.len(), dc, out.as_mut_ptr()),
            3
        );
        assert_eq!(
            xy_vsfilter_font_create_v1(packet.as_ptr(), packet.len(), dc, core::ptr::null_mut()),
            3
        );
        assert_eq!(
            xy_vsfilter_font_create_v1(packet.as_ptr(), packet.len(), 0, out.as_mut_ptr()),
            3
        );
        let mut bad = packet.clone();
        bad[8] = 2;
        assert_eq!(
            xy_vsfilter_font_create_v1(bad.as_ptr(), bad.len(), dc, out.as_mut_ptr()),
            3
        );
        assert_eq!(out, [0xa5; 17]);
        assert_eq!(
            DOCUMENTS.try_with(|_| xy_vsfilter_font_create_v1(
                packet.as_ptr(),
                packet.len(),
                dc,
                out.as_mut_ptr()
            )),
            Ok(2)
        );
        assert_eq!(out, [0xa5; 17]);
        let mut aliased = packet.clone();
        assert_eq!(
            xy_vsfilter_font_create_v1(aliased.as_ptr(), aliased.len(), dc, aliased.as_mut_ptr()),
            0
        );
        let first = u64::from_le_bytes(aliased[..8].try_into().unwrap());
        assert_eq!(
            xy_vsfilter_font_create_v1(packet.as_ptr(), packet.len(), dc, out[1..].as_mut_ptr()),
            0
        );
        let second = u64::from_le_bytes(out[1..9].try_into().unwrap());
        assert_ne!(first, second);
        assert_eq!(GetCurrentObject(dc, 6), before);
        let mut a = [0u8; 16];
        let mut b = [0u8; 16];
        assert_eq!(xy_vsfilter_font_read_v1(first, a.as_mut_ptr()), 0);
        assert_eq!(xy_vsfilter_font_read_v1(second, b.as_mut_ptr()), 0);
        assert_eq!(a, b);
        assert_ne!(&a[..8], &[0; 8]);
        assert_eq!(xy_vsfilter_font_count_v1(out.as_mut_ptr()), 0);
        assert_eq!(u64::from_le_bytes(out[..8].try_into().unwrap()), 1);
        assert_eq!(xy_vsfilter_font_destroy_v1(document), 1);
        assert_eq!(xy_vsfilter_font_destroy_v1(first), 0);
        assert_eq!(xy_vsfilter_font_destroy_v1(first), 1);
        assert_eq!(xy_vsfilter_font_read_v1(second, b.as_mut_ptr()), 0);
        assert_eq!(a, b);
        assert_eq!(xy_vsfilter_font_destroy_v1(second), 0);
        out.fill(0xa5);
        assert_eq!(xy_vsfilter_font_read_v1(second, out[1..].as_mut_ptr()), 1);
        assert_eq!(out, [0xa5; 17]);
        assert_eq!(xy_vsfilter_font_count_v1(out.as_mut_ptr()), 0);
        assert_eq!(u64::from_le_bytes(out[..8].try_into().unwrap()), 0);
        assert_eq!(GetCurrentObject(dc, 6), before);
        assert_ne!(DeleteDC(dc), 0);
    }
}
