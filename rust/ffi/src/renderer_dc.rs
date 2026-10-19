// SPDX-License-Identifier: GPL-2.0-or-later
//! Shared Windows renderer DC ownership. This admission is separate from
//! DOCUMENTS: an active text/path operation can create/read/release fonts there.
//! A checked operation token pins the context and excludes other operations or
//! owner changes. The transitional caller serializes complete operations, keeps
//! the creating thread alive through final release, and ends a token only after
//! all uses of its Windows handle have finished. No Rust pointer crosses FFI.
use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicBool, Ordering};
use xy_vsfilter_compat::renderer_dc::{Error, State};

#[link(name = "gdi32")]
extern "system" {
    fn CreateCompatibleDC(dc: usize) -> usize;
    fn SetBkMode(dc: usize, mode: i32) -> i32;
    fn SetTextColor(dc: usize, color: u32) -> u32;
    fn SetMapMode(dc: usize, mode: i32) -> i32;
    fn DeleteDC(dc: usize) -> i32;
}
struct Context(usize);
impl Context {
    fn new() -> Self {
        // SAFETY: null requests a compatible memory DC. All setup arguments are
        // the cleanup's constants and apply to that newly owned DC, including
        // its null failure value. GDI accepts null handles and reports failure;
        // do not skip calls or erase their last-error/errno/floating effects.
        unsafe {
            let dc = CreateCompatibleDC(0);
            SetBkMode(dc, 1);
            SetTextColor(dc, 0x00ff_ffff);
            SetMapMode(dc, 1);
            Self(dc)
        }
    }
}
impl Drop for Context {
    fn drop(&mut self) {
        // SAFETY: unique native owner, with no outstanding operation token.
        // Exactly one deletion attempt at final release, even for null. Native
        // ignores failure; never retry or delete a subsequently reused handle.
        unsafe { DeleteDC(self.0) };
    }
}

