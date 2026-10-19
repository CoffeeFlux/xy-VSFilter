// SPDX-License-Identifier: GPL-2.0-or-later
//! Checked canonical style owners. Native input/output are copied wire values;
//! no canonical pointer or rectangle ownership reference crosses this boundary.
use crate::document_registry::{Boundary, Error, Kind, Resource, DOCUMENTS};
use alloc::vec::Vec;
use core::ptr;
use xy_vsfilter_compat::{document_style as wire, style_intern};
fn error(e: Error) -> u32 {
    match e {
        Error::Busy => 2,
        Error::Exhausted => 5,
        Error::Unresolved => 6,
        _ => 1,
    }
}
fn style_error(e: style_intern::Error) -> u32 {
    match e {
        style_intern::Error::Capacity => 5,
        style_intern::Error::ExpiredKey => 6,
        style_intern::Error::Identity => 1,
    }
}
fn create(boundary: &Boundary, value: wire::StyleValue) -> Result<[u8; 24], u32> {
    boundary
        .try_with(|r| {
            let id = r.reserve_insert().map_err(error)?;
            let value = r
                .styles
                .intern_with(
                    id,
                    value,
                    &mut r.rectangles,
                    &mut crate::style_compare::equal,
                )
                .map_err(style_error)?;
            // Every stored style came through the checked wire decoder, including
            // any earlier canonical value selected by this lookup.
            let size = wire::HEADER_BYTES + value.style().font_name.len() * 2;
            let mut out = [0; 24];
            for (i, n) in [id, value.identity(), size as u64].iter().enumerate() {
                out[i * 8..i * 8 + 8].copy_from_slice(&n.to_le_bytes());
            }
            r.insert_reserved(id, Resource::StyleValue(value));
            Ok(out)
        })
        .map_err(error)?
}
fn read(boundary: &Boundary, id: u64) -> Result<Vec<u8>, u32> {
    boundary
        .try_with(|r| wire::encode(r.style_value(id).map_err(error)?.style()).map_err(|_| 3))
        .map_err(error)?
}
/// # Safety
/// Input spans length readable initialized style-wire bytes; output spans 24
/// writable bytes for checked ID, canonical identity and packet size (u64 LE).
/// Unaligned and aliased buffers are permitted: decode owns input before output.
/// Complete calls including release/drop must be serialized by the caller.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_style_create_v1(
    input: *const u8,
    length: usize,
    output: *mut u8,
) -> u32 {
    if length > isize::MAX as usize {
        return 5;
    }
    if input.is_null() || output.is_null() || length < wire::HEADER_BYTES {
        return 3;
    }
    // SAFETY: checked metadata plus caller's initialized range; decoder copies
    // all data, ending this borrow before any output write or registry mutation.
    let value = match wire::decode(unsafe { core::slice::from_raw_parts(input, length) }) {
        Ok(v) => v,
        Err(_) => return 3,
    };
    let out = match create(&DOCUMENTS, value) {
        Ok(v) => v,
        Err(e) => return e,
    };
    // SAFETY: detached initialized packet and caller's exact writable span.
    unsafe {
        ptr::copy_nonoverlapping(out.as_ptr(), output, 24);
    }
    0
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_style_destroy_v1(id: u64) -> u32 {
    DOCUMENTS
        .destroy(id, Kind::StyleValue)
        .err()
        .map(error)
        .unwrap_or(0)
}
/// # Safety
/// Output spans capacity writable bytes (null permitted only at zero capacity).
/// Reads return a detached style packet. Failure leaves output unchanged.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_style_read_v1(
    id: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    if capacity > isize::MAX as usize {
        return 5;
    }
    if capacity != 0 && output.is_null() {
        return 3;
    }
    let bytes = match read(&DOCUMENTS, id) {
        Ok(v) => v,
        Err(e) => return e,
    };
    if bytes.len() > capacity {
        return 4;
    }
    // SAFETY: validated writable size, nonempty detached initialized source.
    unsafe {
        ptr::copy_nonoverlapping(bytes.as_ptr(), output, bytes.len());
    }
    0
}
/// # Safety
/// Output spans eight writable bytes for the canonical table count (u64 LE),
/// including expired non-reflexive entries whose native Count stays visible.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_style_count_v1(output: *mut u8) -> u32 {
    if output.is_null() {
        return 3;
    }
    let count = match DOCUMENTS.try_with(|r| r.styles.count() as u64) {
        Ok(v) => v,
        Err(e) => return error(e),
    };
    // SAFETY: detached initialized count and caller's eight-byte output.
    unsafe {
        ptr::copy_nonoverlapping(count.to_le_bytes().as_ptr(), output, 8);
    }
    0
}
#[cfg(test)]
pub(crate) fn test_exports(document: u64) {
    let value = xy_vsfilter_compat::ass_style_value::Builder::default().value;
    let packet = wire::encode(&value).unwrap();
    let mut buffer = alloc::vec![0xa5;packet.len()+2];
    // SAFETY: live, sufficient spans. Null/stale/wrong-type metadata must refuse
    // before writing; input and output deliberately alias at an unaligned offset.
    unsafe {
        assert_eq!(
            xy_vsfilter_style_read_v1(document, buffer.as_mut_ptr(), buffer.len()),
            1
        );
        assert!(buffer.iter().all(|v| *v == 0xa5));
        assert_eq!(
            xy_vsfilter_style_create_v1(ptr::null(), packet.len(), buffer.as_mut_ptr()),
            3
        );
        buffer[1..1 + packet.len()].copy_from_slice(&packet);
        assert_eq!(
            xy_vsfilter_style_create_v1(
                buffer.as_ptr().add(1),
                packet.len(),
                buffer.as_mut_ptr().add(1)
            ),
            0
        );
        let id = u64::from_le_bytes(buffer[1..9].try_into().unwrap());
        buffer.fill(0xa5);
        assert_eq!(
            xy_vsfilter_style_read_v1(id, buffer.as_mut_ptr(), packet.len() - 1),
            4
        );
        assert!(buffer.iter().all(|v| *v == 0xa5));
        assert_eq!(
            xy_vsfilter_style_read_v1(id, buffer.as_mut_ptr(), buffer.len()),
            0
        );
        assert_eq!(&buffer[..packet.len()], packet);
        assert_eq!(xy_vsfilter_style_count_v1(buffer.as_mut_ptr()), 0);
        assert_eq!(u64::from_le_bytes(buffer[..8].try_into().unwrap()), 1);
        assert_eq!(xy_vsfilter_style_destroy_v1(id), 0);
        assert_eq!(xy_vsfilter_style_destroy_v1(id), 1);
        buffer.fill(0xa5);
        assert_eq!(
            xy_vsfilter_style_read_v1(id, buffer.as_mut_ptr(), buffer.len()),
            1
        );
        assert!(buffer.iter().all(|v| *v == 0xa5));
        assert_eq!(xy_vsfilter_style_count_v1(buffer.as_mut_ptr()), 0);
        assert_eq!(u64::from_le_bytes(buffer[..8].try_into().unwrap()), 0);
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn aliases_release_rectangles_and_expired_native_keys_are_reported() {
        let b = Boundary::new();
        let value = xy_vsfilter_compat::ass_style_value::Builder::default().value;
        let a = create(&b, value.clone()).unwrap();
        let c = create(&b, value.clone()).unwrap();
        assert_ne!(a[..8], c[..8]);
        assert_eq!(a[8..], c[8..]);
        let first = u64::from_le_bytes(a[..8].try_into().unwrap());
        let second = u64::from_le_bytes(c[..8].try_into().unwrap());
        assert_eq!(b.destroy(first, Kind::Rectangle), Err(Error::TypeMismatch));
        b.destroy(first, Kind::StyleValue).unwrap();
        assert!(read(&b, second).is_ok());
        assert_eq!(
            b.try_with(|r| (r.styles.count(), r.rectangles.count()))
                .unwrap(),
            (1, 1)
        );
        b.destroy(second, Kind::StyleValue).unwrap();
        assert_eq!(
            b.try_with(|r| (r.styles.count(), r.rectangles.count()))
                .unwrap(),
            (0, 0)
        );
        let mut nan = value;
        nan.font_size = f64::from_bits(0x7ff8000000000001);
        let n = create(&b, nan.clone()).unwrap();
        b.destroy(
            u64::from_le_bytes(n[..8].try_into().unwrap()),
            Kind::StyleValue,
        )
        .unwrap();
        assert_eq!(
            b.try_with(|r| (r.styles.count(), r.rectangles.count()))
                .unwrap(),
            (1, 0)
        );
        assert_eq!(create(&b, nan), Err(6));
    }
}
