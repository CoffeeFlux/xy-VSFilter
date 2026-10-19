// SPDX-License-Identifier: GPL-2.0-or-later
//! Checked persistent paths. No owner pointer or buffer loan crosses the ABI.
//! A separate admission excludes mutation/release during GDI calls; reentry
//! refuses Busy. Caller serializes full native operations and exclusively owns
//! the borrowed HDC, with its creating thread alive. No native unwind is allowed.
use core::{
    cell::UnsafeCell,
    sync::atomic::{AtomicBool, Ordering},
};
use xy_vsfilter_compat::path_align::Point;
use xy_vsfilter_compat::path_data::{Path, Store};
struct Boundary {
    busy: AtomicBool,
    state: UnsafeCell<Store>,
}
// SAFETY: all access uses the atomic gate; HRTB closures cannot return borrowed
// references, and Store owns only Send data. No native pointer is stored.
unsafe impl Sync for Boundary {}
struct Guard<'a>(&'a Boundary);
impl Drop for Guard<'_> {
    fn drop(&mut self) {
        self.0.busy.store(false, Ordering::Release);
    }
}
impl Boundary {
    const fn new() -> Self {
        Self {
            busy: AtomicBool::new(false),
            state: UnsafeCell::new(Store::new()),
        }
    }
    fn with<T>(&self, f: impl for<'a> FnOnce(&'a mut Store) -> Result<T, u32>) -> Result<T, u32> {
        self.busy
            .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
            .map_err(|_| 2u32)?;
        let guard = Guard(self);
        // SAFETY: unique admission, no state reference escapes the closure.
        let result = f(unsafe { &mut *self.state.get() });
        drop(guard);
        result
    }
}
static PATHS: Boundary = Boundary::new();
/// # Safety
/// `input` spans exactly `length` initialized bytes: nine raw double words,
/// signed origin x/y, and a 0/1 rounding flag. No foreign pointer is retained.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_path_transform_v1(
    id: u64,
    input: *const u8,
    length: usize,
) -> u32 {
    if input.is_null() || length != 81 {
        return 3;
    }
    // SAFETY: caller's readable span; integer-only decoding preserves NaN bits.
    let bytes = unsafe { core::slice::from_raw_parts(input, length) };
    if bytes[80] > 1 {
        return 3;
    }
    let mut values = [0; 9];
    for (value, chunk) in values.iter_mut().zip(bytes[..72].chunks_exact(8)) {
        *value = u64::from_le_bytes(chunk.try_into().unwrap());
    }
    let parameters = xy_vsfilter_compat::word_transform::Parameters {
        values,
        origin: Point {
            x: i32::from_le_bytes(bytes[72..76].try_into().unwrap()),
            y: i32::from_le_bytes(bytes[76..80].try_into().unwrap()),
        },
        round: bytes[80] != 0,
    };
    match PATHS.with(|store| {
        let path = store.get_mut(id)?;
        // Prepare even for an empty path: native trig/arithmetic has observable
        // errno and floating exception effects before it checks the point count.
        let mut numeric = crate::windows_word_transform::Numeric::new(&parameters);
        xy_vsfilter_compat::word_transform::transform(
            path.buffers().0,
            parameters.round,
            &mut numeric,
        );
        Ok(())
    }) {
        Ok(()) => 0,
        Err(status) => status,
    }
}
#[link(name = "gdi32")]
extern "system" {
    fn BeginPath(dc: usize) -> i32;
    fn CloseFigure(dc: usize) -> i32;
    fn EndPath(dc: usize) -> i32;
    fn AbortPath(dc: usize) -> i32;
    fn GetPath(dc: usize, points: *mut Point, types: *mut u8, count: i32) -> i32;
    fn TextOutW(dc: usize, x: i32, y: i32, text: *const u16, count: i32) -> i32;
}
struct TextBackend<'a> {
    dc: usize,
    path: &'a mut Path,
}
impl xy_vsfilter_compat::text_path::Backend for TextBackend<'_> {
    fn begin(&mut self, clear: bool) -> Result<bool, u32> {
        if clear {
            self.path.clear();
        }
        // SAFETY: exclusive live HDC retained by the enclosing text operation.
        Ok(unsafe { BeginPath(self.dc) } != 0)
    }
    fn text(&mut self, units: &[u16]) -> bool {
        // SAFETY: bounded initialized units in the retained terminated packet;
        // admitted count fits i32. The HDC is exclusive for the complete call.
        unsafe { TextOutW(self.dc, 0, 0, units.as_ptr(), units.len() as i32) != 0 }
    }
    fn end(&mut self, partial: bool, x: i32) -> Result<bool, u32> {
        end(self.path, self.dc, partial, x, 0)
    }
}
/// # Safety
/// input spans length initialized bytes: font u64, spacing raw u64, unit count
/// u32 then UTF16 units. output spans four writable bytes and may overlap input.
/// The HDC is exclusive under its renderer operation guard; HFONT stays alive.
/// Native callbacks must not unwind through Rust. No foreign pointer is retained.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_path_text_v1(
    id: u64,
    dc: usize,
    input: *const u8,
    length: usize,
    output: *mut u8,
) -> u32 {
    if input.is_null() || output.is_null() || length < 20 || length > isize::MAX as usize {
        return 3;
    }
    // SAFETY: caller's initialized readable span, copied before output writes.
    let bytes = unsafe { core::slice::from_raw_parts(input, length) };
    let font = u64::from_le_bytes(bytes[..8].try_into().unwrap());
    let spacing = u64::from_le_bytes(bytes[8..16].try_into().unwrap());
    let count = u32::from_le_bytes(bytes[16..20].try_into().unwrap()) as usize;
    if font > usize::MAX as u64
        || count > i32::MAX as usize
        || count.checked_mul(2).and_then(|n| n.checked_add(20)) != Some(length)
    {
        return 3;
    }
    let mut units = alloc::vec::Vec::new();
    if units.try_reserve_exact(count + 1).is_err() {
        return 5;
    }
    units.extend(
        bytes[20..]
            .chunks_exact(2)
            .map(|v| u16::from_le_bytes([v[0], v[1]])),
    );
    units.push(0);
    let result = match PATHS.with(|s| {
        xy_vsfilter_compat::text_path::create(
            &units[..count],
            spacing,
            &mut crate::text_measure::Windows {
                dc,
                font: font as usize,
            },
            &mut TextBackend {
                dc,
                path: s.get_mut(id)?,
            },
        )
    }) {
        Ok(v) => u32::from(v),
        Err(e) => return e,
    };
    // SAFETY: detached scalar and caller's writable four-byte span.
    unsafe {
        core::ptr::copy_nonoverlapping(result.to_le_bytes().as_ptr(), output, 4);
    }
    0
}
fn end(path: &mut Path, dc: usize, partial: bool, dx: i32, dy: i32) -> Result<bool, u32> {
    // SAFETY: export's exclusive live HDC contract; scalar-only GDI calls.
    unsafe {
        CloseFigure(dc);
    }
    // SAFETY: same borrowed HDC, whose path state is exclusively used here.
    if unsafe { EndPath(dc) } != 0 {
        // SAFETY: null buffers with zero capacity request only the count.
        let count = unsafe { GetPath(dc, core::ptr::null_mut(), core::ptr::null_mut(), 0) };
        if count < 0 {
            return Err(6);
        }
        if count == 0 {
            if !partial {
                path.clear();
            }
            return Ok(true);
        }
        let mut next = Path::allocate(count)?;
        let (points, types) = next.buffers();
        // SAFETY: two disjoint initialized exact-count buffers, Point matches
        // Windows POINT. GDI's synchronous output borrow ends on return.
        let read = unsafe { GetPath(dc, points.as_mut_ptr(), types.as_mut_ptr(), count) };
        if read != count {
            // SAFETY: same exclusive HDC. A short read leaves native storage
            // partly uninitialized (partial mode also DebugBreaks): unresolved,
            // never claim initialized zero filler is a compatible native value.
            unsafe {
                AbortPath(dc);
            }
            return Err(6);
        }
        if partial {
            path.append(&next, dx, dy)?;
        } else {
            *path = next;
        }
        return Ok(true);
    }
    // SAFETY: native failure sequence aborts this exclusive HDC path.
    unsafe {
        AbortPath(dc);
    }
    Ok(false)
}
/// # Safety
/// output spans eight writable bytes. source=0 creates empty; otherwise it is
/// a checked source ID. No native pointer is retained; unaligned output is OK.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_path_create_v1(source: u64, output: *mut u8) -> u32 {
    if output.is_null() {
        return 3;
    }
    let id = match PATHS.with(|s| {
        let p = if source == 0 {
            Path::default()
        } else {
            s.get(source)?.clone()
        };
        s.create(p)
    }) {
        Ok(id) => id,
        Err(e) => return e,
    };
    // SAFETY: detached ID and caller's writable eight-byte span.
    unsafe {
        core::ptr::copy_nonoverlapping(id.to_le_bytes().as_ptr(), output, 8);
    }
    0
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_path_destroy_v1(id: u64) -> u32 {
    PATHS.with(|s| s.remove(id)).err().unwrap_or(0)
}
/// # Safety
/// output spans twelve writable bytes: point count u64 and hash u32, LE.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_path_info_v1(id: u64, output: *mut u8) -> u32 {
    if output.is_null() {
        return 3;
    }
    let (n, h) = match PATHS.with(|s| {
        let p = s.get(id)?;
        Ok((p.points().len() as u64, p.hash()))
    }) {
        Ok(v) => v,
        Err(e) => return e,
    };
    let mut bytes = [0; 12];
    bytes[..8].copy_from_slice(&n.to_le_bytes());
    bytes[8..].copy_from_slice(&h.to_le_bytes());
    // SAFETY: detached initialized packet and caller's writable span.
    unsafe {
        core::ptr::copy_nonoverlapping(bytes.as_ptr(), output, 12);
    }
    0
}
/// # Safety
/// output spans capacity writable bytes. On insufficient capacity, no write.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_path_read_v1(
    id: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    if output.is_null() || capacity > isize::MAX as usize {
        return 3;
    }
    let bytes = match PATHS.with(|s| s.get(id)?.encode()) {
        Ok(b) => b,
        Err(e) => return e,
    };
    if bytes.len() > capacity {
        return 4;
    }
    // SAFETY: detached owned packet is disjoint from caller output.
    unsafe {
        core::ptr::copy_nonoverlapping(bytes.as_ptr(), output, bytes.len());
    }
    0
}
/// # Safety
/// input spans length initialized bytes; copied before admission. Packet is a
/// u32 count then count (x:i32,y:i32,type:u8) records, all little endian.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_path_write_v1(
    id: u64,
    input: *const u8,
    length: usize,
) -> u32 {
    if input.is_null() || length > isize::MAX as usize {
        return 3;
    }
    // SAFETY: caller's initialized readable span; decoding takes owned copies.
    let p = match Path::decode(unsafe { core::slice::from_raw_parts(input, length) }) {
        Ok(p) => p,
        Err(e) => return e,
    };
    PATHS
        .with(|s| {
            *s.get_mut(id)? = p;
            Ok(())
        })
        .err()
        .unwrap_or(0)
}
/// # Safety
/// output spans four writable bytes. assign=0 compares; assign=1 performs native
/// assignment (including empty-source no-op). Self assignment does not copy.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_path_pair_v1(
    id: u64,
    source: u64,
    assign: u32,
    output: *mut u8,
) -> u32 {
    if output.is_null() || assign > 1 {
        return 3;
    }
    let result = match PATHS.with(|s| {
        if assign == 0 {
            return Ok(u32::from(s.get(id)? == s.get(source)?));
        }
        if id == source {
            s.get(id)?;
        } else {
            let p = s.get(source)?.clone();
            s.get_mut(id)?.assign(&p);
        }
        Ok(1u32)
    }) {
        Ok(v) => v,
        Err(e) => return e,
    };
    // SAFETY: detached scalar and caller's writable four-byte output.
    unsafe {
        core::ptr::copy_nonoverlapping(result.to_le_bytes().as_ptr(), output, 4);
    }
    0
}
/// # Safety
/// output spans 16 writable bytes. op 0 clear, 1 BeginPath (dx!=0 clears first),
/// 2 EndPath, 3 PartialEndPath(dx,dy), 4 AlignLeftTop. GDI operations exclusively
/// borrow dc under the renderer operation guard; no callback may unwind here.
/// Output is bool u32 for GDI, four i32s for alignment; failures leave it intact.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_path_action_v1(
    id: u64,
    op: u32,
    dc: usize,
    dx: i32,
    dy: i32,
    output: *mut u8,
) -> u32 {
    if output.is_null() || op > 4 {
        return 3;
    }
    let result = match PATHS.with(|s| {
        let p = s.get_mut(id)?;
        let values = match op {
            0 => {
                p.clear();
                [0; 4]
            }
            1 => {
                if dx != 0 {
                    p.clear();
                }
                // SAFETY: the export's exclusive, live HDC contract.
                [i32::from(unsafe { BeginPath(dc) } != 0), 0, 0, 0]
            }
            2 | 3 => [i32::from(end(p, dc, op == 3, dx, dy)?), 0, 0, 0],
            4 => p.align(),
            _ => unreachable!(),
        };
        Ok(values)
    }) {
        Ok(v) => v,
        Err(e) => return e,
    };
    let mut bytes = [0; 16];
    for (chunk, v) in bytes.chunks_exact_mut(4).zip(result) {
        chunk.copy_from_slice(&v.to_le_bytes());
    }
    // SAFETY: detached initialized packet and caller's writable output span.
    unsafe {
        core::ptr::copy_nonoverlapping(bytes.as_ptr(), output, 16);
    }
    0
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn checked_ids_detached_packets_and_failed_admission_preserve_output() {
        let mut out = [0xa5; 32];
        // SAFETY: initialized packet inputs and sufficient unaligned writable
        // outputs; handles are created here and every owner is released once.
        unsafe {
            assert_eq!(xy_vsfilter_path_create_v1(0, out.as_mut_ptr().add(1)), 0);
            let a = u64::from_le_bytes(out[1..9].try_into().unwrap());
            let mut value = Path::allocate(1).unwrap();
            value.buffers().0[0] = Point { x: 13, y: -7 };
            let packet = value.encode().unwrap();
            assert_eq!(
                xy_vsfilter_path_write_v1(a, packet.as_ptr(), packet.len()),
                0
            );
            assert_eq!(xy_vsfilter_path_create_v1(a, out.as_mut_ptr()), 0);
            let b = u64::from_le_bytes(out[..8].try_into().unwrap());
            assert_ne!(a, b);
            assert_eq!(xy_vsfilter_path_create_v1(0, out.as_mut_ptr()), 0);
            let empty = u64::from_le_bytes(out[..8].try_into().unwrap());
            assert_eq!(xy_vsfilter_path_pair_v1(a, empty, 1, out.as_mut_ptr()), 0);
            assert_eq!(xy_vsfilter_path_pair_v1(a, b, 0, out.as_mut_ptr()), 0);
            assert_eq!(out[0], 1);
            assert_eq!(
                xy_vsfilter_path_write_v1(a, packet.as_ptr(), packet.len() - 1),
                3
            );
            out.fill(0xa5);
            assert_eq!(
                xy_vsfilter_path_read_v1(a, out.as_mut_ptr(), packet.len() - 1),
                4
            );
            assert!(out.iter().all(|v| *v == 0xa5));
            assert_eq!(xy_vsfilter_path_read_v1(a, out.as_mut_ptr().add(1), 31), 0);
            assert_eq!(&out[1..1 + packet.len()], packet);
            // Malformed transform packets and stale owners must not change data.
            let mut transform = [0u8; 82];
            assert_eq!(xy_vsfilter_path_transform_v1(a, core::ptr::null(), 81), 3);
            assert_eq!(xy_vsfilter_path_transform_v1(a, transform.as_ptr(), 80), 3);
            transform[81] = 2;
            assert_eq!(
                xy_vsfilter_path_transform_v1(a, transform.as_ptr().add(1), 81),
                3
            );
            transform[81] = 0;
            assert_eq!(
                xy_vsfilter_path_transform_v1(u64::MAX, transform.as_ptr().add(1), 81),
                1
            );
            assert_eq!(xy_vsfilter_path_read_v1(a, out.as_mut_ptr().add(1), 31), 0);
            assert_eq!(&out[1..1 + packet.len()], packet);
            assert_eq!(
                xy_vsfilter_path_action_v1(a, 0, 0, 0, 0, out.as_mut_ptr()),
                0
            );
            assert_eq!(xy_vsfilter_path_pair_v1(a, b, 0, out.as_mut_ptr()), 0);
            assert_eq!(out[0], 0);
            for id in [a, b, empty] {
                assert_eq!(xy_vsfilter_path_destroy_v1(id), 0);
            }
            out.fill(0xa5);
            assert_eq!(xy_vsfilter_path_info_v1(a, out.as_mut_ptr()), 1);
            assert!(out.iter().all(|v| *v == 0xa5));
        }
        let boundary = Boundary::new();
        assert_eq!(boundary.with(|_| boundary.with(|_| Ok(()))), Err(2));
        assert_eq!(boundary.with(|s| s.create(Path::default())), Ok(1));
    }
}
