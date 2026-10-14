// SPDX-License-Identifier: GPL-2.0-or-later
//! Private v1 document boundary for the opt-in experimental native caller.
//! See include/xy_rust_document.h. All canonical access goes through typed IDs
//! and nonblocking admission. Input bytes are owned before admission, and output
//! bytes are staged until every registry loan ends. No explicit host callbacks are
//! made; allocator/new-handler reentry can observe Busy, and its compatible host
//! handling is unresolved.
use alloc::boxed::Box;
use alloc::vec::Vec;
use core::ptr;
use xy_vsfilter_compat::document::{AddMode, Document, Refusal};
use xy_vsfilter_compat::document_style as style_wire;
use xy_vsfilter_compat::document_wire::{self as wire, Counts, EntryHeader, SegmentHeader};
use xy_vsfilter_compat::timeline_search::Mode;

use crate::document_registry::{Error as RegistryError, Kind, Registry, Resource, DOCUMENTS};

const OK: u32 = 0;
const BAD_HANDLE: u32 = 1;
const BUSY: u32 = 2;
const INVALID_INPUT: u32 = 3;
const OUT_OF_RANGE: u32 = 4;
const COUNT_OVERFLOW: u32 = 5;
const UNRESOLVED_REFERENCE: u32 = 6;

macro_rules! required {
    ($value:expr) => {
        match $value {
            Ok(value) => value,
            Err(status) => return status,
        }
    };
}

fn registry_error(error: RegistryError) -> u32 {
    match error {
        RegistryError::Busy => BUSY,
        RegistryError::InvalidHandle | RegistryError::TypeMismatch => BAD_HANDLE,
        RegistryError::Exhausted => COUNT_OVERFLOW,
    }
}
fn refusal(error: Refusal) -> u32 {
    match error {
        Refusal::StaleEntryIndex | Refusal::WebVttEndIterator => UNRESOLVED_REFERENCE,
        Refusal::EntryCountOverflow | Refusal::TimelineCountOverflow => COUNT_OVERFLOW,
        Refusal::EntryIndex | Refusal::SegmentIndex => OUT_OF_RANGE,
        Refusal::InvalidPermutation | Refusal::TimeCountMismatch => INVALID_INPUT,
    }
}
fn wire_error(error: wire::Error) -> u32 {
    match error {
        wire::Error::CountOverflow | wire::Error::LengthOverflow => COUNT_OVERFLOW,
        wire::Error::InvalidShape | wire::Error::InvalidBool | wire::Error::NonzeroReserved => {
            INVALID_INPUT
        }
    }
}
fn style_wire_error(error: style_wire::Error) -> u32 {
    match error {
        style_wire::Error::CountOverflow | style_wire::Error::LengthOverflow => COUNT_OVERFLOW,
        _ => INVALID_INPUT,
    }
}
fn status(result: Result<(), u32>) -> u32 {
    result.err().unwrap_or(OK)
}
fn admitted<T>(
    operation: impl for<'a> FnOnce(&'a mut Registry) -> Result<T, u32>,
) -> Result<T, u32> {
    DOCUMENTS.try_with(operation).map_err(registry_error)?
}
fn mutate(owner: u64, operation: impl FnOnce(&mut Document) -> Result<(), Refusal>) -> u32 {
    status(admitted(|registry| {
        operation(registry.document_mut(owner).map_err(registry_error)?).map_err(refusal)
    }))
}
fn insert(resource: Resource) -> Result<u64, u32> {
    // Do not map the (error, resource) inside admission: a rejected resource must
    // be destroyed after the registry guard has ended.
    match DOCUMENTS.try_with(|registry| registry.insert(resource)) {
        Ok(Ok(id)) => Ok(id),
        Ok(Err((error, resource))) => {
            drop(resource);
            Err(registry_error(error))
        }
        Err(error) => Err(registry_error(error)),
    }
}

// These four helpers are the only raw-buffer boundary. Address/length checks
// cannot prove accessibility or lifetime; those remain the caller's contract.
fn byte_range(pointer: *const u8, length: usize) -> Result<(), u32> {
    if length > isize::MAX as usize
        || (length != 0 && pointer.is_null())
        || (pointer as usize).checked_add(length).is_none()
    {
        Err(INVALID_INPUT)
    } else {
        Ok(())
    }
}
unsafe fn copy_input(pointer: *const u8, length: usize) -> Result<Vec<u8>, u32> {
    byte_range(pointer, length)?;
    if length == 0 {
        return Ok(Vec::new());
    }
    // SAFETY: numeric bounds were checked; caller supplies initialized readable
    // bytes for this entire range. No reference escapes this immediate copy.
    Ok(unsafe { core::slice::from_raw_parts(pointer, length) }.to_vec())
}
fn output_capacity(pointer: *mut u8, capacity: usize, required: usize) -> Result<(), u32> {
    byte_range(pointer.cast_const(), capacity)?;
    if capacity < required {
        Err(INVALID_INPUT)
    } else {
        Ok(())
    }
}
unsafe fn write_output(pointer: *mut u8, bytes: &[u8]) {
    if !bytes.is_empty() {
        // SAFETY: capacity was validated before mutation. Caller owns the live
        // exclusive destination, which cannot alias staged Rust-owned bytes.
        unsafe { ptr::copy_nonoverlapping(bytes.as_ptr(), pointer, bytes.len()) };
    }
}
fn element_shape(length: usize, width: usize) -> Result<(), u32> {
    if length % width != 0 {
        Err(INVALID_INPUT)
    } else if length / width > i32::MAX as usize {
        Err(COUNT_OVERFLOW)
    } else {
        Ok(())
    }
}
fn units(bytes: &[u8]) -> Vec<u16> {
    bytes
        .chunks_exact(2)
        .map(|pair| u16::from_le_bytes([pair[0], pair[1]]))
        .collect()
}

