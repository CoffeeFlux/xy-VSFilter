// SPDX-License-Identifier: GPL-2.0-or-later
//! Checked ASS tree IDs and detached parameter packets. Native shared owners
//! retain IDs, never pointers into Rust; canonical nodes and strings live here.
use crate::document_registry::{Boundary, Error, Kind, Registry, Resource, DOCUMENTS};
use alloc::vec::Vec;
use core::ptr;
use xy_vsfilter_compat::ass_tree::{self, Tree};
fn error(e: Error) -> u32 {
    match e {
        Error::Busy => 2,
        Error::InvalidHandle | Error::TypeMismatch => 1,
        Error::Unresolved => 6,
        Error::Exhausted => 5,
    }
}
fn tree_error(e: ass_tree::Error) -> u32 {
    match e {
        ass_tree::Error::Shape => 3,
        ass_tree::Error::Index => 4,
        ass_tree::Error::Overflow => 5,
    }
}
fn with<T>(
    boundary: &Boundary,
    call: impl for<'a> FnOnce(&'a mut Registry) -> Result<T, u32>,
) -> Result<T, u32> {
    boundary.try_with(call).map_err(error)?
}
fn create(boundary: &Boundary) -> Result<u64, u32> {
    match boundary
        .try_with(|r| r.insert(Resource::AssTree(Tree::default())))
        .map_err(error)?
    {
        Ok(id) => Ok(id),
        Err((e, retired)) => {
            drop(retired);
            Err(error(e))
        }
    }
}
fn append(boundary: &Boundary, id: u64, list: u32, tree: Tree) -> Result<(), u32> {
    with(boundary, |r| {
        r.ass_tree(id)
            .map_err(error)?
            .append(list, tree)
            .map_err(tree_error)
    })
}
fn query(boundary: &Boundary, id: u64, list: u32, index: u32, field: u32) -> Result<Vec<u8>, u32> {
    with(boundary, |r| {
        let tree = r.ass_tree(id).map_err(error)?;
        match field {
            0 => Ok(u64::from(tree.count(list).map_err(tree_error)?)
                .to_le_bytes()
                .to_vec()),
            1 => Ok((tree.packet(list, index).map_err(tree_error)?.len() as u64)
                .to_le_bytes()
                .to_vec()),
            2 => tree.packet(list, index).map_err(tree_error),
            _ => Err(3),
        }
    })
}
type GateCall = unsafe extern "C" fn();
struct Leave(Option<GateCall>);
impl Drop for Leave {
    fn drop(&mut self) {
        if let Some(leave) = self.0 {
            // SAFETY: append's paired synchronous nonunwinding gate contract.
            unsafe {
                leave();
            }
        }
    }
}
/// # Safety
/// Output is eight writable bytes, possibly unaligned/uninitialized. Caller
/// serializes admission through the shared native gate. No buffer is retained.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_tree_create_v1(output: *mut u8) -> u32 {
    if output.is_null() {
        return 3;
    }
    let id = match create(&DOCUMENTS) {
        Ok(v) => v,
        Err(s) => return s,
    };
    // SAFETY: live caller output and distinct initialized local ID bytes.
    unsafe {
        ptr::copy_nonoverlapping(id.to_le_bytes().as_ptr(), output, 8);
    }
    0
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_ass_tree_destroy_v1(id: u64) -> u32 {
    DOCUMENTS
        .destroy(id, Kind::AssTree)
        .err()
        .map(error)
        .unwrap_or(0)
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_ass_tree_position_v1(command: u32) -> i32 {
    ass_tree::position_level(command)
}
/// # Safety
/// Input spans length initialized bytes, UTF16LE without a terminator. For
/// length0 null is allowed. Input is fully copied before CRT work or callbacks.
/// Enter/leave are both absent or a valid paired synchronous, same-thread,
/// nonunwinding admission gate; neither callback is retained. CRT whitespace
/// classification runs without gate or registry loan. ID/list are checked after
/// parsing; a destroyed owner is refused, never dereferenced.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_tree_append_v1(
    id: u64,
    list: u32,
    input: *const u8,
    length: usize,
    enter: Option<GateCall>,
    leave: Option<GateCall>,
) -> u32 {
    if length > isize::MAX as usize || length / 2 > i32::MAX as usize {
        return 5;
    }
    if length % 2 != 0 || (length != 0 && input.is_null()) || enter.is_some() != leave.is_some() {
        return 3;
    }
    let bytes = if length == 0 {
        Vec::new()
    } else {
        // SAFETY: preflighted readable caller range; borrow ends before services.
        unsafe { core::slice::from_raw_parts(input, length) }.to_vec()
    };
    let units: Vec<u16> = bytes
        .chunks_exact(2)
        .map(|b| u16::from_le_bytes([b[0], b[1]]))
        .collect();
    let white: Vec<u8> = units
        .iter()
        .map(|u| u8::from(crate::windows_crt::space(*u)))
        .collect();
    let tree = match ass_tree::parse(&units, &white) {
        Ok(v) => v,
        Err(e) => return tree_error(e),
    };
    if let Some(enter) = enter {
        // SAFETY: paired callback contract; no resource loan exists yet.
        unsafe {
            enter();
        }
    }
    let _leave = Leave(leave);
    append(&DOCUMENTS, id, list, tree).err().unwrap_or(0)
}
/// # Safety
/// Output is capacity writable bytes, may be unaligned/uninitialized. Field0
/// returns list count as u64, field1 packet length as u64, field2 node packet.
/// Invalid metadata/ID/list/index or short output leave output untouched. Native
/// caller serializes this admission. Packet is detached before the output write.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_tree_query_v1(
    id: u64,
    list: u32,
    index: u32,
    field: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    if capacity > isize::MAX as usize {
        return 5;
    }
    if output.is_null() || capacity < 8 || field > 2 {
        return 3;
    }
    let bytes = match query(&DOCUMENTS, id, list, index, field) {
        Ok(v) => v,
        Err(e) => return e,
    };
    if capacity < bytes.len() {
        return 4;
    }
    // SAFETY: detached source, preflighted caller capacity, no output read.
    unsafe {
        ptr::copy_nonoverlapping(bytes.as_ptr(), output, bytes.len());
    }
    0
}

