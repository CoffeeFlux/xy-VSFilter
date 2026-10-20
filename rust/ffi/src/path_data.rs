// SPDX-License-Identifier: GPL-2.0-or-later
//! Checked persistent paths, scan results and widened outlines. No owner pointer or buffer loan
//! crosses the ABI.
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
    state: UnsafeCell<Owners>,
}
struct Owners {
    paths: Store,
    scans: xy_vsfilter_compat::scan_convert::Store,
    regions: xy_vsfilter_compat::wide_region::Store,
    rasters: xy_vsfilter_compat::raster::Store,
    coefficients: xy_vsfilter_compat::gaussian_coefficients::Store,
    ellipse: xy_vsfilter_compat::ellipse::Cache<crate::windows_ellipse_numeric::Numeric>,
}
// SAFETY: all access uses the atomic gate; HRTB closures cannot return borrowed
// references, and all four registries own only Send data. No native pointer is
// stored. Path->scan conversion uses disjoint fields under this single gate,
// so it never nests admissions or releases a path loan across a callback.
// The ellipse cache owns only scalar numeric state, an arc and a table. Region
// conversion holds disjoint scan/region/cache borrows and makes no callbacks.
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
            state: UnsafeCell::new(Owners {
                paths: Store::new(),
                scans: xy_vsfilter_compat::scan_convert::Store::new(),
                regions: xy_vsfilter_compat::wide_region::Store::new(),
                rasters: xy_vsfilter_compat::raster::Store::new(),
                coefficients: xy_vsfilter_compat::gaussian_coefficients::Store::new(),
                ellipse: xy_vsfilter_compat::ellipse::Cache::new(),
            }),
        }
    }
    fn with<T>(&self, f: impl for<'a> FnOnce(&'a mut Store) -> Result<T, u32>) -> Result<T, u32> {
        self.with_owners(|owners| f(&mut owners.paths))
    }
    fn with_owners<T>(
        &self,
        f: impl for<'a> FnOnce(&'a mut Owners) -> Result<T, u32>,
    ) -> Result<T, u32> {
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
/// output lends sixteen writable bytes for a checked ID and packet length.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_gaussian_coefficients_create_v1(
    sigma: u64,
    output: *mut u8,
) -> u32 {
    if output.is_null() {
        return 3;
    }
    let (id, len) = match PATHS.with_owners(|o| {
        let mut numeric = crate::windows_gaussian_numeric::Numeric::new(sigma);
        let value =
            xy_vsfilter_compat::gaussian_coefficients::Coefficients::build(sigma, &mut numeric)?;
        let len = value.bytes().len() as u64;
        Ok((o.coefficients.insert(value)?, len))
    }) {
        Ok(v) => v,
        Err(e) => return e,
    };
    let mut packet = [0u8; 16];
    packet[..8].copy_from_slice(&id.to_le_bytes());
    packet[8..].copy_from_slice(&len.to_le_bytes());
    // SAFETY: detached initialized scalars and the caller's writable output.
    unsafe {
        core::ptr::copy_nonoverlapping(packet.as_ptr(), output, 16);
    }
    0
}
/// # Safety
/// output lends capacity writable bytes, disjoint from private owner storage.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_gaussian_coefficients_read_v1(
    id: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    if output.is_null() || capacity > isize::MAX as usize {
        return 3;
    }
    match PATHS.with_owners(|o| {
        let bytes = o.coefficients.get(id)?.bytes();
        if bytes.len() > capacity {
            return Err(4);
        }
        // SAFETY: checked capacity, exclusive admission and private source.
        unsafe {
            core::ptr::copy_nonoverlapping(bytes.as_ptr(), output, bytes.len());
        }
        Ok(())
    }) {
        Ok(()) => 0,
        Err(e) => e,
    }
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_gaussian_coefficients_destroy_v1(id: u64) -> u32 {
    match PATHS.with_owners(|o| o.coefficients.remove(id)) {
        Ok(()) => 0,
        Err(e) => e,
    }
}
/// # Safety
/// output lends eight writable bytes for the independent checked overlay ID.
/// Strength/scale arguments are raw float bits. No image storage is borrowed.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_overlay_be_blur_v1(
    id: u64,
    be: u32,
    sx: u32,
    sy: u32,
    output: *mut u8,
) -> u32 {
    if output.is_null() {
        return 3;
    }
    let id = match PATHS.with_owners(|o| {
        let mut numeric = crate::windows_be_blur_numeric::Numeric::new(be, sx, sy);
        let image = if id == 0 {
            xy_vsfilter_compat::raster::Image::decode(&[0; 32])?.be_blur(&mut numeric)?
        } else {
            o.rasters.get(id)?.be_blur(&mut numeric)?
        };
        o.rasters.insert(image)
    }) {
        Ok(v) => v,
        Err(e) => return e,
    };
    // SAFETY: detached scalar and caller's writable eight-byte output.
    unsafe {
        core::ptr::copy_nonoverlapping(id.to_le_bytes().as_ptr(), output, 8);
    }
    0
}
/// # Safety
/// output lends eight writable bytes for an independent checked owner ID.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_overlay_clone_v1(id: u64, output: *mut u8) -> u32 {
    if output.is_null() {
        return 3;
    }
    let id = match PATHS.with_owners(|o| {
        if id == 0 {
            return Ok(0);
        }
        let image = o.rasters.get(id)?.duplicate()?;
        o.rasters.insert(image)
    }) {
        Ok(v) => v,
        Err(e) => return e,
    };
    // SAFETY: detached scalar and caller's writable eight-byte output.
    unsafe {
        core::ptr::copy_nonoverlapping(id.to_le_bytes().as_ptr(), output, 8);
    }
    0
}
/// # Safety
/// output lends eight writable bytes for the retained or newly created owner ID.
/// No native field or pixel storage is borrowed while the offset is updated.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_overlay_offset_v1(
    id: u64,
    x: i32,
    y: i32,
    output: *mut u8,
) -> u32 {
    if output.is_null() {
        return 3;
    }
    let id = match PATHS.with_owners(|o| {
        if id == 0 {
            let mut image = xy_vsfilter_compat::raster::Image::decode(&[0; 32])?;
            image.offset(x, y);
            o.rasters.insert(image)
        } else {
            o.rasters.get_mut(id)?.offset(x, y);
            Ok(id)
        }
    }) {
        Ok(v) => v,
        Err(e) => return e,
    };
    // SAFETY: detached scalar and caller's writable eight-byte output.
    unsafe {
        core::ptr::copy_nonoverlapping(id.to_le_bytes().as_ptr(), output, 8);
    }
    0
}
/// # Safety
/// output spans sixteen writable bytes for a checked raster ID and byte length.
/// Both sources and the new owned result remain admitted together. No pointer
/// into any owner, span or pixel allocation crosses this operation.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_raster_create_v1(
    scan: u64,
    region: u64,
    offset_x: i32,
    offset_y: i32,
    sub_x: i32,
    sub_y: i32,
    output: *mut u8,
) -> u32 {
    if output.is_null() {
        return 3;
    }
    let (id, length) = match PATHS.with_owners(|o| {
        let image = xy_vsfilter_compat::raster::Image::render(
            o.scans.get(scan)?,
            o.regions.get(region)?,
            [offset_x, offset_y],
            [sub_x, sub_y],
        )?;
        let length = image.bytes().len() as u64;
        Ok((o.rasters.insert(image)?, length))
    }) {
        Ok(v) => v,
        Err(e) => return e,
    };
    // SAFETY: caller's writable output, detached scalars after admission ends.
    unsafe {
        core::ptr::copy_nonoverlapping(id.to_le_bytes().as_ptr(), output, 8);
        core::ptr::copy_nonoverlapping(length.to_le_bytes().as_ptr(), output.add(8), 8);
    }
    0
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_raster_destroy_v1(id: u64) -> u32 {
    match PATHS.with_owners(|o| o.rasters.remove(id)) {
        Ok(()) => 0,
        Err(e) => e,
    }
}
/// # Safety
/// output spans capacity writable bytes and cannot alias a private owner. The
/// checked copy holds admission through its entire write; no owner loan escapes.
/// Invalid IDs or short buffers leave output unchanged.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_raster_read_v1(
    id: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    if output.is_null() || capacity > isize::MAX as usize {
        return 3;
    }
    match PATHS.with_owners(|o| {
        let bytes = o.rasters.get(id)?.bytes();
        if !o.rasters.get(id)?.all_known() {
            return Err(6);
        }
        if bytes.len() > capacity {
            return Err(4);
        }
        // SAFETY: caller's disjoint writable buffer, checked size and unique
        // admission. The source cannot be mutated/released during the copy.
        unsafe {
            core::ptr::copy_nonoverlapping(bytes.as_ptr(), output, bytes.len());
        }
        Ok(())
    }) {
        Ok(()) => 0,
        Err(e) => e,
    }
}
/// # Safety
/// input is an initialized, immutable wire packet for this call; output lends
/// eight writable bytes. The packet contains values and validity bits, never
/// native uninitialized bytes. Output may overlap input after decode completes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_overlay_import_v1(
    input: *const u8,
    length: usize,
    output: *mut u8,
) -> u32 {
    if input.is_null() || output.is_null() || length < 32 || length > isize::MAX as usize {
        return 3;
    }
    // SAFETY: bounded initialized packet, borrowed only during owned decoding.
    let image = match xy_vsfilter_compat::raster::Image::decode(unsafe {
        core::slice::from_raw_parts(input, length)
    }) {
        Ok(v) => v,
        Err(e) => return e,
    };
    let id = match PATHS.with_owners(|o| o.rasters.insert(image)) {
        Ok(v) => v,
        Err(e) => return e,
    };
    // SAFETY: detached scalar; no input borrow remains.
    unsafe {
        core::ptr::copy_nonoverlapping(id.to_le_bytes().as_ptr(), output, 8);
    }
    0
}
/// # Safety
/// output lends eight writable bytes for the detached encoded length.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_overlay_info_v1(id: u64, output: *mut u8) -> u32 {
    if output.is_null() {
        return 3;
    }
    let length = match PATHS.with_owners(|o| o.rasters.get(id)?.wire_len().ok_or(5u32)) {
        Ok(v) => v as u64,
        Err(e) => return e,
    };
    // SAFETY: detached scalar into caller's writable buffer.
    unsafe {
        core::ptr::copy_nonoverlapping(length.to_le_bytes().as_ptr(), output, 8);
    }
    0
}
/// # Safety
/// output lends capacity writable bytes. No canonical storage address escapes;
/// unknown bytes are represented by validity zero and an opaque placeholder.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_overlay_read_v1(
    id: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    if output.is_null() || capacity > isize::MAX as usize {
        return 3;
    }
    let bytes = match PATHS.with_owners(|o| {
        let image = o.rasters.get(id)?;
        if image.wire_len().ok_or(5u32)? > capacity {
            return Err(4);
        }
        image.encode()
    }) {
        Ok(v) => v,
        Err(e) => return e,
    };
    // SAFETY: detached initialized packet and checked caller capacity.
    unsafe {
        core::ptr::copy_nonoverlapping(bytes.as_ptr(), output, bytes.len());
    }
    0
}
/// # Safety
/// output lends eight writable bytes for the new checked ID, or zero for the
/// reference's null-result branch. Source ownership remains admitted throughout.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_overlay_variance_v1(
    id: u64,
    x: u32,
    y: u32,
    output: *mut u8,
) -> u32 {
    if output.is_null() {
        return 3;
    }
    let id = match PATHS.with_owners(|o| {
        let image = if id == 0 {
            xy_vsfilter_compat::raster::Image::decode(&[0; 32])?.variance(x, y)?
        } else {
            o.rasters.get(id)?.variance(x, y)?
        };
        match image {
            Some(v) => o.rasters.insert(v),
            None => Ok(0),
        }
    }) {
        Ok(v) => v,
        Err(e) => return e,
    };
    // SAFETY: detached scalar after source borrow ends.
    unsafe {
        core::ptr::copy_nonoverlapping(id.to_le_bytes().as_ptr(), output, 8);
    }
    0
}
/// # Safety
/// output spans eight writable bytes. No owner address is exported.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_region_create_v1(output: *mut u8) -> u32 {
    if output.is_null() {
        return 3;
    }
    let id = match PATHS.with_owners(|o| o.regions.create()) {
        Ok(v) => v,
        Err(e) => return e,
    };
    // SAFETY: detached scalar and caller's writable eight-byte output.
    unsafe {
        core::ptr::copy_nonoverlapping(id.to_le_bytes().as_ptr(), output, 8);
    }
    0
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_region_destroy_v1(id: u64) -> u32 {
    match PATHS.with_owners(|o| o.regions.remove(id)) {
        Ok(()) => 0,
        Err(e) => e,
    }
}
/// # Safety
/// output spans four writable bytes. Scan, region and ellipse cache are admitted
/// together. No native callback or foreign input pointer participates.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_region_convert_v1(
    id: u64,
    scan: u64,
    rx: i32,
    ry: i32,
    output: *mut u8,
) -> u32 {
    if output.is_null() {
        return 3;
    }
    let result = match PATHS.with_owners(|o| {
        let source = o.scans.get(scan)?;
        o.regions
            .get_mut(id)?
            .convert(source.spans(), rx, ry, &mut o.ellipse)
    }) {
        Ok(v) => u32::from(v),
        Err(e) => return e,
    };
    // SAFETY: detached scalar, after callback and all registry borrows end.
    unsafe {
        core::ptr::copy_nonoverlapping(result.to_le_bytes().as_ptr(), output, 4);
    }
    0
}
/// # Safety
/// output spans eight writable bytes for the detached snapshot length.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_region_info_v1(id: u64, output: *mut u8) -> u32 {
    if output.is_null() {
        return 3;
    }
    let length = match PATHS.with_owners(|o| o.regions.get(id)?.wire_len().ok_or(5)) {
        Ok(v) => v as u64,
        Err(e) => return e,
    };
    // SAFETY: detached scalar and caller's writable output.
    unsafe {
        core::ptr::copy_nonoverlapping(length.to_le_bytes().as_ptr(), output, 8);
    }
    0
}
/// # Safety
/// output spans capacity writable bytes. Invalid/short requests leave it intact.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_region_read_v1(
    id: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    if output.is_null() || capacity > isize::MAX as usize {
        return 3;
    }
    let bytes = match PATHS.with_owners(|o| {
        let region = o.regions.get(id)?;
        if region.wire_len().ok_or(5u32)? > capacity {
            return Err(4);
        }
        region.encode()
    }) {
        Ok(v) => v,
        Err(e) => return e,
    };
    // SAFETY: detached packet after registry borrow ends; capacity was checked.
    unsafe {
        core::ptr::copy_nonoverlapping(bytes.as_ptr(), output, bytes.len());
    }
    0
}
/// # Safety
/// output spans eight writable bytes for the new checked scan ID.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_scan_create_v1(output: *mut u8) -> u32 {
    if output.is_null() {
        return 3;
    }
    let id = match PATHS.with_owners(|owners| owners.scans.create()) {
        Ok(id) => id,
        Err(e) => return e,
    };
    // SAFETY: detached ID and caller's writable span; no owner pointer escapes.
    unsafe {
        core::ptr::copy_nonoverlapping(id.to_le_bytes().as_ptr(), output, 8);
    }
    0
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_scan_destroy_v1(id: u64) -> u32 {
    match PATHS.with_owners(|owners| owners.scans.remove(id)) {
        Ok(()) => 0,
        Err(e) => e,
    }
}
/// # Safety
/// output spans four writable bytes. Both checked owners remain admitted for the
/// operation; no callbacks, foreign buffers or retained path loans are involved.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_scan_convert_v1(
    id: u64,
    path: u64,
    width: i32,
    height: i32,
    output: *mut u8,
) -> u32 {
    if output.is_null() {
        return 3;
    }
    let result = match PATHS.with_owners(|owners| {
        let path = owners.paths.get(path)?;
        owners
            .scans
            .get_mut(id)?
            .convert::<crate::windows_scan_curve::Numeric>(path, [width, height])
    }) {
        Ok(v) => u32::from(v),
        Err(e) => return e,
    };
    // SAFETY: detached scalar, after all registry borrows have ended.
    unsafe {
        core::ptr::copy_nonoverlapping(result.to_le_bytes().as_ptr(), output, 4);
    }
    0
}
/// # Safety
/// output spans eight writable bytes for the encoded snapshot length.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_scan_info_v1(id: u64, output: *mut u8) -> u32 {
    if output.is_null() {
        return 3;
    }
    let length = match PATHS.with_owners(|owners| owners.scans.get(id)?.wire_len().ok_or(5)) {
        Ok(v) => v as u64,
        Err(e) => return e,
    };
    // SAFETY: detached scalar and caller's writable eight-byte span.
    unsafe {
        core::ptr::copy_nonoverlapping(length.to_le_bytes().as_ptr(), output, 8);
    }
    0
}
/// # Safety
/// output spans capacity writable bytes. Invalid/short requests leave it intact.
/// The detached snapshot never aliases the registry or caller's other data.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_scan_read_v1(
    id: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    if output.is_null() || capacity > isize::MAX as usize {
        return 3;
    }
    let bytes = match PATHS.with_owners(|owners| {
        let scan = owners.scans.get(id)?;
        if scan.wire_len().ok_or(5u32)? > capacity {
            return Err(4);
        }
        scan.encode()
    }) {
        Ok(v) => v,
        Err(e) => return e,
    };
    // SAFETY: caller's capacity checked before encoding; detached bytes copied
    // after the admission/borrow ends, so overlapping foreign packets are safe.
    unsafe {
        core::ptr::copy_nonoverlapping(bytes.as_ptr(), output, bytes.len());
    }
    0
}
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
            assert_eq!(
                xy_vsfilter_gaussian_coefficients_create_v1(0, core::ptr::null_mut()),
                3
            );
            assert_eq!(
                xy_vsfilter_gaussian_coefficients_create_v1(
                    1.0f64.to_bits(),
                    out.as_mut_ptr().add(1)
                ),
                0
            );
            let coefficient = u64::from_le_bytes(out[1..9].try_into().unwrap());
            assert_eq!(u64::from_le_bytes(out[9..17].try_into().unwrap()), 40);
            assert_eq!((out[0], out[17]), (0xa5, 0xa5));
            let mut coefficient_bytes = [0xa5; 42];
            assert_eq!(
                xy_vsfilter_gaussian_coefficients_read_v1(
                    coefficient,
                    coefficient_bytes.as_mut_ptr().add(1),
                    39
                ),
                4
            );
            assert_eq!(coefficient_bytes, [0xa5; 42]);
            assert_eq!(
                xy_vsfilter_gaussian_coefficients_read_v1(
                    coefficient,
                    coefficient_bytes.as_mut_ptr().add(1),
                    40
                ),
                0
            );
            assert_eq!((coefficient_bytes[0], coefficient_bytes[41]), (0xa5, 0xa5));
            assert_eq!(xy_vsfilter_gaussian_coefficients_destroy_v1(coefficient), 0);
            let saved = coefficient_bytes;
            assert_eq!(
                xy_vsfilter_gaussian_coefficients_read_v1(
                    coefficient,
                    coefficient_bytes.as_mut_ptr(),
                    42
                ),
                1
            );
            assert_eq!(coefficient_bytes, saved);
            assert_eq!(xy_vsfilter_gaussian_coefficients_destroy_v1(coefficient), 1);
            out.fill(0xa5);
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
            let mut scan_id = [0; 8];
            assert_eq!(xy_vsfilter_scan_create_v1(scan_id.as_mut_ptr()), 0);
            let scan = u64::from_le_bytes(scan_id);
            assert_eq!(
                xy_vsfilter_scan_convert_v1(scan, a, 32, 8, out.as_mut_ptr()),
                0
            );
            assert_eq!(out[0], 1);
            let mut snapshot = [0xa5; 64];
            assert_eq!(
                xy_vsfilter_scan_read_v1(scan, snapshot.as_mut_ptr().add(1), 43),
                4
            );
            assert_eq!(snapshot, [0xa5; 64]);
            assert_eq!(xy_vsfilter_scan_info_v1(scan, scan_id.as_mut_ptr()), 0);
            assert_eq!(u64::from_le_bytes(scan_id), 44);
            assert_eq!(
                xy_vsfilter_scan_read_v1(scan, snapshot.as_mut_ptr().add(1), 63),
                0
            );
            assert_eq!(&snapshot[1..9], &[32, 0, 0, 0, 8, 0, 0, 0]);
            assert_eq!(
                xy_vsfilter_scan_convert_v1(scan, u64::MAX, 32, 8, out.as_mut_ptr()),
                1
            );
            assert_eq!(xy_vsfilter_region_create_v1(scan_id.as_mut_ptr()), 0);
            let region = u64::from_le_bytes(scan_id);
            out.fill(0xa5);
            assert_eq!(
                xy_vsfilter_region_convert_v1(region, u64::MAX, 1, 1, out.as_mut_ptr()),
                1
            );
            assert_eq!(out, [0xa5; 32]);
            assert_eq!(
                xy_vsfilter_region_convert_v1(region, scan, 1, 1, out.as_mut_ptr()),
                0
            );
            assert_eq!(out[0], 1);
            snapshot.fill(0xa5);
            assert_eq!(
                xy_vsfilter_region_read_v1(region, snapshot.as_mut_ptr().add(1), 11),
                4
            );
            assert_eq!(snapshot, [0xa5; 64]);
            assert_eq!(
                xy_vsfilter_region_read_v1(region, snapshot.as_mut_ptr().add(1), 63),
                0
            );
            assert_eq!(&snapshot[1..13], &[1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
            assert_eq!(xy_vsfilter_region_info_v1(region, scan_id.as_mut_ptr()), 0);
            assert_eq!(u64::from_le_bytes(scan_id), 12);
            let mut raster_info = [0xa5u8; 18];
            assert_eq!(
                xy_vsfilter_raster_create_v1(
                    u64::MAX,
                    region,
                    0,
                    0,
                    0,
                    0,
                    raster_info.as_mut_ptr().add(1)
                ),
                1
            );
            assert_eq!(raster_info, [0xa5; 18]);
            assert_eq!(
                xy_vsfilter_raster_create_v1(scan, region, 0, 0, 0, 0, core::ptr::null_mut()),
                3
            );
            assert_eq!(
                xy_vsfilter_raster_create_v1(
                    scan,
                    region,
                    0,
                    0,
                    -1,
                    -9,
                    raster_info.as_mut_ptr().add(1)
                ),
                0
            );
            let raster = u64::from_le_bytes(raster_info[1..9].try_into().unwrap());
            assert_eq!(
                u64::from_le_bytes(raster_info[9..17].try_into().unwrap()),
                80
            );
            assert_eq!((raster_info[0], raster_info[17]), (0xa5, 0xa5));
            let mut raster_bytes = [0xa5; 82];
            assert_eq!(
                xy_vsfilter_raster_read_v1(raster, raster_bytes.as_mut_ptr().add(1), 79),
                4
            );
            assert_eq!(raster_bytes, [0xa5; 82]);
            assert_eq!(
                xy_vsfilter_raster_read_v1(raster, raster_bytes.as_mut_ptr().add(1), 80),
                0
            );
            let fields: alloc::vec::Vec<_> = raster_bytes[1..33]
                .chunks_exact(4)
                .map(|b| i32::from_le_bytes(b.try_into().unwrap()))
                .collect();
            assert_eq!(fields, [-7, -7, 39, 15, 6, 3, 16, 3]);
            assert_eq!(&raster_bytes[33..81], &[0; 48]);
            assert_eq!((raster_bytes[0], raster_bytes[81]), (0xa5, 0xa5));
            assert_eq!(xy_vsfilter_raster_destroy_v1(raster), 0);
            raster_bytes.fill(0xa5);
            assert_eq!(
                xy_vsfilter_raster_read_v1(raster, raster_bytes.as_mut_ptr(), 82),
                1
            );
            assert_eq!(raster_bytes, [0xa5; 82]);
            assert_eq!(xy_vsfilter_raster_destroy_v1(raster), 1);
            let mut overlay_wire = [0u8; 96];
            for (i, v) in [0i32, 0, 8, 8, 2, 2, 16, 3].into_iter().enumerate() {
                overlay_wire[i * 4..i * 4 + 4].copy_from_slice(&v.to_le_bytes());
            }
            for (at, v) in [(0, 8), (1, 16), (16, 24), (17, 32)] {
                overlay_wire[32 + at] = v;
                overlay_wire[64 + at] = 1;
            }
            raster_info.fill(0xa5);
            overlay_wire[64] = 2;
            assert_eq!(
                xy_vsfilter_overlay_import_v1(
                    overlay_wire.as_ptr(),
                    96,
                    raster_info.as_mut_ptr().add(1)
                ),
                3
            );
            assert_eq!(raster_info, [0xa5; 18]);
            overlay_wire[64] = 1;
            assert_eq!(
                xy_vsfilter_overlay_import_v1(
                    overlay_wire.as_ptr(),
                    96,
                    raster_info.as_mut_ptr().add(1)
                ),
                0
            );
            let source = u64::from_le_bytes(raster_info[1..9].try_into().unwrap());
            let one = 1.0f32.to_bits();
            assert_eq!(
                xy_vsfilter_overlay_be_blur_v1(source, one, one, one, core::ptr::null_mut()),
                3
            );
            assert_eq!(
                xy_vsfilter_overlay_be_blur_v1(
                    source,
                    one,
                    one,
                    one,
                    raster_info.as_mut_ptr().add(1)
                ),
                0
            );
            let blurred = u64::from_le_bytes(raster_info[1..9].try_into().unwrap());
            assert_ne!(blurred, source);
            assert_eq!((raster_info[0], raster_info[9]), (0xa5, 0xa5));
            assert_eq!(
                xy_vsfilter_overlay_clone_v1(source, raster_info.as_mut_ptr().add(1)),
                0
            );
            let copied = u64::from_le_bytes(raster_info[1..9].try_into().unwrap());
            assert_ne!(copied, source);
            assert_eq!(
                xy_vsfilter_overlay_offset_v1(copied, 19, -23, raster_info.as_mut_ptr().add(1)),
                0
            );
            assert_eq!(
                u64::from_le_bytes(raster_info[1..9].try_into().unwrap()),
                copied
            );
            let mut copy_wire = [0; 96];
            assert_eq!(
                xy_vsfilter_overlay_read_v1(copied, copy_wire.as_mut_ptr(), 96),
                0
            );
            assert_eq!(&copy_wire[8..], &overlay_wire[8..]);
            assert_eq!(&copy_wire[..4], &19i32.to_le_bytes());
            assert_eq!(xy_vsfilter_raster_destroy_v1(copied), 0);
            assert_eq!(
                xy_vsfilter_overlay_variance_v1(source, 0, 0, raster_info.as_mut_ptr().add(1)),
                0
            );
            let shifted = u64::from_le_bytes(raster_info[1..9].try_into().unwrap());
            assert_ne!(source, shifted);
            assert_eq!(xy_vsfilter_raster_destroy_v1(source), 0);
            let mut blur_wire = [0xa5; 162];
            assert_eq!(
                xy_vsfilter_overlay_read_v1(blurred, blur_wire.as_mut_ptr().add(1), 159),
                4
            );
            assert_eq!(blur_wire, [0xa5; 162]);
            assert_eq!(
                xy_vsfilter_overlay_read_v1(blurred, blur_wire.as_mut_ptr().add(1), 160),
                0
            );
            assert_eq!((blur_wire[0], blur_wire[161]), (0xa5, 0xa5));
            assert_eq!(xy_vsfilter_raster_destroy_v1(blurred), 0);
            raster_info.fill(0xa5);
            assert_eq!(
                xy_vsfilter_overlay_be_blur_v1(
                    blurred,
                    one,
                    one,
                    one,
                    raster_info.as_mut_ptr().add(1)
                ),
                1
            );
            assert_eq!(raster_info, [0xa5; 18]);
            let mut wire_out = [0xa5; 98];
            assert_eq!(
                xy_vsfilter_raster_read_v1(shifted, wire_out.as_mut_ptr(), 98),
                6
            );
            assert_eq!(
                xy_vsfilter_overlay_read_v1(shifted, wire_out.as_mut_ptr().add(1), 95),
                4
            );
            assert_eq!(wire_out, [0xa5; 98]);
            assert_eq!(
                xy_vsfilter_overlay_read_v1(shifted, wire_out.as_mut_ptr().add(1), 96),
                0
            );
            assert_eq!(&wire_out[1..97], &overlay_wire);
            assert_eq!((wire_out[0], wire_out[97]), (0xa5, 0xa5));
            assert_eq!(
                xy_vsfilter_overlay_info_v1(shifted, raster_info.as_mut_ptr().add(1)),
                0
            );
            assert_eq!(
                u64::from_le_bytes(raster_info[1..9].try_into().unwrap()),
                96
            );
            assert_eq!(xy_vsfilter_raster_destroy_v1(shifted), 0);
            raster_info.fill(0xa5);
            assert_eq!(
                xy_vsfilter_overlay_variance_v1(shifted, 0, 0, raster_info.as_mut_ptr().add(1)),
                1
            );
            assert_eq!(raster_info, [0xa5; 18]);
            assert_eq!(xy_vsfilter_region_destroy_v1(region), 0);
            assert_eq!(xy_vsfilter_region_info_v1(region, scan_id.as_mut_ptr()), 1);
            assert_eq!(xy_vsfilter_scan_destroy_v1(scan), 0);
            assert_eq!(xy_vsfilter_scan_info_v1(scan, scan_id.as_mut_ptr()), 1);
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