/// # Safety
/// Output follows the byte-buffer contract in xy_rust_document.h, capacity >=8.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_create_v1(output: *mut u8, capacity: usize) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let id = required!(insert(Resource::Document(Box::default())));
    // SAFETY: caller contract and capacity were validated before insertion.
    unsafe { write_output(output, &id.to_le_bytes()) };
    OK
}

#[no_mangle]
pub extern "C" fn xy_vsfilter_document_copy_storage_v1(destination: u64, source: u64) -> u32 {
    let retired = required!(admitted(|registry| {
        registry
            .copy_document(destination, source)
            .map_err(registry_error)
    }));
    drop(retired); // All replaced storage is destroyed after admission.
    OK
}
/// # Safety
/// Input follows the byte-buffer contract; copied before registry admission.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_set_default_style_v1(
    owner: u64,
    input: *const u8,
    length: usize,
) -> u32 {
    // SAFETY: caller supplies the documented live input byte region.
    let input = required!(unsafe { copy_input(input, length) });
    let mut value = Some(required!(
        style_wire::decode(&input).map_err(style_wire_error)
    ));
    // Borrow pending ownership so rejected admission/handles also drop outside.
    let retired = required!(admitted(|registry| {
        let document = registry.document_mut(owner).map_err(registry_error)?;
        Ok(document.replace_default_style(value.take()))
    }));
    drop(retired);
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_copy_default_style_v1(destination: u64, source: u64) -> u32 {
    let retired = required!(admitted(|registry| registry
        .copy_default_style(destination, source)
        .map_err(registry_error)));
    drop(retired);
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_destroy_v1(owner: u64) -> u32 {
    status(
        DOCUMENTS
            .destroy(owner, Kind::Document)
            .map_err(registry_error),
    )
}

/// # Safety
/// Output follows the byte-buffer contract, capacity >=COUNTS_BYTES.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_counts_v1(
    owner: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, wire::COUNTS_BYTES));
    let bytes = required!(admitted(|registry| {
        let document = registry.document(owner).map_err(registry_error)?;
        let counts = Counts {
            entries: u32::try_from(document.entries().len()).map_err(|_| COUNT_OVERFLOW)?,
            segments: u32::try_from(document.segments().len()).map_err(|_| COUNT_OVERFLOW)?,
        };
        wire::encode_counts(&counts).map_err(wire_error)
    }));
    // SAFETY: validated caller destination; admission and canonical loans ended.
    unsafe { write_output(output, &bytes) };
    OK
}

/// # Safety
/// Output follows the byte-buffer contract, capacity >=ENTRY_HEADER_BYTES.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_entry_header_v1(
    owner: u64,
    index: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, wire::ENTRY_HEADER_BYTES));
    let bytes = required!(admitted(|registry| {
        let entry = registry
            .document(owner)
            .map_err(registry_error)?
            .entries()
            .get(index as usize)
            .ok_or(OUT_OF_RANGE)?;
        let header = EntryHeader::from_entry(entry).map_err(wire_error)?;
        wire::encode_entry_header(&header).map_err(wire_error)
    }));
    // SAFETY: caller destination validated; only a staged value remains.
    unsafe { write_output(output, &bytes) };
    OK
}

/// # Safety
/// Output follows the byte-buffer contract, capacity >=SEGMENT_HEADER_BYTES.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_segment_header_v1(
    owner: u64,
    index: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(
        output,
        capacity,
        wire::SEGMENT_HEADER_BYTES
    ));
    let bytes = required!(admitted(|registry| {
        let segment = registry
            .document(owner)
            .map_err(registry_error)?
            .segments()
            .get(index as usize)
            .ok_or(OUT_OF_RANGE)?;
        let header = SegmentHeader::from_segment(segment).map_err(wire_error)?;
        wire::encode_segment_header(&header).map_err(wire_error)
    }));
    // SAFETY: caller destination validated; only a staged value remains.
    unsafe { write_output(output, &bytes) };
    OK
}

/// # Safety
/// Output follows the byte-buffer contract, capacity >=QUERY_BYTES.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_query_time_v1(
    owner: u64,
    mode: u32,
    time: i64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, wire::QUERY_BYTES));
    let mode = match mode {
        0 => Mode::Entry,
        1 => Mode::NextSegment,
        2 => Mode::ContainingSegment,
        _ => return INVALID_INPUT,
    };
    let bytes = required!(admitted(|registry| {
        let query = registry
            .document(owner)
            .map_err(registry_error)?
            .query_time(mode, time)
            .map_err(refusal)?;
        wire::encode_query(&query).map_err(wire_error)
    }));
    // SAFETY: validated destination, and query admission has ended.
    unsafe { write_output(output, &bytes) };
    OK
}

