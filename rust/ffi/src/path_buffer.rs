// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use core::ffi::c_void;
use xy_vsfilter_compat::path_buffer::Buffer;
#[repr(C)]
pub struct NativeBuffers {
    points: *mut c_void,
    types: *mut u8,
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_path_buffer_create(count: i32) -> *mut Buffer {
    Buffer::new(count).map_or(core::ptr::null_mut(), |buffer| {
        Box::into_raw(Box::new(buffer))
    })
}
/// # Safety
/// owner is live and exclusive. output is aligned writable NativeBuffers storage
/// disjoint from owner and both buffers. After this call native code may read or
/// write the exported count-sized buffers. No further Rust call using owner is
/// allowed until native use ends. Afterwards append or release may use owner;
/// further native accesses require a fresh loan. Release consumes the owner.
/// GetPath is an output-only boundary: the caller consumes buffer contents only
/// after GetPath reports that all count entries were written.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_path_buffer_loan(
    owner: *mut Buffer,
    output: *mut NativeBuffers,
) {
    // SAFETY: unique owner produces both disjoint loans in the same borrow.
    let (points, types) = unsafe { &mut *owner }.buffers();
    let pointers = NativeBuffers {
        points: points.as_mut_ptr().cast(),
        types: types.as_mut_ptr(),
    };
    // SAFETY: native output pointer slots are disjoint from both loans and owner.
    unsafe {
        output.write(pointers);
    }
}
/// # Safety
/// owner is live and immutable, and all previous native buffer accesses have
/// ended. points/types are disjoint writable tails spanning owner.len() POINTs
/// and bytes, also disjoint from owner and its buffers. They may be uninitialized.
/// Pointers remain valid and exclusive until return. On decline no output changes;
/// a caller resuming native source reads must request a fresh paired loan.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_path_buffer_append(
    owner: *const Buffer,
    points: *mut c_void,
    types: *mut u8,
    dx: i32,
    dy: i32,
) -> u32 {
    if points.is_null() || types.is_null() {
        return 0;
    }
    // SAFETY: native source loans have ended; owner remains immutable here.
    let source = unsafe { &*owner };
    let count = source.len();
    // SAFETY: caller supplies disjoint aligned writable output tails. Using
    // MaybeUninit does not assume native realloc initialized the new bytes.
    let points = unsafe {
        core::slice::from_raw_parts_mut(
            points.cast::<core::mem::MaybeUninit<xy_vsfilter_compat::path_align::Point>>(),
            count,
        )
    };
    // SAFETY: this byte tail is disjoint from points and the immutable owner.
    let types = unsafe {
        core::slice::from_raw_parts_mut(types.cast::<core::mem::MaybeUninit<u8>>(), count)
    };
    u32::from(source.append(points, types, dx, dy))
}
/// # Safety
/// owner is null or the unique create result. All exported pointers must have
/// finished native use before this call and must never be accessed afterwards.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_path_buffer_release(owner: *mut Buffer) {
    if !owner.is_null() {
        // SAFETY: the native scope ends both loans and returns its unique owner.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use xy_vsfilter_compat::path_align::Point;
    #[test]
    fn append_initializes_native_tails_and_decline_allows_a_fresh_loan() {
        use core::mem::MaybeUninit;
        let owner = xy_vsfilter_path_buffer_create(2);
        let mut buffers = NativeBuffers {
            points: core::ptr::null_mut(),
            types: core::ptr::null_mut(),
        };
        let mut points = [MaybeUninit::new(Point { x: 71, y: 72 }); 2];
        let mut types = [MaybeUninit::new(73u8); 2];
        // SAFETY: all native source uses end before append; destinations are
        // aligned, disjoint, writable, and initialized before inspection.
        unsafe {
            xy_vsfilter_path_buffer_loan(owner, &mut buffers);
            buffers
                .points
                .cast::<Point>()
                .write(Point { x: i32::MAX, y: 0 });
            buffers
                .points
                .cast::<Point>()
                .add(1)
                .write(Point { x: 3, y: 4 });
            buffers.types.write(1);
            buffers.types.add(1).write(255);
            assert_eq!(
                xy_vsfilter_path_buffer_append(
                    owner,
                    points.as_mut_ptr().cast(),
                    types.as_mut_ptr().cast(),
                    1,
                    -1
                ),
                0
            );
            assert_eq!(points[0].assume_init(), Point { x: 71, y: 72 });
            assert_eq!(types[0].assume_init(), 73);
            // Native fallback must acquire new pointer provenance after the
            // Rust source borrow, even though the allocation address is stable.
            xy_vsfilter_path_buffer_loan(owner, &mut buffers);
            assert_eq!((*buffers.points.cast::<Point>()).x, i32::MAX);
            buffers.points.cast::<Point>().write(Point { x: 1, y: 2 });
            assert_eq!(
                xy_vsfilter_path_buffer_append(
                    owner,
                    points.as_mut_ptr().cast(),
                    types.as_mut_ptr().cast(),
                    1,
                    -1
                ),
                1
            );
            assert_eq!(points[0].assume_init(), Point { x: 2, y: 1 });
            assert_eq!(points[1].assume_init(), Point { x: 4, y: 3 });
            assert_eq!(types[0].assume_init(), 1);
            assert_eq!(types[1].assume_init(), 255);
            xy_vsfilter_path_buffer_release(owner);
        }
    }
    #[test]
    fn native_writes_and_reads_end_before_owner_release() {
        let owner = xy_vsfilter_path_buffer_create(4);
        assert!(!owner.is_null());
        let mut buffers = NativeBuffers {
            points: core::ptr::null_mut(),
            types: core::ptr::null_mut(),
        };
        // SAFETY: disjoint output slots and four initialized writable records;
        // emulate native GetPath, then native consumption, then release.
        unsafe {
            xy_vsfilter_path_buffer_loan(owner, &mut buffers);
            let points = buffers.points.cast::<Point>();
            for i in 0..4 {
                points.add(i).write(Point {
                    x: i as i32,
                    y: -(i as i32),
                });
                buffers.types.add(i).write(i as u8 + 1);
            }
            for i in 0..4 {
                assert_eq!((*points.add(i)).y, -(i as i32));
                assert_eq!(*buffers.types.add(i), i as u8 + 1);
            }
            xy_vsfilter_path_buffer_release(owner);
            xy_vsfilter_path_buffer_release(core::ptr::null_mut());
        }
    }
}
