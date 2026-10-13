// SPDX-License-Identifier: GPL-2.0-or-later
//! Keep the production static library on the host DLL's existing C runtime.
//! Rust std's modern Windows OS dependencies must not change DLL loadability.
use core::alloc::{GlobalAlloc, Layout};

extern "C" {
    fn _aligned_malloc(size: usize, alignment: usize) -> *mut u8;
    fn _aligned_free(pointer: *mut u8);
    #[cfg(all(feature = "native-runtime", not(test)))]
    fn abort() -> !;
}

pub struct CrtAllocator;

// SAFETY: MSVC's aligned allocator returns unique storage of at least `size`
// bytes at the requested power-of-two alignment, or null. GlobalAlloc callers
// supply a nonzero Layout and return each allocation at most once. Allocation
// and deallocation use the matching CRT pair; neither function unwinds.
unsafe impl GlobalAlloc for CrtAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        // SAFETY: Layout enforces power-of-two alignment; GlobalAlloc requires
        // nonzero size. The CRT checks allocation failure and returns null.
        unsafe { _aligned_malloc(layout.size(), layout.align()) }
    }

    unsafe fn dealloc(&self, pointer: *mut u8, _layout: Layout) {
        // SAFETY: pointer is a live allocation from this same allocator.
        unsafe { _aligned_free(pointer) }
    }
}

#[cfg(all(feature = "native-runtime", not(test)))]
#[global_allocator]
static ALLOCATOR: CrtAllocator = CrtAllocator;

/// A link-time check that the native adapter received the production runtime.
#[cfg(all(feature = "native-runtime", not(test)))]
#[no_mangle]
pub extern "C" fn xy_vsfilter_native_runtime_v1() {}

#[cfg(all(feature = "native-runtime", not(test)))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo<'_>) -> ! {
    // Never unwind through the C ABI. Infallible allocation failure follows the
    // same abort path; recovery from exhaustion is not claimed as compatible.
    // SAFETY: abort takes no arguments and never returns, as declared by MSVC.
    unsafe { abort() }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crt_alignment_zeroing_and_reallocation_preserve_owned_bytes() {
        for size in [1, 2, 3, 16, 513] {
            for power in 0..=12 {
                let layout = Layout::from_size_align(size, 1 << power).unwrap();
                // SAFETY: nonzero layouts, checked allocations, bounded accesses
                // and exactly one release per successful final allocation.
                unsafe {
                    let pointer = CrtAllocator.alloc_zeroed(layout);
                    assert!(!pointer.is_null());
                    assert_eq!(pointer as usize % layout.align(), 0);
                    assert!(core::slice::from_raw_parts(pointer, size)
                        .iter()
                        .all(|&b| b == 0));
                    core::ptr::write_bytes(pointer, 0x5a, size);
                    let resized = CrtAllocator.realloc(pointer, layout, size + 17);
                    assert!(!resized.is_null());
                    assert_eq!(resized as usize % layout.align(), 0);
                    assert!(core::slice::from_raw_parts(resized, size)
                        .iter()
                        .all(|&b| b == 0x5a));
                    let layout = Layout::from_size_align(size + 17, layout.align()).unwrap();
                    CrtAllocator.dealloc(resized, layout);
                }
            }
        }
    }
}