/// # Safety
/// Output follows the byte-buffer contract, capacity >=8.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_snapshot_open_v1(
    owner: u64,
    kind: u32,
    index: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    if kind > 2 {
        return INVALID_INPUT;
    }
    let bytes = required!(admitted(|registry| {
        let document = registry.document(owner).map_err(registry_error)?;
        match kind {
            0 => wire::encode_entry(document.entries().get(index as usize).ok_or(OUT_OF_RANGE)?)
                .map_err(wire_error),
            1 => wire::encode_segment(
                document
                    .segments()
                    .get(index as usize)
                    .ok_or(OUT_OF_RANGE)?,
            )
            .map_err(wire_error),
            _ => {
                if index != 0 {
                    return Err(OUT_OF_RANGE);
                }
                style_wire::encode(document.default_style().ok_or(OUT_OF_RANGE)?)
                    .map_err(style_wire_error)
            }
        }
    }));
    let id = required!(insert(Resource::Snapshot(bytes)));
    // SAFETY: capacity was checked before snapshot insertion, with no loans now.
    unsafe { write_output(output, &id.to_le_bytes()) };
    OK
}

/// # Safety
/// Output follows the byte-buffer contract, capacity >=8.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_snapshot_size_v1(
    snapshot: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let length = required!(admitted(|registry| {
        Ok(registry.snapshot(snapshot).map_err(registry_error)?.len() as u64)
    }));
    // SAFETY: capacity checked; no snapshot loan remains.
    unsafe { write_output(output, &length.to_le_bytes()) };
    OK
}

/// # Safety
/// Output follows the byte-buffer contract for exactly length bytes. Zero length
/// permits null and performs no access; offset must still lie within snapshot.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_snapshot_read_v1(
    snapshot: u64,
    offset: u64,
    output: *mut u8,
    length: usize,
) -> u32 {
    required!(output_capacity(output, length, length));
    let offset = required!(usize::try_from(offset).map_err(|_| OUT_OF_RANGE));
    let end = required!(offset.checked_add(length).ok_or(OUT_OF_RANGE));
    let bytes = required!(admitted(|registry| {
        Ok(registry
            .snapshot(snapshot)
            .map_err(registry_error)?
            .get(offset..end)
            .ok_or(OUT_OF_RANGE)?
            .to_vec())
    }));
    // SAFETY: caller range validated; snapshot borrow ended before this copy.
    unsafe { write_output(output, &bytes) };
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_snapshot_drop_v1(snapshot: u64) -> u32 {
    status(
        DOCUMENTS
            .destroy(snapshot, Kind::Snapshot)
            .map_err(registry_error),
    )
}

/// # Safety
/// Input/output follow the byte-buffer contract. Input is one exact entry wire;
/// output capacity >=8. Overlap is permitted: input is copied before admission.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_add_prepared_v1(
    owner: u64,
    mode: u32,
    input: *const u8,
    length: usize,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let mode = match mode {
        0 => AddMode::Add,
        1 => AddMode::EntryOnly,
        2 => AddMode::WebVtt,
        _ => return INVALID_INPUT,
    };
    // SAFETY: caller lends the bounded readable range; helper snapshots it now.
    let input = required!(unsafe { copy_input(input, length) });
    let entry = required!(wire::decode_entry(&input).map_err(wire_error));
    let index = required!(admitted(|registry| {
        let document = registry.document_mut(owner).map_err(registry_error)?;
        // The v1 wire admits at most INT_MAX entries, stricter than the core's
        // final index INT_MAX. This is a declared experimental transport bound,
        // not a native parity result for an enormous document.
        if document.entries().len() >= i32::MAX as usize
            && entry.start <= entry.end
            && (mode == AddMode::EntryOnly || entry.start != entry.end)
        {
            return Err(COUNT_OVERFLOW);
        }
        document.add_prepared(entry, mode).map_err(refusal)
    }));
    let mut bytes = [0; 8];
    bytes[..4].copy_from_slice(&u32::from(index.is_some()).to_le_bytes());
    // Core Add validates the signed native index domain before any mutation.
    bytes[4..].copy_from_slice(&index.map_or(-1, |index| index as i32).to_le_bytes());
    // SAFETY: output capacity checked before any mutation; admission ended.
    unsafe { write_output(output, &bytes) };
    OK
}

/// # Safety
/// Input follows the byte-buffer contract and has an even raw-UTF16LE length.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_set_prepared_text_v1(
    owner: u64,
    index: u32,
    flag: i32,
    input: *const u8,
    length: usize,
) -> u32 {
    let flag = match flag {
        -1 => None,
        0 => Some(false),
        1 => Some(true),
        _ => return INVALID_INPUT,
    };
    required!(element_shape(length, 2));
    // SAFETY: caller range is copied before registry admission.
    let input = required!(unsafe { copy_input(input, length) });
    let text = units(&input);
    mutate(owner, |document| {
        document.set_prepared_text(index as usize, text, flag)
    })
}

