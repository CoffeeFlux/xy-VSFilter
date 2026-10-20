// SPDX-License-Identifier: GPL-2.0-or-later
//! Detached initialized-word observer entry. The production image operation
//! uses the same safe filter directly, without lending native float storage.
/// # Safety
/// input lends length initialized bytes: width,height,stride,coefficient count
/// (u32 LE), coefficients, then stride*height words including the prefix. Output
/// lends capacity bytes, may overlap input after decoding, and receives words.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_gaussian_filter_v1(
    input: *const u8,
    length: usize,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    if input.is_null()
        || output.is_null()
        || length < 16
        || length > isize::MAX as usize
        || capacity > isize::MAX as usize
    {
        return 3;
    }
    let compute = || -> Result<alloc::vec::Vec<u8>, u32> {
        // SAFETY: checked length and caller's initialized, immutable packet.
        let input = unsafe { core::slice::from_raw_parts(input, length) };
        let header: [usize; 4] = core::array::from_fn(|i| {
            u32::from_le_bytes(input[i * 4..i * 4 + 4].try_into().unwrap()) as usize
        });
        let [width, height, stride, m] = header;
        let count = stride.checked_mul(height).ok_or(6u32)?;
        let size = count.checked_mul(4).ok_or(6u32)?;
        if m.checked_mul(4)
            .and_then(|n| n.checked_add(size))
            .and_then(|n| n.checked_add(16))
            != Some(length)
        {
            return Err(6);
        }
        if size > capacity {
            return Err(4);
        }
        let mut all = alloc::vec::Vec::new();
        all.try_reserve_exact(m + count).map_err(|_| 5u32)?;
        all.extend(
            input[16..]
                .chunks_exact(4)
                .map(|v| u32::from_le_bytes(v.try_into().unwrap())),
        );
        let (coefficients, words) = all.split_at_mut(m);
        xy_vsfilter_compat::gaussian_filter::filter(
            words,
            width,
            height,
            stride,
            coefficients,
            &mut crate::windows_gaussian_filter::Numeric::new(),
        )?;
        let mut result = alloc::vec::Vec::new();
        result.try_reserve_exact(size).map_err(|_| 5u32)?;
        for word in words {
            result.extend_from_slice(&word.to_le_bytes());
        }
        Ok(result)
    };
    let result = match compute() {
        Ok(v) => v,
        Err(e) => return e,
    };
    // SAFETY: checked caller capacity, detached result, input borrow has ended.
    unsafe {
        core::ptr::copy_nonoverlapping(result.as_ptr(), output, result.len());
    }
    0
}
