// SPDX-License-Identifier: GPL-2.0-or-later
//! Checked text owners; the shared registry also owns the module's MRU cache.
use crate::document_registry::{Boundary, Error, Kind, Resource, DOCUMENTS};
use alloc::vec::Vec;
use core::ptr;
fn error(e: Error) -> u32 {
    match e {
        Error::Busy => 2,
        Error::Exhausted => 5,
        _ => 1,
    }
}
fn create(boundary: &Boundary, units: Vec<u16>) -> Result<[u8; 24], u32> {
    boundary
        .try_with(|r| {
            let id = r.reserve_insert().map_err(error)?;
            let value = r.strings.intern(units).map_err(|_| 6u32)?;
            let mut out = [0; 24];
            for (i, n) in [id, value.id() as u64, value.units().len() as u64]
                .iter()
                .enumerate()
            {
                out[i * 8..i * 8 + 8].copy_from_slice(&n.to_le_bytes());
            }
            r.insert_reserved(id, Resource::StringValue(value));
            Ok(out)
        })
        .map_err(error)?
}
fn read(boundary: &Boundary, id: u64) -> Result<Vec<u8>, u32> {
    boundary
        .try_with(|r| {
            Ok(r.string(id)
                .map_err(error)?
                .units()
                .iter()
                .flat_map(|u| u.to_le_bytes())
                .collect())
        })
        .map_err(error)?
}
fn cache(boundary: &Boundary, action: u32, maximum: usize, reset: bool) -> Result<[u8; 32], u32> {
    boundary
        .try_with(|r| {
            match action {
                0 => {}
                1 => r.strings.set_maximum(maximum, reset),
                2 => r.strings.clear(reset),
                3 => r.strings.remove_tail(),
                4 => r.strings.shutdown(),
                _ => return Err(3),
            }
            let mut out = [0; 32];
            for (i, n) in r.strings.stats().iter().enumerate() {
                out[i * 8..i * 8 + 8].copy_from_slice(&(*n as u64).to_le_bytes());
            }
            Ok(out)
        })
        .map_err(error)?
}

/// # Safety
/// Input spans length readable initialized UTF16LE bytes (null allowed for zero).
/// Output spans 24 writable, possibly unaligned/uninitialized bytes. It may alias
/// input: input is copied before any output write. Caller serializes admission.
/// Returns checked owner ID, legacy cache ID, stored unit count as three u64s.
/// No foreign memory or pointer is retained. Failure leaves output untouched.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_string_create_v1(
    input: *const u8,
    length: usize,
    output: *mut u8,
) -> u32 {
    if length > isize::MAX as usize || length / 2 > i32::MAX as usize {
        return 5;
    }
    if length % 2 != 0 || (length != 0 && input.is_null()) || output.is_null() {
        return 3;
    }
    let units = if length == 0 {
        Vec::new()
    } else {
        // SAFETY: preflighted caller-owned initialized range, copied immediately;
        // the borrow ends before registry access or output mutation.
        unsafe { core::slice::from_raw_parts(input, length) }
            .chunks_exact(2)
            .map(|b| u16::from_le_bytes([b[0], b[1]]))
            .collect()
    };
    let bytes = match create(&DOCUMENTS, units) {
        Ok(v) => v,
        Err(e) => return e,
    };
    // SAFETY: detached initialized source and the caller's 24-byte destination.
    unsafe {
        ptr::copy_nonoverlapping(bytes.as_ptr(), output, 24);
    }
    0
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_string_destroy_v1(id: u64) -> u32 {
    DOCUMENTS
        .destroy(id, Kind::StringValue)
        .err()
        .map(error)
        .unwrap_or(0)
}
/// # Safety
/// Output spans capacity writable bytes; null allowed only for capacity zero.
/// Checked ID's complete raw UTF16LE value is copied from detached storage.
/// Wrong/stale IDs and short output leave output untouched. No buffer retained.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_string_read_v1(
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
    if !bytes.is_empty() {
        // SAFETY: capacity was checked, output nonnull, source detached.
        unsafe {
            ptr::copy_nonoverlapping(bytes.as_ptr(), output, bytes.len());
        }
    }
    0
}
/// # Safety
/// Output spans 32 writable bytes. Caller serializes admission. Action 0 reads,
/// 1 resizes, 2 clears, 3 removes the least-recently-used entry, 4 shuts down
/// module cache storage. The optional
/// reset flag clears statistics on actions 1/2. No callback or pointer retained.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_string_cache_v1(
    action: u32,
    maximum: usize,
    reset: u32,
    output: *mut u8,
) -> u32 {
    if output.is_null() || reset > 1 || action > 4 {
        return 3;
    }
    let bytes = match cache(&DOCUMENTS, action, maximum, reset != 0) {
        Ok(v) => v,
        Err(e) => return e,
    };
    // SAFETY: detached local source and caller-owned 32-byte destination.
    unsafe {
        ptr::copy_nonoverlapping(bytes.as_ptr(), output, 32);
    }
    0
}