struct Boundary<T> {
    busy: AtomicBool,
    state: UnsafeCell<State<T>>,
}
// SAFETY: every state access is under the acquire/release atomic gate. Its
// higher-ranked closure cannot return references borrowing state. T: Send is
// required for ownership transfer; native thread/handle-use contracts are
// additionally enforced by the private adapter, not inferred from this bound.
unsafe impl<T: Send> Sync for Boundary<T> {}
struct Guard<'a, T>(&'a Boundary<T>);
impl<T> Drop for Guard<'_, T> {
    fn drop(&mut self) {
        self.0.busy.store(false, Ordering::Release);
    }
}
impl<T> Boundary<T> {
    const fn new() -> Self {
        Self {
            busy: AtomicBool::new(false),
            state: UnsafeCell::new(State::new()),
        }
    }
    fn with<R>(
        &self,
        operation: impl for<'a> FnOnce(&'a mut State<T>) -> Result<R, Error>,
    ) -> Result<R, Error> {
        self.busy
            .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
            .map_err(|_| Error::Busy)?;
        let guard = Guard(self);
        // SAFETY: the unique guard owns admission and no state reference can
        // escape operation. Native reentry observes Busy instead of deadlocking.
        let result = operation(unsafe { &mut *self.state.get() });
        drop(guard);
        result
    }
}
static CONTEXTS: Boundary<Context> = Boundary::new();
fn error(error: Error) -> u32 {
    match error {
        Error::InvalidHandle => 1,
        Error::Busy => 2,
        Error::Exhausted => 5,
    }
}
/// # Safety
/// output spans eight writable bytes, with any alignment. The creating thread
/// must remain alive until the final renderer owner is released. Complete calls
/// are serialized by the adapter; no caller may delete the borrowed native DC.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_renderer_dc_acquire_v1(output: *mut u8) -> u32 {
    if output.is_null() {
        return 3;
    }
    let id = match CONTEXTS.with(|state| state.acquire(Context::new)) {
        Ok(id) => id,
        Err(e) => return error(e),
    };
    // SAFETY: detached initialized ID and caller's eight writable bytes.
    unsafe { core::ptr::copy_nonoverlapping(id.to_le_bytes().as_ptr(), output, 8) };
    0
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_renderer_dc_release_v1(id: u64) -> u32 {
    CONTEXTS
        .with(|state| {
            // Destruction stays inside admission: recreation cannot overtake
            // DeleteDC, and reentry during deletion observes Busy.
            drop(state.release(id)?);
            Ok(())
        })
        .err()
        .map(error)
        .unwrap_or(0)
}
/// # Safety
/// output spans sixteen writable bytes: token u64 LE, native HDC u64 LE.
/// The handle is borrowed only until matching end(token). Do not delete it,
/// transfer the token, retain a usable alias, or issue GDI calls after end.
/// Keep the creating thread alive. Begin excludes owner changes and overlapping
/// operations; font calls may independently enter DOCUMENTS while this token is
/// active. No Rust memory is borrowed by this packet.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_renderer_dc_begin_v1(output: *mut u8) -> u32 {
    if output.is_null() {
        return 3;
    }
    let (token, handle) = match CONTEXTS.with(|state| {
        let (token, value) = state.begin()?;
        Ok((token, value.0 as u64))
    }) {
        Ok(pair) => pair,
        Err(e) => return error(e),
    };
    let mut bytes = [0; 16];
    bytes[..8].copy_from_slice(&token.to_le_bytes());
    bytes[8..].copy_from_slice(&handle.to_le_bytes());
    // SAFETY: detached initialized packet and caller's sixteen writable bytes.
    unsafe { core::ptr::copy_nonoverlapping(bytes.as_ptr(), output, bytes.len()) };
    0
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_renderer_dc_end_v1(token: u64) -> u32 {
    CONTEXTS
        .with(|state| state.end(token))
        .err()
        .map(error)
        .unwrap_or(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::sync::Arc;
    use std::sync::Barrier;

    #[test]
    fn admission_reentry_and_concurrent_access_refuse_then_recover() {
        let boundary = Arc::new(Boundary::<usize>::new());
        let entered = Arc::new(Barrier::new(2));
        let finish = Arc::new(Barrier::new(2));
        let (b, e, f) = (boundary.clone(), entered.clone(), finish.clone());
        let thread = std::thread::spawn(move || {
            b.with(|state| {
                assert_eq!(b.with(|_| Ok(())), Err(Error::Busy));
                e.wait();
                f.wait();
                state.acquire(|| 5)
            })
            .unwrap()
        });
        entered.wait();
        assert_eq!(boundary.with(|_| Ok(())), Err(Error::Busy));
        finish.wait();
        let id = thread.join().unwrap();
        assert_eq!(boundary.with(|s| s.release(id)), Ok(Some(5)));
    }

    #[test]
    fn real_exports_reject_invalid_buffers_handles_and_active_operation_release() {
        let mut owner = [0xa5; 9];
        let mut packet = [0xa5; 17];
        // SAFETY: exact writable spans, including unaligned outputs. Deliberate
        // null arguments are checked before access; the real DC is never used
        // outside its token and the creating test thread outlives its owners.
        unsafe {
            assert_eq!(xy_vsfilter_renderer_dc_acquire_v1(core::ptr::null_mut()), 3);
            assert_eq!(xy_vsfilter_renderer_dc_begin_v1(core::ptr::null_mut()), 3);
            assert_eq!(xy_vsfilter_renderer_dc_release_v1(0), 1);
            assert_eq!(xy_vsfilter_renderer_dc_end_v1(0), 1);
            assert_eq!(
                xy_vsfilter_renderer_dc_begin_v1(packet[1..].as_mut_ptr()),
                1
            );
            assert_eq!(packet, [0xa5; 17]);
            assert_eq!(
                xy_vsfilter_renderer_dc_acquire_v1(owner[1..].as_mut_ptr()),
                0
            );
            let id = u64::from_le_bytes(owner[1..].try_into().unwrap());
            assert_eq!(
                xy_vsfilter_renderer_dc_begin_v1(packet[1..].as_mut_ptr()),
                0
            );
            let token = u64::from_le_bytes(packet[1..9].try_into().unwrap());
            assert_ne!(u64::from_le_bytes(packet[9..].try_into().unwrap()), 0);
            let original = packet;
            assert_eq!(
                xy_vsfilter_renderer_dc_begin_v1(packet[1..].as_mut_ptr()),
                2
            );
            assert_eq!(packet, original);
            assert_eq!(xy_vsfilter_renderer_dc_release_v1(id), 2);
            assert_eq!(xy_vsfilter_renderer_dc_end_v1(id), 1);
            assert_eq!(xy_vsfilter_renderer_dc_end_v1(token), 0);
            assert_eq!(xy_vsfilter_renderer_dc_release_v1(token), 1);
            assert_eq!(xy_vsfilter_renderer_dc_release_v1(id), 0);
            assert_eq!(xy_vsfilter_renderer_dc_release_v1(id), 1);
        }
        assert_eq!(owner[0], 0xa5);
        assert_eq!(packet[0], 0xa5);
    }
}
