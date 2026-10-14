// SPDX-License-Identifier: GPL-3.0-or-later
use core::{mem::MaybeUninit, ptr};
use xy_vsfilter_compat::vobsub_trim::{bounds, Layout};

/// # Safety
/// For supported dimensions, source spans width*height pixels of four bytes;
/// each alpha byte is initialized, but RGB need not be. output is writable for
/// four i32 values and disjoint from source. There is no concurrent mutation.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_vobsub_bounds(
    source: *const u8,
    width: i32,
    height: i32,
    output: *mut i32,
) -> u32 {
    let Some(_bytes) = width
        .checked_mul(height)
        .and_then(|n| usize::try_from(n).ok())
        .and_then(|n| n.checked_mul(4))
        .filter(|&n| n <= isize::MAX as usize)
    else {
        return 0;
    };
    let Some(rect) = bounds(width, height, |index| {
        // SAFETY: only initialized alpha bytes inside the checked pixel span.
        unsafe { *source.add(index * 4 + 3) }
    }) else {
        return 0;
    };
    // SAFETY: output is writable and disjoint from the local array and source.
    unsafe {
        ptr::copy_nonoverlapping(rect.as_ptr(), output, 4);
    }
    1
}

/// # Safety
/// Accepted layouts cover the calculated readable source and writable output
/// spans. RGB source bytes and output may be uninitialized. protected_object
/// supplies only an address range, never dereferenced here; writes must not
/// overlap it. Native code may not access these regions concurrently. Zero
/// leaves output untouched so the original incremental copy can handle aliases.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_vobsub_crop(
    source: *const u8,
    pitch: i32,
    width: i32,
    height: i32,
    destination: *mut u8,
    protected_object: *const u8,
    protected_length: usize,
) -> u32 {
    let Some(layout) = Layout::new(width, height, pitch) else {
        return 0;
    };
    let Some(source_bytes) = layout
        .source_span
        .checked_mul(4)
        .filter(|&n| n <= isize::MAX as usize)
    else {
        return 0;
    };
    let Some(destination_bytes) = layout
        .destination_span
        .checked_mul(4)
        .filter(|&n| n <= isize::MAX as usize)
    else {
        return 0;
    };
    let Some(source_end) = (source as usize).checked_add(source_bytes) else {
        return 0;
    };
    let Some(destination_end) = (destination as usize).checked_add(destination_bytes) else {
        return 0;
    };
    let Some(protected_end) = (protected_object as usize).checked_add(protected_length) else {
        return 0;
    };
    if (source_bytes != 0
        && (source as usize) < destination_end
        && (destination as usize) < source_end)
        || (protected_length != 0
            && (protected_object as usize) < destination_end
            && (destination as usize) < protected_end)
    {
        return 0;
    }
    let Some(output) = layout.copy(|index| {
        // SAFETY: copy an opaque pixel without assuming its bytes initialized.
        unsafe { source.add(index * 4).cast::<MaybeUninit<[u8; 4]>>().read() }
    }) else {
        return 0;
    };
    // SAFETY: output is separate Rust-owned storage and destination is writable;
    // a typed MaybeUninit copy retains initialization state of every copied byte.
    unsafe {
        ptr::copy_nonoverlapping(
            output.as_ptr(),
            destination.cast::<MaybeUninit<[u8; 4]>>(),
            output.len(),
        );
    }
    1
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn alpha_scan_and_copy_do_not_require_initialized_rgb_or_output() {
        let mut source = [MaybeUninit::<u8>::uninit(); 16];
        for (i, alpha) in [0, 7, 9, 0].into_iter().enumerate() {
            source[i * 4 + 3].write(alpha);
        }
        let mut rect = [0; 4];
        let mut output = [MaybeUninit::<u8>::uninit(); 64];
        // SAFETY: alpha bytes are initialized, every source/output byte is
        // allocated, and all regions are disjoint and exclusive.
        unsafe {
            assert_eq!(
                xy_vsfilter_vobsub_bounds(source.as_ptr().cast(), 2, 2, rect.as_mut_ptr()),
                1
            );
            assert_eq!(rect, [0, 0, 1, 1]);
            assert_eq!(
                xy_vsfilter_vobsub_crop(
                    source.as_ptr().cast(),
                    2,
                    2,
                    2,
                    output.as_mut_ptr().cast(),
                    ptr::null(),
                    0
                ),
                1
            );
            for i in 0..16 {
                let x = i % 4;
                let y = i / 4;
                if x == 0 || x == 3 || y == 0 || y == 3 {
                    for byte in &output[i * 4..i * 4 + 4] {
                        assert_eq!(byte.assume_init(), 0);
                    }
                } else {
                    assert_eq!(
                        output[i * 4 + 3].assume_init(),
                        [0, 7, 9, 0][(y - 1) * 2 + x - 1]
                    );
                }
            }
            // RGB bytes inside the copy remain uninitialized and are never read.
            assert_eq!(
                xy_vsfilter_vobsub_crop(
                    source.as_ptr().cast(),
                    2,
                    2,
                    2,
                    output.as_mut_ptr().cast(),
                    output.as_ptr().cast(),
                    64
                ),
                0
            );
        }
    }
}