/// # Safety
/// Input follows the byte-buffer contract and has an even raw-UTF16LE length.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_set_prepared_style_v1(
    owner: u64,
    index: u32,
    input: *const u8,
    length: usize,
) -> u32 {
    required!(element_shape(length, 2));
    // SAFETY: caller range is copied before registry admission.
    let input = required!(unsafe { copy_input(input, length) });
    let style = units(&input);
    mutate(owner, |document| {
        document.set_prepared_style(index as usize, style)
    })
}
/// # Safety
/// Both inputs follow the byte-buffer contract and have even raw-UTF16LE byte
/// lengths. Overlap is allowed; both values are owned before admission. CString
/// comparison terminates at first NUL, while replacement preserves all units.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_rename_style_cstring_v1(
    owner: u64,
    old: *const u8,
    old_length: usize,
    new: *const u8,
    new_length: usize,
) -> u32 {
    required!(element_shape(old_length, 2));
    required!(element_shape(new_length, 2));
    // SAFETY: caller provides both readable ranges; neither can change during
    // these copies, and no caller pointer enters the registry closure.
    let old = required!(unsafe { copy_input(old, old_length) });
    let new = required!(unsafe { copy_input(new, new_length) });
    let old = units(&old);
    let new = units(&new);
    mutate(owner, |document| {
        document.rename_style_cstring(&old, &new);
        Ok(())
    })
}

#[no_mangle]
pub extern "C" fn xy_vsfilter_document_set_entry_time_v1(
    owner: u64,
    index: u32,
    start: u32,
    value: i64,
) -> u32 {
    let start = match start {
        0 => false,
        1 => true,
        _ => return INVALID_INPUT,
    };
    mutate(owner, |document| {
        document.set_entry_time(index as usize, start, value)
    })
}

/// # Safety
/// Input follows the byte-buffer contract, containing only u32LE indexes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_reorder_entries_v1(
    owner: u64,
    input: *const u8,
    length: usize,
) -> u32 {
    required!(element_shape(length, 4));
    // SAFETY: caller range is copied before registry admission.
    let input = required!(unsafe { copy_input(input, length) });
    let order: Vec<_> = input
        .chunks_exact(4)
        .map(|bytes| u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]) as usize)
        .collect();
    mutate(owner, |document| document.reorder_entries(&order))
}

