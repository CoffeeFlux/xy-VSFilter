// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::vec::Vec;
use xy_vsfilter_compat::gauss_integer::{blur, Kernel};
fn span(address: usize, length: usize) -> Option<(usize, usize)> {
    if length > isize::MAX as usize {
        return None;
    }
    Some((address, address.checked_add(length)?))
}
fn overlaps(a: (usize, usize), b: (usize, usize)) -> bool {
    a.0 < b.1 && b.0 < a.1
}

/// # Safety
/// Active image rows contain width initialized writable bytes. Scratch has
/// height*(width+1) writable words (initialization is not required). Each table
/// contains 256*kernel_width initialized words. Native buffers cover their
/// checked spans and are not accessed concurrently. Zero leaves them unchanged.
#[no_mangle]
#[allow(clippy::too_many_arguments)]
pub unsafe extern "C" fn xy_vsfilter_gauss_integer(
    buffer: *mut u8,
    scratch: *mut u32,
    width: i32,
    height: i32,
    stride: i32,
    x_table: *const u32,
    x_radius: i32,
    x_width: i32,
    y_table: *const u32,
    y_radius: i32,
    y_width: i32,
) -> u32 {
    fn geometry(
        width: i32,
        height: i32,
        stride: i32,
        rx: i32,
        gx: i32,
        ry: i32,
        gy: i32,
    ) -> Option<(usize, usize, usize, usize)> {
        if stride < 0
            || rx < 0
            || ry < 0
            || rx.checked_mul(2)?.checked_add(1)? != gx
            || ry.checked_mul(2)?.checked_add(1)? != gy
            || width < gx
            || height < gy
            || height < ry.checked_mul(3)?
        {
            return None;
        }
        let scratch_words = height.checked_mul(width.checked_add(1)?)? as usize;
        height.checked_mul(stride)?;
        let x_words = gx.checked_mul(256)? as usize;
        let y_words = gy.checked_mul(256)? as usize;
        let image_bytes = ((height - 1) as usize)
            .checked_mul(stride as usize)?
            .checked_add(width as usize)?;
        Some((scratch_words, x_words, y_words, image_bytes))
    }
    let Some((scratch_words, x_words, y_words, image_bytes)) =
        geometry(width, height, stride, x_radius, x_width, y_radius, y_width)
    else {
        return 0;
    };
    if (scratch as usize | x_table as usize | y_table as usize) & 3 != 0 {
        return 0;
    }
    let Some(image_span) = span(buffer as usize, image_bytes) else {
        return 0;
    };
    let Some(scratch_span) = scratch_words
        .checked_mul(4)
        .and_then(|n| span(scratch as usize, n))
    else {
        return 0;
    };
    let Some(x_span) = x_words
        .checked_mul(4)
        .and_then(|n| span(x_table as usize, n))
    else {
        return 0;
    };
    let Some(y_span) = y_words
        .checked_mul(4)
        .and_then(|n| span(y_table as usize, n))
    else {
        return 0;
    };
    if overlaps(image_span, scratch_span)
        || overlaps(image_span, x_span)
        || overlaps(image_span, y_span)
        || overlaps(scratch_span, x_span)
        || overlaps(scratch_span, y_span)
    {
        return 0;
    }
    let (width, height, stride) = (width as usize, height as usize, stride as usize);
    let mut input = Vec::new();
    if input.try_reserve_exact(width * height).is_err() {
        return 0;
    }
    for y in 0..height {
        // SAFETY: active initialized bytes only; rows may overlap, but their
        // shared borrows end before output begins. Padding is never borrowed.
        input.extend_from_slice(unsafe {
            core::slice::from_raw_parts(buffer.add(y * stride), width)
        });
    }
    // SAFETY: both coefficient tables are aligned and initialized. Shared table
    // aliases are permitted; both are disjoint from mutable native buffers.
    let (x_values, y_values) = unsafe {
        (
            core::slice::from_raw_parts(x_table, x_words),
            core::slice::from_raw_parts(y_table, y_words),
        )
    };
    let Some(x_kernel) = Kernel::new(x_radius, x_width, x_values) else {
        return 0;
    };
    let Some(y_kernel) = Kernel::new(y_radius, y_width, y_values) else {
        return 0;
    };
    let Some(output) = blur(&input, width, height, x_kernel, y_kernel) else {
        return 0;
    };
    // SAFETY: scratch is writable and disjoint from initialized Rust storage.
    // No reference to its previously uninitialized contents is created.
    unsafe {
        core::ptr::copy_nonoverlapping(output.scratch.as_ptr(), scratch, scratch_words);
    }
    for y in 0..height {
        // SAFETY: each active native output row is writable; preserve row order
        // when later rows overlap earlier output.
        unsafe {
            core::ptr::copy_nonoverlapping(
                output.pixels.as_ptr().add(y * width),
                buffer.add(y * stride),
                width,
            );
        }
    }
    1
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::mem::MaybeUninit;
    #[test]
    fn unsupported_lookup_state_declines_before_any_native_write() {
        let x_table = alloc::vec![0x8000_0000;3*256];
        let y_table = alloc::vec![0;3*256];
        let mut image = [1; 9];
        let mut scratch = [0xdead_beef; 12];
        // SAFETY: all bounded buffers are initialized and disjoint. This checks
        // only the Rust adapter's decline contract, not a native parity outcome.
        unsafe {
            assert_eq!(
                xy_vsfilter_gauss_integer(
                    image.as_mut_ptr(),
                    scratch.as_mut_ptr(),
                    3,
                    3,
                    3,
                    x_table.as_ptr(),
                    1,
                    3,
                    y_table.as_ptr(),
                    1,
                    3
                ),
                0
            );
        }
        assert_eq!(image, [1; 9]);
        assert_eq!(scratch, [0xdead_beef; 12]);
    }
    #[test]
    fn scratch_and_image_padding_need_no_initialized_borrow() {
        let table: Vec<u32> = (0..256).map(|value| value << 22).collect();
        let mut image = [MaybeUninit::<u8>::uninit(); 6];
        for (index, value) in [(0, 1), (1, 2), (3, 3), (4, 4)] {
            image[index].write(value);
        }
        let mut scratch = [MaybeUninit::<u32>::uninit(); 6];
        // SAFETY: active image rows and tables are initialized, all scratch
        // words writable, and all three allocations are mutually disjoint.
        unsafe {
            assert_eq!(
                xy_vsfilter_gauss_integer(
                    image.as_mut_ptr().cast(),
                    scratch.as_mut_ptr().cast(),
                    2,
                    2,
                    3,
                    table.as_ptr(),
                    0,
                    1,
                    table.as_ptr(),
                    0,
                    1
                ),
                1
            );
            for (index, value) in [(0, 0), (1, 0), (3, 0), (4, 4)] {
                assert_eq!(image[index].assume_init(), value);
            }
            assert_eq!(scratch[5].assume_init(), 1 << 21);
        }
    }
}
