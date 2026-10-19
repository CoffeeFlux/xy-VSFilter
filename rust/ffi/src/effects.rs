// SPDX-License-Identifier: GPL-2.0-or-later
//! Typed effect owners. Detached snapshots cross the transitional renderer ABI;
//! no raw resource pointer is a handle and no loan survives a CRT call.
use crate::document_registry::{Boundary, Error, Kind, Registry, Resource, DOCUMENTS};
use core::ptr;
use xy_vsfilter_compat::effects::{self, Request, Services, State};

fn error(e: Error) -> u32 {
    match e {
        Error::Busy => 2,
        Error::InvalidHandle | Error::TypeMismatch => 1,
        Error::Unresolved => 6,
        Error::Exhausted => 5,
    }
}
fn with<T>(
    boundary: &Boundary,
    call: impl for<'a> FnOnce(&'a mut Registry) -> Result<T, u32>,
) -> Result<T, u32> {
    boundary.try_with(call).map_err(error)?
}
fn create(boundary: &Boundary) -> Result<u64, u32> {
    let result = boundary
        .try_with(|r| {
            r.insert(Resource::Effects {
                revision: 0,
                state: State::default(),
            })
        })
        .map_err(error)?;
    match result {
        Ok(id) => Ok(id),
        Err((e, retired)) => {
            drop(retired);
            Err(error(e))
        }
    }
}
pub(crate) fn snapshot(boundary: &Boundary, id: u64) -> Result<(u64, State), u32> {
    with(boundary, |r| {
        let (rev, state) = r.effects(id).map_err(error)?;
        Ok((*rev, state.clone()))
    })
}
pub(crate) fn commit(boundary: &Boundary, id: u64, expected: u64, state: State) -> Result<(), u32> {
    with(boundary, |r| {
        let (rev, current) = r.effects(id).map_err(error)?;
        if *rev != expected {
            return Err(2);
        }
        let next = rev.checked_add(1).ok_or(5u32)?;
        *current = state;
        *rev = next;
        Ok(())
    })
}
struct Windows;
impl Services for Windows {
    fn space(&mut self, u: u16) -> bool {
        crate::windows_crt::space(u)
    }
    fn compare(&mut self, a: &[u16], b: &[u16]) -> i32 {
        crate::windows_crt::compare(a, b)
    }
    fn scan(&mut self, input: &[u16], count: usize) -> (i32, [i32; 4]) {
        crate::windows_crt::scan_effect(input, count)
    }
    fn integer(&mut self, input: &[u16]) -> i32 {
        crate::windows_crt::number(input, 10).0
    }
    fn float(&mut self, input: &[u16]) -> f64 {
        crate::windows_crt::float(input)
    }
}
type GateCall = unsafe extern "C" fn();
pub(crate) struct Admission {
    pub(crate) enter: Option<GateCall>,
    pub(crate) leave: Option<GateCall>,
}
impl Admission {
    pub(crate) fn call<T>(&self, operation: impl FnOnce() -> T) -> T {
        struct Leave(Option<GateCall>);
        impl Drop for Leave {
            fn drop(&mut self) {
                if let Some(leave) = self.0 {
                    // SAFETY: apply's paired synchronous nonunwinding gate contract.
                    unsafe {
                        leave();
                    }
                }
            }
        }
        if let Some(enter) = self.enter {
            // SAFETY: same gate contract; no resource loan exists at entry.
            unsafe {
                enter();
            }
        }
        let _leave = Leave(self.leave);
        operation()
    }
}
/// # Safety
/// Output is eight writable bytes, possibly unaligned/uninitialized. No retained
/// buffer. Native caller serializes this call through the shared admission gate.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_effects_create_v1(output: *mut u8) -> u32 {
    if output.is_null() {
        return 3;
    }
    let id = match create(&DOCUMENTS) {
        Ok(v) => v,
        Err(s) => return s,
    };
    // SAFETY: eight writable bytes from caller; local source cannot overlap.
    unsafe {
        ptr::copy_nonoverlapping(id.to_le_bytes().as_ptr(), output, 8);
    }
    0
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_effects_destroy_v1(id: u64) -> u32 {
    DOCUMENTS
        .destroy(id, Kind::Effects)
        .err()
        .map(error)
        .unwrap_or(0)
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_effects_clear_v1(id: u64) -> u32 {
    with(&DOCUMENTS, |r| {
        let (rev, state) = r.effects(id).map_err(error)?;
        let next = rev.checked_add(1).ok_or(5u32)?;
        state.clear();
        *rev = next;
        Ok(())
    })
    .err()
    .unwrap_or(0)
}
/// # Safety
/// Output is 56 writable bytes (present,type,8params,4times), may be unaligned
/// and uninitialized. Snapshot bytes are detached before writing output. Caller
/// serializes admission; invalid IDs/slots leave output untouched.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_effects_get_v1(id: u64, slot: u32, output: *mut u8) -> u32 {
    if output.is_null() {
        return 3;
    }
    let Some(kind) = effects::Kind::from_index(slot as usize) else {
        return 4;
    };
    let (_, state) = match snapshot(&DOCUMENTS, id) {
        Ok(v) => v,
        Err(s) => return s,
    };
    let mut words = [0i32; 14];
    if let Some(effect) = state.get(kind) {
        words[0] = 1;
        words[1] = slot as i32;
        words[2..10].copy_from_slice(&effect.param);
        words[10..].copy_from_slice(&effect.time);
    }
    let mut bytes = [0u8; 56];
    for (chunk, word) in bytes.chunks_exact_mut(4).zip(words) {
        chunk.copy_from_slice(&word.to_le_bytes());
    }
    // SAFETY: detached local source, preflighted caller output; no output read.
    unsafe {
        ptr::copy_nonoverlapping(bytes.as_ptr(), output, 56);
    }
    0
}
/// # Safety
/// Input is length readable bytes; output is capacity writable bytes and may
/// overlap input or be uninitialized. Counts must fit isize. Input is owned
/// before any callback. Gate callbacks are either both absent or a valid paired,
/// same-thread, nonunwinding gate used only around registry access. They are not
/// retained. Neither gate nor registry loan is held during Windows CRT work.
/// Reentrant owner mutation refuses commit instead of overwriting newer state.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_effects_apply_v1(
    id: u64,
    input: *const u8,
    length: usize,
    output: *mut u8,
    capacity: usize,
    enter: Option<GateCall>,
    leave: Option<GateCall>,
) -> u32 {
    if length > isize::MAX as usize || capacity > isize::MAX as usize {
        return 5;
    }
    if input.is_null()
        || output.is_null()
        || length < 36
        || capacity < 12
        || enter.is_some() != leave.is_some()
    {
        return 3;
    }
    // SAFETY: preflighted readable input, copied before callbacks/admission.
    let input = unsafe { core::slice::from_raw_parts(input, length) }.to_vec();
    let mut request = match Request::decode(&input) {
        Ok(v) => v,
        Err(_) => return 3,
    };
    let gate = Admission { enter, leave };
    let (revision, mut state) = match gate.call(|| snapshot(&DOCUMENTS, id)) {
        Ok(v) => v,
        Err(s) => return s,
    };
    state.apply(
        request.command,
        &request.params,
        &mut request.context,
        &mut Windows,
    );
    if let Err(s) = gate.call(|| commit(&DOCUMENTS, id, revision, state)) {
        return s;
    }
    let mut bytes = [0u8; 12];
    for (chunk, value) in bytes.chunks_exact_mut(4).zip([
        request.context.wrap,
        request.context.hard,
        i32::from(request.context.animated),
    ]) {
        chunk.copy_from_slice(&value.to_le_bytes());
    }
    // SAFETY: detached result, capacity checked before any services or mutation.
    unsafe {
        ptr::copy_nonoverlapping(bytes.as_ptr(), output, 12);
    }
    0
}