#[cfg(test)]
pub(crate) fn test_exports(document: u64) {
    let mut buffer = [0xa5; 32];
    // SAFETY: live exact-sized buffers; malformed metadata is rejected before
    // access. Runs within the existing serialized global export test.
    unsafe {
        assert_eq!(xy_vsfilter_string_cache_v1(2, 0, 1, buffer.as_mut_ptr()), 0);
        assert_eq!(
            xy_vsfilter_string_create_v1([65, 0, 0, 0, 66, 0].as_ptr(), 6, buffer.as_mut_ptr()),
            0
        );
        let id = u64::from_le_bytes(buffer[..8].try_into().unwrap());
        assert_eq!(u64::from_le_bytes(buffer[16..24].try_into().unwrap()), 3);
        buffer.fill(0xa5);
        assert_eq!(
            xy_vsfilter_string_read_v1(document, buffer.as_mut_ptr(), 32),
            1
        );
        assert_eq!(xy_vsfilter_string_read_v1(id, buffer.as_mut_ptr(), 4), 4);
        assert_eq!(buffer, [0xa5; 32]);
        assert_eq!(
            xy_vsfilter_string_create_v1(ptr::null(), 2, buffer.as_mut_ptr()),
            3
        );
        assert_eq!(
            xy_vsfilter_string_create_v1(buffer.as_ptr(), 1, buffer.as_mut_ptr()),
            3
        );
        assert_eq!(xy_vsfilter_string_cache_v1(5, 0, 0, buffer.as_mut_ptr()), 3);
        assert_eq!(buffer, [0xa5; 32]);
        assert_eq!(xy_vsfilter_string_cache_v1(2, 0, 0, buffer.as_mut_ptr()), 0);
        assert_eq!(xy_vsfilter_string_read_v1(id, buffer.as_mut_ptr(), 32), 0);
        assert_eq!(&buffer[..6], &[65, 0, 0, 0, 66, 0]);
        // Aliased input/output remains valid because the input is copied first.
        assert_eq!(
            xy_vsfilter_string_create_v1(buffer.as_ptr(), 6, buffer.as_mut_ptr()),
            0
        );
        let other = u64::from_le_bytes(buffer[..8].try_into().unwrap());
        assert_ne!(id, other);
        assert_eq!(xy_vsfilter_string_destroy_v1(id), 0);
        assert_eq!(xy_vsfilter_string_destroy_v1(id), 1);
        buffer.fill(0xa5);
        assert_eq!(xy_vsfilter_string_read_v1(id, buffer.as_mut_ptr(), 32), 1);
        assert_eq!(buffer, [0xa5; 32]);
        assert_eq!(xy_vsfilter_string_destroy_v1(other), 0);
        assert_eq!(
            xy_vsfilter_string_create_v1(ptr::null(), 0, buffer.as_mut_ptr()),
            0
        );
        let empty = u64::from_le_bytes(buffer[..8].try_into().unwrap());
        assert_eq!(xy_vsfilter_string_read_v1(empty, ptr::null_mut(), 0), 0);
        assert_eq!(xy_vsfilter_string_destroy_v1(empty), 0);
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;
    #[test]
    fn checked_owner_survives_eviction_and_cache_clear() {
        let b = Boundary::new();
        let reply = create(&b, vec![1, 0, 2]).unwrap();
        let id = u64::from_le_bytes(reply[..8].try_into().unwrap());
        cache(&b, 2, 0, true).unwrap();
        assert_eq!(read(&b, id).unwrap(), [1, 0, 0, 0, 2, 0]);
        let another = create(&b, vec![1]).unwrap();
        assert_ne!(reply[8..16], another[8..16]);
        b.destroy(id, Kind::StringValue).unwrap();
        assert_eq!(read(&b, id), Err(1));
        cache(&b, 1, 0, false).unwrap();
        assert_eq!(create(&b, vec![]), Err(6));
        assert_eq!(b.try_with(|_| read(&b, id)).unwrap(), Err(2));
    }
}
