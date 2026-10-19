// SPDX-License-Identifier: GPL-2.0-or-later
//! Typed rectangle owners and detached fixed-width values. No native pointer
//! identifies an owner and no canonical reference crosses this boundary.
use crate::document_registry::{Boundary, Error, Kind, Resource, DOCUMENTS};
use core::ptr;
fn error(e: Error) -> u32 {
    match e {
        Error::Busy => 2,
        Error::Exhausted => 5,
        _ => 1,
    }
}
fn create(boundary: &Boundary, coordinates: [i32; 4]) -> Result<[u8; 16], u32> {
    boundary
        .try_with(|r| {
            let handle = r.reserve_insert().map_err(error)?;
            let value = r.rectangles.intern(handle, coordinates);
            let mut output = [0; 16];
            output[..8].copy_from_slice(&handle.to_le_bytes());
            output[8..].copy_from_slice(&value.identity().to_le_bytes());
            r.insert_reserved(handle, Resource::Rectangle(value));
            Ok(output)
        })
        .map_err(error)?
}
fn read(boundary: &Boundary, id: u64) -> Result<[u8; 16], u32> {
    boundary
        .try_with(|r| {
            let value = r.rectangle(id).map_err(error)?.coordinates();
            let mut out = [0; 16];
            for (i, n) in value.iter().enumerate() {
                out[i * 4..i * 4 + 4].copy_from_slice(&n.to_le_bytes());
            }
            Ok(out)
        })
        .map_err(error)?
}
/// # Safety
/// Input is 16 readable initialized bytes (four little-endian signed32 values).
/// Output is 16 writable bytes for owner and canonical-identity u64s. Either may
/// be unaligned; output may alias input. Input is copied before output mutation.
/// Caller serializes admission. Null metadata is rejected without output writes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_rect_create_v1(input: *const u8, output: *mut u8) -> u32 {
    if input.is_null() || output.is_null() {
        return 3;
    }
    let mut bytes = [0; 16];
    // SAFETY: exact caller input contract, copied to distinct local storage.
    unsafe {
        ptr::copy_nonoverlapping(input, bytes.as_mut_ptr(), 16);
    }
    let mut coordinates = [0; 4];
    for (i, n) in coordinates.iter_mut().enumerate() {
        *n = i32::from_le_bytes(bytes[i * 4..i * 4 + 4].try_into().unwrap());
    }
    let out = match create(&DOCUMENTS, coordinates) {
        Ok(v) => v,
        Err(e) => return e,
    };
    // SAFETY: fully initialized detached packet and caller's writable output.
    unsafe {
        ptr::copy_nonoverlapping(out.as_ptr(), output, 16);
    }
    0
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_rect_destroy_v1(id: u64) -> u32 {
    DOCUMENTS
        .destroy(id, Kind::Rectangle)
        .err()
        .map(error)
        .unwrap_or(0)
}
/// # Safety
/// Output spans 16 writable bytes, unaligned/uninitialized allowed. Null, stale
/// or wrong-type handles leave output untouched. No foreign memory is retained.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_rect_read_v1(id: u64, output: *mut u8) -> u32 {
    if output.is_null() {
        return 3;
    }
    let out = match read(&DOCUMENTS, id) {
        Ok(v) => v,
        Err(e) => return e,
    };
    // SAFETY: detached value, exact caller output contract, no output read.
    unsafe {
        ptr::copy_nonoverlapping(out.as_ptr(), output, 16);
    }
    0
}
/// # Safety
/// Output spans eight writable bytes for the distinct live-value count, encoded
/// as little-endian u64. Caller serializes this admission. No pointer retained.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_rect_count_v1(output: *mut u8) -> u32 {
    if output.is_null() {
        return 3;
    }
    let count = match DOCUMENTS.try_with(|r| r.rectangles.count() as u64) {
        Ok(v) => v,
        Err(e) => return error(e),
    };
    // SAFETY: detached initialized count and caller's eight-byte destination.
    unsafe {
        ptr::copy_nonoverlapping(count.to_le_bytes().as_ptr(), output, 8);
    }
    0
}
#[cfg(test)]
pub(crate) fn test_exports(document: u64) {
    let mut bytes = [0xa5; 32];
    // SAFETY: all provided spans are live and long enough. Null metadata and
    // stale/wrong-type IDs are rejected before memory writes. This runs from the
    // existing single serialized global export test.
    unsafe {
        assert_eq!(xy_vsfilter_rect_read_v1(document, bytes.as_mut_ptr()), 1);
        assert_eq!(bytes, [0xa5; 32]);
        assert_eq!(
            xy_vsfilter_rect_create_v1(ptr::null(), bytes.as_mut_ptr()),
            3
        );
        assert_eq!(bytes, [0xa5; 32]);
        // Unaligned and aliased input/output.
        assert_eq!(
            xy_vsfilter_rect_create_v1(bytes.as_ptr().add(1), bytes.as_mut_ptr().add(1)),
            0
        );
        let id = u64::from_le_bytes(bytes[1..9].try_into().unwrap());
        assert_eq!(xy_vsfilter_rect_read_v1(id, bytes.as_mut_ptr()), 0);
        assert_eq!(&bytes[..16], &[0xa5; 16]);
        assert_eq!(xy_vsfilter_rect_count_v1(bytes.as_mut_ptr()), 0);
        assert_eq!(u64::from_le_bytes(bytes[..8].try_into().unwrap()), 1);
        assert_eq!(xy_vsfilter_rect_destroy_v1(id), 0);
        assert_eq!(xy_vsfilter_rect_destroy_v1(id), 1);
        bytes.fill(0xa5);
        assert_eq!(xy_vsfilter_rect_read_v1(id, bytes.as_mut_ptr()), 1);
        assert_eq!(bytes, [0xa5; 32]);
        assert_eq!(xy_vsfilter_rect_count_v1(bytes.as_mut_ptr()), 0);
        assert_eq!(u64::from_le_bytes(bytes[..8].try_into().unwrap()), 0);
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn distinct_checked_handles_share_canonical_identity_and_release_last_value() {
        let b = Boundary::new();
        let a = create(&b, [1, 2, 3, 4]).unwrap();
        let c = create(&b, [1, 2, 3, 4]).unwrap();
        assert_ne!(a[..8], c[..8]);
        assert_eq!(a[8..], c[8..]);
        let first = u64::from_le_bytes(a[..8].try_into().unwrap());
        let second = u64::from_le_bytes(c[..8].try_into().unwrap());
        b.destroy(first, Kind::Rectangle).unwrap();
        assert_eq!(read(&b, first), Err(1));
        assert_eq!(b.try_with(|r| r.rectangles.count()).unwrap(), 1);
        let snapshot = read(&b, second).unwrap();
        assert_eq!(
            b.destroy(second, Kind::StringValue),
            Err(Error::TypeMismatch)
        );
        b.destroy(second, Kind::Rectangle).unwrap();
        assert_eq!(b.try_with(|r| r.rectangles.count()).unwrap(), 0);
        assert_eq!(&snapshot[..4], &1i32.to_le_bytes());
        assert_eq!(b.try_with(|_| create(&b, [0; 4])).unwrap(), Err(2));
    }
}