#[cfg(test)]
pub(crate) fn test_exports(document_id: u64) {
    use alloc::vec::Vec;
    let mut id_bytes = [0u8; 8];
    let mut output = [0xa5; 56];
    let mut packet = Vec::new();
    packet.extend_from_slice(&0u32.to_le_bytes());
    packet.extend_from_slice(&1f64.to_bits().to_le_bytes());
    packet.extend_from_slice(&1f64.to_bits().to_le_bytes());
    for word in [3u32, u32::MAX, 0, 1] {
        packet.extend_from_slice(&word.to_le_bytes());
    }
    let text: Vec<u16> = "Banner;4;1;32".encode_utf16().collect();
    packet.extend_from_slice(&(text.len() as u32).to_le_bytes());
    for u in text {
        packet.extend_from_slice(&u.to_le_bytes());
    }
    // SAFETY: all buffers span their advertised lengths; overlap is intentional
    // and permitted. Global-export tests run inside document's single test.
    unsafe {
        assert_eq!(xy_vsfilter_effects_create_v1(id_bytes.as_mut_ptr()), 0);
        let id = u64::from_le_bytes(id_bytes);
        assert_eq!(
            xy_vsfilter_effects_get_v1(document_id, 3, output.as_mut_ptr()),
            1
        );
        assert_eq!(output, [0xa5; 56]);
        assert_eq!(
            xy_vsfilter_effects_apply_v1(
                id,
                packet.as_ptr(),
                packet.len(),
                output.as_mut_ptr(),
                11,
                None,
                None
            ),
            3
        );
        assert_eq!(output, [0xa5; 56]);
        assert_eq!(
            xy_vsfilter_effects_apply_v1(
                id,
                packet.as_ptr(),
                packet.len(),
                packet.as_mut_ptr(),
                packet.len(),
                None,
                None
            ),
            0
        );
        assert_eq!(&packet[..12], &[2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
        assert_eq!(xy_vsfilter_effects_get_v1(id, 3, output.as_mut_ptr()), 0);
        assert_eq!(i32::from_le_bytes(output[8..12].try_into().unwrap()), 4);
        assert_eq!(i32::from_le_bytes(output[16..20].try_into().unwrap()), 32);
        let detached = output;
        assert_eq!(xy_vsfilter_effects_clear_v1(id), 0);
        assert_eq!(xy_vsfilter_effects_get_v1(id, 3, output.as_mut_ptr()), 0);
        assert_eq!(output, [0; 56]);
        assert_eq!(detached[0], 1);
        assert_eq!(xy_vsfilter_effects_destroy_v1(id), 0);
        assert_eq!(xy_vsfilter_effects_destroy_v1(id), 1);
        output.fill(0xa5);
        assert_eq!(xy_vsfilter_effects_get_v1(id, 3, output.as_mut_ptr()), 1);
        assert_eq!(output, [0xa5; 56]);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn stale_wrong_type_and_reentrant_updates_refuse_commit() {
        let boundary = Boundary::new();
        let id = create(&boundary).unwrap();
        let (revision, state) = snapshot(&boundary, id).unwrap();
        commit(&boundary, id, revision, state.clone()).unwrap();
        assert_eq!(commit(&boundary, id, revision, state), Err(2));
        assert_eq!(
            boundary.destroy(id, Kind::Snapshot),
            Err(Error::TypeMismatch)
        );
        boundary.destroy(id, Kind::Effects).unwrap();
        assert_eq!(snapshot(&boundary, id).unwrap_err(), 1);
        let next = create(&boundary).unwrap();
        assert!(next > id);
        assert!(boundary
            .try_with(|_| snapshot(&boundary, next))
            .unwrap()
            .is_err());
    }
    #[test]
    fn invalid_buffers_are_rejected_before_pointer_reads() {
        let mut output = [0xa5; 56];
        // SAFETY: invalid metadata refuses all reads; output is live and writable.
        unsafe {
            assert_eq!(xy_vsfilter_effects_create_v1(ptr::null_mut()), 3);
            assert_eq!(
                xy_vsfilter_effects_get_v1(0, u32::MAX, output.as_mut_ptr()),
                4
            );
            assert_eq!(
                xy_vsfilter_effects_apply_v1(
                    0,
                    ptr::null(),
                    usize::MAX,
                    output.as_mut_ptr(),
                    56,
                    None,
                    None
                ),
                5
            );
            assert_eq!(
                xy_vsfilter_effects_apply_v1(
                    0,
                    ptr::null(),
                    36,
                    output.as_mut_ptr(),
                    56,
                    None,
                    None
                ),
                3
            );
        }
        assert_eq!(output, [0xa5; 56]);
    }
}