/// # Safety
/// Input follows the byte-buffer contract, containing complete i64LE start/end pairs.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_replace_entry_times_v1(
    owner: u64,
    input: *const u8,
    length: usize,
) -> u32 {
    required!(element_shape(length, 16));
    // SAFETY: caller range is copied before registry admission.
    let input = required!(unsafe { copy_input(input, length) });
    let times: Vec<_> = input
        .chunks_exact(16)
        .map(|bytes| {
            let mut start = [0; 8];
            let mut end = [0; 8];
            start.copy_from_slice(&bytes[..8]);
            end.copy_from_slice(&bytes[8..]);
            (i64::from_le_bytes(start), i64::from_le_bytes(end))
        })
        .collect();
    mutate(owner, |document| document.replace_entry_times(&times))
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_rebuild_segments_v1(owner: u64) -> u32 {
    mutate(owner, |document| {
        document.rebuild_segments();
        Ok(())
    })
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_clear_storage_v1(owner: u64) -> u32 {
    mutate(owner, |document| {
        document.clear_entries_and_segments();
        Ok(())
    })
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_remove_entries_v1(owner: u64) -> u32 {
    mutate(owner, |document| {
        document.remove_all_entries();
        Ok(())
    })
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_animation_or_v1(
    owner: u64,
    segment: u32,
    animated: u32,
) -> u32 {
    let animated = match animated {
        0 => false,
        1 => true,
        _ => return INVALID_INPUT,
    };
    mutate(owner, |document| {
        document.feedback_animation(segment as usize, animated)
    })
}

/// # Safety
/// Output follows the byte-buffer contract, capacity >=16, checked before correction.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_correct_last_end_v1(
    owner: u64,
    bound: i64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 16));
    let end = required!(admitted(|registry| {
        Ok(registry
            .document_mut(owner)
            .map_err(registry_error)?
            .correct_last_end(bound))
    }));
    let mut bytes = [0; 16];
    bytes[..4].copy_from_slice(&u32::from(end.is_some()).to_le_bytes());
    bytes[8..].copy_from_slice(&end.unwrap_or(0).to_le_bytes());
    // SAFETY: capacity checked before mutation, with all canonical loans ended.
    unsafe { write_output(output, &bytes) };
    OK
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;
    use xy_vsfilter_compat::document::PreparedEntry;

    fn state(owner: u64) -> Document {
        admitted(|registry| Ok(registry.document(owner).map_err(registry_error)?.clone())).unwrap()
    }
    unsafe fn create() -> u64 {
        let mut bytes = [0xa5; 17];
        // SAFETY: this live byte destination deliberately has an unaligned offset.
        assert_eq!(
            unsafe { xy_vsfilter_document_create_v1(bytes.as_mut_ptr().add(1), 16) },
            OK
        );
        assert_eq!(bytes[0], 0xa5);
        assert_eq!(&bytes[9..], &[0xa5; 8]);
        u64::from_le_bytes(bytes[1..9].try_into().unwrap())
    }
    fn entry(start: i64, end: i64) -> Vec<u8> {
        wire::encode_entry(&PreparedEntry {
            text: vec![0, 0xd800, 0xffff],
            style: vec![0, 65],
            actor: vec![0xdc00],
            effect: vec![],
            unicode: false,
            margins: [-1, 2, -3, 4],
            layer: -1,
            start,
            end,
            readorder: -1,
        })
        .unwrap()
    }
    unsafe fn add(owner: u64, mode: u32, bytes: &[u8]) -> (u32, [u8; 8]) {
        let mut output = [0xa5; 8];
        // SAFETY: live readable input and exclusive full output ranges.
        let status = unsafe {
            xy_vsfilter_document_add_prepared_v1(
                owner,
                mode,
                bytes.as_ptr(),
                bytes.len(),
                output.as_mut_ptr(),
                output.len(),
            )
        };
        (status, output)
    }
    unsafe fn open_snapshot(owner: u64, kind: u32) -> u64 {
        let mut bytes = [0; 8];
        // SAFETY: fully writable byte destination.
        assert_eq!(
            unsafe { xy_vsfilter_document_snapshot_open_v1(owner, kind, 0, bytes.as_mut_ptr(), 8) },
            OK
        );
        u64::from_le_bytes(bytes)
    }
    unsafe fn read_snapshot(id: u64) -> Vec<u8> {
        let mut size = [0; 8];
        // SAFETY: writable size field, followed by allocated exact-size output.
        assert_eq!(
            unsafe { xy_vsfilter_document_snapshot_size_v1(id, size.as_mut_ptr(), 8) },
            OK
        );
        let mut bytes = vec![0; u64::from_le_bytes(size) as usize];
        assert_eq!(
            unsafe {
                xy_vsfilter_document_snapshot_read_v1(id, 0, bytes.as_mut_ptr(), bytes.len())
            },
            OK
        );
        bytes
    }

    #[test]
    fn byte_bounds_and_shapes_reject_without_access() {
        assert_eq!(byte_range(ptr::null(), 0), Ok(()));
        assert_eq!(byte_range(ptr::null(), 1), Err(INVALID_INPUT));
        assert_eq!(
            byte_range(ptr::NonNull::<u8>::dangling().as_ptr(), usize::MAX),
            Err(INVALID_INPUT)
        );
        assert_eq!(byte_range(usize::MAX as *const u8, 1), Err(INVALID_INPUT));
        assert_eq!(element_shape(3, 2), Err(INVALID_INPUT));
        if usize::BITS > 32 {
            let bytes = (i32::MAX as usize + 1).checked_mul(2).unwrap();
            assert_eq!(element_shape(bytes, 2), Err(COUNT_OVERFLOW));
        }
        assert_eq!(element_shape(0, 16), Ok(()));
        // SAFETY: null/zero is explicitly supported and never dereferenced.
        assert_eq!(unsafe { copy_input(ptr::null(), 0) }, Ok(vec![]));
    }

    // All global-export checks share one test to avoid unrelated parallel tests
    // observing private Busy. Registry concurrency is tested on separate stores.
    #[test]
    fn experimental_v1_exports_preserve_ownership_refusals_and_output_atomicity() {
        // SAFETY: allocated byte ranges below are valid for each supplied size;
        // explicit null/oversized checks are rejected before raw accesses.
        unsafe {
            // Default-style transport and ownership are independent of storage.
            let source = create();
            let target = create();
            let mut output = [0xa5; 8];
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(source, 2, 0, output.as_mut_ptr(), 8),
                OUT_OF_RANGE
            );
            assert_eq!(output, [0xa5; 8]);
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(source, 3, 0, output.as_mut_ptr(), 8),
                INVALID_INPUT
            );
            assert_eq!(output, [0xa5; 8]);
            let mut style = vec![0u8; style_wire::HEADER_BYTES + 8];
            style[64..68].copy_from_slice(&4u32.to_le_bytes());
            style[72..80].copy_from_slice(&0x7ff0000000000042u64.to_le_bytes());
            style[192..].copy_from_slice(&[0, 216, 0, 0, 255, 220, 255, 255]);
            assert_eq!(
                xy_vsfilter_document_set_default_style_v1(source, style.as_ptr(), style.len()),
                OK
            );
            let snapshot = open_snapshot(source, 2);
            assert_eq!(read_snapshot(snapshot), style);
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(source, 2, 1, output.as_mut_ptr(), 8),
                OUT_OF_RANGE
            );
            assert_eq!(
                xy_vsfilter_document_set_default_style_v1(snapshot, style.as_ptr(), style.len()),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_set_default_style_v1(source, ptr::null(), 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_copy_default_style_v1(target, snapshot),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_copy_default_style_v1(target, source),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_copy_default_style_v1(source, source),
                OK
            );
            assert_eq!(state(source).default_style(), state(target).default_style());
            let retained = state(target).default_style().cloned();
            style[72..80].copy_from_slice(&(-0.0f64).to_bits().to_le_bytes());
            assert_eq!(
                xy_vsfilter_document_set_default_style_v1(source, style.as_ptr(), style.len()),
                OK
            );
            assert_ne!(state(source).default_style(), retained.as_ref());
            assert_eq!(xy_vsfilter_document_copy_storage_v1(target, source), OK);
            assert_eq!(state(target).default_style(), retained.as_ref());
            assert_eq!(xy_vsfilter_document_clear_storage_v1(target), OK);
            assert_eq!(xy_vsfilter_document_remove_entries_v1(target), OK);
            assert_eq!(state(target).default_style(), retained.as_ref());
            let cloned = state(target).clone();
            assert_eq!(cloned.default_style(), retained.as_ref());
            assert_eq!(xy_vsfilter_document_destroy_v1(source), OK);
            assert_eq!(xy_vsfilter_document_destroy_v1(target), OK);
            assert_ne!(read_snapshot(snapshot), style);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(snapshot), OK);
            let mut sentinel = [0xa5; 80];
            assert_eq!(
                xy_vsfilter_document_create_v1(ptr::null_mut(), 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_create_v1(ptr::null_mut(), 8),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_create_v1(sentinel.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(sentinel, [0xa5; 80]);
            let owner = create();
            let destination = create();
            assert_ne!(owner, destination);
            let encoded = entry(0, 10);
            let original = state(owner);
            assert_eq!(
                xy_vsfilter_document_add_prepared_v1(
                    owner,
                    0,
                    encoded.as_ptr(),
                    encoded.len(),
                    sentinel.as_mut_ptr(),
                    7
                ),
                INVALID_INPUT
            );
            assert_eq!(state(owner), original);
            assert_eq!(sentinel, [0xa5; 80]);
            assert_eq!(
                xy_vsfilter_document_add_prepared_v1(
                    owner,
                    0,
                    ptr::null(),
                    1,
                    sentinel.as_mut_ptr(),
                    8
                ),
                INVALID_INPUT
            );
            assert_eq!(add(owner, 9, &encoded), (INVALID_INPUT, [0xa5; 8]));
            let mut malformed = encoded.clone();
            malformed[60] = 1;
            assert_eq!(add(owner, 0, &malformed), (INVALID_INPUT, [0xa5; 8]));
            malformed = encoded.clone();
            malformed.push(0);
            assert_eq!(add(owner, 0, &malformed), (INVALID_INPUT, [0xa5; 8]));
            assert_eq!(state(owner), original);

            // Input/output alias is allowed: staging finishes before output.
            let mut overlapping = encoded.clone();
            let input = overlapping.as_ptr();
            let output = overlapping.as_mut_ptr();
            assert_eq!(
                xy_vsfilter_document_add_prepared_v1(owner, 0, input, overlapping.len(), output, 8),
                OK
            );
            assert_eq!(&overlapping[..8], &[1, 0, 0, 0, 0, 0, 0, 0]);
            let before = state(owner);
            assert_eq!(before.entries().len(), 1);
            assert_eq!(before.entries()[0].text, [0, 0xd800, 0xffff]);
            assert_eq!(before.entries()[0].readorder, 0);
            assert_eq!(
                xy_vsfilter_document_counts_v1(owner, sentinel.as_mut_ptr(), 8),
                OK
            );
            assert_eq!(
                wire::decode_counts(&sentinel[..8]).unwrap(),
                Counts {
                    entries: 1,
                    segments: 1
                }
            );
            assert_eq!(&sentinel[8..], &[0xa5; 72]);
            sentinel.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_entry_header_v1(owner, 0, sentinel.as_mut_ptr(), 64),
                OK
            );
            assert_eq!(
                wire::decode_entry_header(&sentinel[..64]).unwrap().lengths,
                [3, 2, 1, 0]
            );
            assert_eq!(&sentinel[64..], &[0xa5; 16]);
            sentinel.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_segment_header_v1(owner, 0, sentinel.as_mut_ptr(), 32),
                OK
            );
            assert_eq!(
                wire::decode_segment_header(&sentinel[..32])
                    .unwrap()
                    .member_count,
                1
            );
            sentinel.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_entry_header_v1(owner, 1, sentinel.as_mut_ptr(), 64),
                OUT_OF_RANGE
            );
            assert_eq!(
                xy_vsfilter_document_segment_header_v1(owner, 1, sentinel.as_mut_ptr(), 32),
                OUT_OF_RANGE
            );
            assert_eq!(sentinel, [0xa5; 80]);
            assert_eq!(
                xy_vsfilter_document_query_time_v1(owner, 0, 0, sentinel.as_mut_ptr(), 16),
                OK
            );
            assert_eq!(wire::decode_query(&sentinel[..16]).unwrap().index, 0);
            sentinel.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_query_time_v1(owner, 3, 0, sentinel.as_mut_ptr(), 16),
                INVALID_INPUT
            );
            assert_eq!(sentinel, [0xa5; 80]);

            let entry_snapshot = open_snapshot(owner, 0);
            let segment_snapshot = open_snapshot(owner, 1);
            let entry_bytes = wire::encode_entry(&before.entries()[0]).unwrap();
            let segment_bytes = wire::encode_segment(&before.segments()[0]).unwrap();
            assert_eq!(read_snapshot(entry_snapshot), entry_bytes);
            assert_eq!(read_snapshot(segment_snapshot), segment_bytes);
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(owner, 2, 0, sentinel.as_mut_ptr(), 8),
                OUT_OF_RANGE
            );
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(owner, 0, 0, sentinel.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_snapshot_read_v1(
                    entry_snapshot,
                    entry_bytes.len() as u64,
                    ptr::null_mut(),
                    0
                ),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_snapshot_read_v1(
                    entry_snapshot,
                    entry_bytes.len() as u64 + 1,
                    ptr::null_mut(),
                    0
                ),
                OUT_OF_RANGE
            );
            assert_eq!(
                xy_vsfilter_document_snapshot_read_v1(
                    entry_snapshot,
                    u64::MAX,
                    sentinel.as_mut_ptr(),
                    1
                ),
                OUT_OF_RANGE
            );
            assert_eq!(
                xy_vsfilter_document_snapshot_read_v1(
                    entry_snapshot,
                    entry_bytes.len() as u64 - 1,
                    sentinel.as_mut_ptr(),
                    2
                ),
                OUT_OF_RANGE
            );
            assert_eq!(sentinel, [0xa5; 80]);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(owner), BAD_HANDLE);
            assert_eq!(xy_vsfilter_document_destroy_v1(entry_snapshot), BAD_HANDLE);
            assert_eq!(
                xy_vsfilter_document_counts_v1(entry_snapshot, sentinel.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_snapshot_read_v1(owner, 0, sentinel.as_mut_ptr(), 1),
                BAD_HANDLE
            );
            assert_eq!(sentinel, [0xa5; 80]);

            // Reentry is refused before canonical access or output publication.
            DOCUMENTS
                .try_with(|registry| {
                    let retained = registry.document(owner).unwrap();
                    assert_eq!(
                        xy_vsfilter_document_counts_v1(owner, sentinel.as_mut_ptr(), 8),
                        BUSY
                    );
                    assert_eq!(
                        xy_vsfilter_document_create_v1(sentinel.as_mut_ptr(), 8),
                        BUSY
                    );
                    assert_eq!(
                        xy_vsfilter_document_set_entry_time_v1(owner, 0, 1, 99),
                        BUSY
                    );
                    assert_eq!(sentinel, [0xa5; 80]);
                    assert_eq!(retained, &before);
                })
                .unwrap();

            assert_eq!(xy_vsfilter_document_animation_or_v1(owner, 0, 1), OK);
            assert_eq!(xy_vsfilter_document_animation_or_v1(owner, 0, 0), OK);
            assert!(state(owner).segments()[0].animated);
            let animated = state(owner);
            assert_eq!(
                xy_vsfilter_document_animation_or_v1(owner, 0, 2),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_animation_or_v1(owner, 1, 1),
                OUT_OF_RANGE
            );
            assert_eq!(state(owner), animated);
            assert_eq!(xy_vsfilter_document_copy_storage_v1(destination, owner), OK);
            assert_eq!(state(destination), animated);
            assert_eq!(
                xy_vsfilter_document_copy_storage_v1(destination, destination),
                OK
            );
            assert_eq!(state(destination), animated);
            assert_eq!(
                xy_vsfilter_document_copy_storage_v1(destination, entry_snapshot),
                BAD_HANDLE
            );
            assert_eq!(state(destination), animated);

            // Refusals are statuses, never a native fallback or partial edit.
            assert_eq!(
                add(owner, 2, &entry(20, 30)),
                (UNRESOLVED_REFERENCE, [0xa5; 8])
            );
            assert_eq!(state(owner), animated);
            assert_eq!(
                xy_vsfilter_document_set_prepared_text_v1(owner, 1, -1, ptr::null(), 0),
                OUT_OF_RANGE
            );
            assert_eq!(
                xy_vsfilter_document_set_prepared_text_v1(owner, 0, 2, ptr::null(), 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_set_prepared_style_v1(owner, 0, encoded.as_ptr(), 1),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_set_entry_time_v1(owner, 0, 2, 1),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_set_entry_time_v1(owner, 1, 0, 1),
                OUT_OF_RANGE
            );
            assert_eq!(
                xy_vsfilter_document_reorder_entries_v1(owner, ptr::null(), 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_reorder_entries_v1(owner, 1u32.to_le_bytes().as_ptr(), 4),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_replace_entry_times_v1(owner, ptr::null(), 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_replace_entry_times_v1(owner, encoded.as_ptr(), 15),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_correct_last_end_v1(owner, -5, sentinel.as_mut_ptr(), 15),
                INVALID_INPUT
            );
            assert_eq!(state(owner), animated);
            assert_eq!(sentinel, [0xa5; 80]);

            // Full replacement survives CString's NUL-terminated comparison.
            let old_style = [65, 0, 0, 0, 66, 0];
            let equivalent_old = [65, 0, 0, 0, 67, 0];
            let replacement = [90, 0, 0, 0, 0, 0xd8, 0xff, 0xff];
            assert_eq!(
                xy_vsfilter_document_set_prepared_style_v1(
                    owner,
                    0,
                    old_style.as_ptr(),
                    old_style.len()
                ),
                OK
            );
            let before_rename = state(owner);
            assert_eq!(
                xy_vsfilter_document_rename_style_cstring_v1(
                    owner,
                    equivalent_old.as_ptr(),
                    equivalent_old.len(),
                    replacement.as_ptr(),
                    replacement.len()
                ),
                OK
            );
            assert_eq!(state(owner).entries()[0].style, [90, 0, 0xd800, 0xffff]);
            assert_eq!(state(owner).segments(), before_rename.segments());
            let renamed = state(owner);
            assert_eq!(
                xy_vsfilter_document_rename_style_cstring_v1(
                    owner,
                    ptr::null(),
                    1,
                    replacement.as_ptr(),
                    replacement.len()
                ),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_rename_style_cstring_v1(
                    owner,
                    replacement.as_ptr(),
                    replacement.len(),
                    ptr::null(),
                    2
                ),
                INVALID_INPUT
            );
            assert_eq!(state(owner), renamed);
            assert_eq!(
                xy_vsfilter_document_rename_style_cstring_v1(
                    owner,
                    replacement.as_ptr(),
                    replacement.len(),
                    replacement.as_ptr(),
                    replacement.len()
                ),
                OK
            );
            assert_eq!(state(owner), renamed); // overlapped inputs, same full value
            assert_eq!(
                xy_vsfilter_document_set_prepared_style_v1(owner, 0, ptr::null(), 0),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_rename_style_cstring_v1(
                    owner,
                    ptr::null(),
                    0,
                    replacement.as_ptr(),
                    replacement.len()
                ),
                OK
            );
            assert_eq!(state(owner).entries()[0].style, [90, 0, 0xd800, 0xffff]);

            assert_eq!(
                xy_vsfilter_document_set_prepared_text_v1(owner, 0, 1, ptr::null(), 0),
                OK
            );
            assert!(state(owner).entries()[0].text.is_empty());
            assert!(state(owner).entries()[0].unicode);
            let style = [0, 0, 0, 0xd8];
            assert_eq!(
                xy_vsfilter_document_set_prepared_style_v1(owner, 0, style.as_ptr(), style.len()),
                OK
            );
            assert_eq!(state(owner).entries()[0].style, [0, 0xd800]);
            assert_eq!(
                xy_vsfilter_document_set_entry_time_v1(owner, 0, 1, i64::MAX),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_set_entry_time_v1(owner, 0, 0, i64::MIN),
                OK
            );
            assert_eq!(state(owner).segments(), animated.segments());
            assert_eq!(state(destination), animated); // independent copy payload
            assert_eq!(read_snapshot(entry_snapshot), entry_bytes);
            assert_eq!(read_snapshot(segment_snapshot), segment_bytes);
            assert_eq!(
                xy_vsfilter_document_reorder_entries_v1(owner, 0u32.to_le_bytes().as_ptr(), 4),
                OK
            );
            assert!(state(owner).segments().is_empty()); // reversed stored interval
            let mut times = vec![];
            times.extend_from_slice(&0i64.to_le_bytes());
            times.extend_from_slice(&10i64.to_le_bytes());
            assert_eq!(
                xy_vsfilter_document_replace_entry_times_v1(owner, times.as_ptr(), times.len()),
                OK
            );
            assert_eq!(state(owner).segments()[0].start, 0);
            assert!(!state(owner).segments()[0].animated);
            assert_eq!(
                xy_vsfilter_document_correct_last_end_v1(owner, 4, sentinel.as_mut_ptr(), 16),
                OK
            );
            assert_eq!(&sentinel[..8], &[1, 0, 0, 0, 0, 0, 0, 0]);
            assert_eq!(i64::from_le_bytes(sentinel[8..16].try_into().unwrap()), 4);
            assert_eq!(state(owner).entries()[0].end, 4);
            assert_eq!(state(owner).segments()[0].end, 10); // correction does not rebuild
            assert_eq!(xy_vsfilter_document_rebuild_segments_v1(owner), OK);
            assert_eq!(state(owner).segments()[0].end, 4);

            assert_eq!(xy_vsfilter_document_remove_entries_v1(destination), OK);
            let stale = state(destination);
            assert!(stale.entries().is_empty());
            assert!(!stale.segments().is_empty());
            assert_eq!(
                add(destination, 0, &encoded),
                (UNRESOLVED_REFERENCE, [0xa5; 8])
            );
            assert_eq!(state(destination), stale);
            assert_eq!(xy_vsfilter_document_clear_storage_v1(destination), OK);
            assert_eq!(
                add(destination, 0, &entry(2, 2)),
                (OK, [0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff])
            );
            assert!(state(destination).entries().is_empty());
            assert_eq!(
                add(destination, 1, &entry(2, 2)),
                (OK, [1, 0, 0, 0, 0, 0, 0, 0])
            );
            assert_eq!(state(destination).entries().len(), 1);
            assert!(state(destination).segments().is_empty());
            assert_eq!(xy_vsfilter_document_clear_storage_v1(destination), OK);
            sentinel.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_correct_last_end_v1(destination, 0, sentinel.as_mut_ptr(), 16),
                OK
            );
            assert_eq!(&sentinel[..16], &[0; 16]);
            assert_eq!(&sentinel[16..], &[0xa5; 64]);
            assert_eq!(
                xy_vsfilter_document_reorder_entries_v1(destination, ptr::null(), 0),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_replace_entry_times_v1(destination, ptr::null(), 0),
                OK
            );

            assert_eq!(xy_vsfilter_document_destroy_v1(owner), OK);
            sentinel.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_counts_v1(owner, sentinel.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(xy_vsfilter_document_destroy_v1(owner), BAD_HANDLE);
            assert_eq!(sentinel, [0xa5; 80]);
            assert_eq!(read_snapshot(entry_snapshot), entry_bytes); // survives owner destruction
            assert_eq!(read_snapshot(segment_snapshot), segment_bytes);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(entry_snapshot), OK);
            assert_eq!(
                xy_vsfilter_document_snapshot_drop_v1(entry_snapshot),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_snapshot_size_v1(entry_snapshot, sentinel.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(sentinel, [0xa5; 80]);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(segment_snapshot), OK);
            assert_eq!(xy_vsfilter_document_destroy_v1(destination), OK);
            let fresh = create();
            assert!(fresh > owner);
            assert!(fresh > entry_snapshot);
            assert_eq!(xy_vsfilter_document_destroy_v1(fresh), OK);
        }
    }
}
