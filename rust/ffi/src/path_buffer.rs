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
/// allowed until native use ends; release then consumes the unique owner.
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