#[cfg(test)]
pub(crate) fn test_exports(document: u64) {
    let mut idbytes = [0; 8];
    let mut output = [0xa5; 64];
    let input: Vec<u8> = "\\t(\\b1)"
        .encode_utf16()
        .flat_map(u16::to_le_bytes)
        .collect();
    // SAFETY: live exact-sized input/output spans, explicit invalid metadata is
    // rejected before access. Called from the single serialized global test.
    unsafe {
        assert_eq!(xy_vsfilter_ass_tree_create_v1(idbytes.as_mut_ptr()), 0);
        let id = u64::from_le_bytes(idbytes);
        assert_eq!(
            xy_vsfilter_ass_tree_query_v1(document, 0, 0, 0, output.as_mut_ptr(), 64),
            1
        );
        assert_eq!(output, [0xa5; 64]);
        assert_eq!(
            xy_vsfilter_ass_tree_append_v1(id, 0, input.as_ptr(), input.len(), None, None),
            0
        );
        assert_eq!(
            xy_vsfilter_ass_tree_query_v1(id, 0, 0, 2, output.as_mut_ptr(), 8),
            4
        );
        assert_eq!(output, [0xa5; 64]);
        assert_eq!(
            xy_vsfilter_ass_tree_query_v1(id, 0, 0, 2, output.as_mut_ptr(), 64),
            0
        );
        let child = u32::from_le_bytes(output[4..8].try_into().unwrap());
        let detached = output;
        assert_eq!(
            xy_vsfilter_ass_tree_append_v1(id, child, input.as_ptr(), input.len(), None, None),
            0
        );
        assert_eq!(
            xy_vsfilter_ass_tree_query_v1(id, child, 0, 0, output.as_mut_ptr(), 64),
            0
        );
        assert_eq!(u64::from_le_bytes(output[..8].try_into().unwrap()), 2);
        assert_eq!(xy_vsfilter_ass_tree_destroy_v1(id), 0);
        assert_eq!(xy_vsfilter_ass_tree_destroy_v1(id), 1);
        assert_eq!(detached[0], 48);
        output.fill(0xa5);
        assert_eq!(
            xy_vsfilter_ass_tree_query_v1(id, 0, 0, 0, output.as_mut_ptr(), 64),
            1
        );
        assert_eq!(output, [0xa5; 64]);
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn typed_ids_detached_packets_and_destruction() {
        let boundary = Boundary::new();
        let id = create(&boundary).unwrap();
        let tree = ass_tree::parse(&[92, 98, 49], &[0; 3]).unwrap();
        append(&boundary, id, 0, tree).unwrap();
        let packet = query(&boundary, id, 0, 0, 2).unwrap();
        assert_eq!(packet[0], 14);
        assert_eq!(query(&boundary, id, 99, 0, 0), Err(4));
        assert_eq!(query(&boundary, id, 0, 99, 2), Err(4));
        assert_eq!(
            boundary.destroy(id, Kind::Effects),
            Err(Error::TypeMismatch)
        );
        assert_eq!(
            boundary
                .try_with(|_| query(&boundary, id, 0, 0, 0))
                .unwrap(),
            Err(2)
        );
        boundary.destroy(id, Kind::AssTree).unwrap();
        assert_eq!(query(&boundary, id, 0, 0, 0), Err(1));
        assert_eq!(packet[0], 14);
        assert!(create(&boundary).unwrap() > id);
    }
    #[test]
    fn invalid_buffer_shapes_refuse_before_admission() {
        let mut out = [0xa5; 8];
        // SAFETY: malformed metadata prevents reads and callbacks; output is live.
        unsafe {
            assert_eq!(
                xy_vsfilter_ass_tree_append_v1(0, 0, ptr::null(), usize::MAX, None, None),
                5
            );
            assert_eq!(
                xy_vsfilter_ass_tree_append_v1(0, 0, ptr::null(), 1, None, None),
                3
            );
            assert_eq!(
                xy_vsfilter_ass_tree_query_v1(0, 0, 0, 3, out.as_mut_ptr(), 8),
                3
            );
            assert_eq!(xy_vsfilter_ass_tree_create_v1(ptr::null_mut()), 3);
        }
        assert_eq!(out, [0xa5; 8]);
    }
}
