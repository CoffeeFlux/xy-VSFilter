// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::{boxed::Box, vec::Vec};
/// # Safety
/// Each nonempty pointer/length pair spans initialized aligned u16 values,
/// immutable during this call; each byte size fits isize. Loans may alias one
/// another but no loan escapes. The result owns an independent allocation.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_case_replace_create(
    source: *const u16,
    source_len: u32,
    lowered: *const u16,
    lowered_len: u32,
    from: *const u16,
    from_len: u32,
    to: *const u16,
    to_len: u32,
) -> *mut Vec<u16> {
    let mut spans = [&[][..]; 4];
    for (i, (pointer, length)) in [
        (source, source_len),
        (lowered, lowered_len),
        (from, from_len),
        (to, to_len),
    ]
    .into_iter()
    .enumerate()
    {
        if length != 0 {
            // SAFETY: each native span is initialized, immutable and bounded.
            spans[i] = unsafe { core::slice::from_raw_parts(pointer, length as usize) };
        }
    }
    xy_vsfilter_compat::case_replace::replace(spans[0], spans[1], spans[2], spans[3])
        .map_or(core::ptr::null_mut(), |text| Box::into_raw(Box::new(text)))
}
/// # Safety
/// owner is live and immutable during this scalar query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_case_replace_field(
    owner: *const Vec<u16>,
    field: u32,
    index: u32,
) -> u32 {
    // SAFETY: adapter retains the owner until after all queries.
    let text = unsafe { &*owner };
    match field {
        0 => text.len() as u32,
        1 => u32::from(text.get(index as usize).copied().unwrap_or(0)),
        _ => 0,
    }
}
/// # Safety
/// Return the unique create result exactly once after all calls finish.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_case_replace_release(owner: *mut Vec<u16>) {
    if !owner.is_null() {
        // SAFETY: noncopyable adapter returns unique ownership.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn aliased_loans_end_before_result_access() {
        let mut input = [97u16, 97];
        // SAFETY: overlapping immutable input spans, followed by mutation only
        // after the call returns; owned result queried and released once.
        unsafe {
            let owner = xy_vsfilter_case_replace_create(
                input.as_ptr(),
                2,
                input.as_ptr(),
                2,
                input.as_ptr(),
                1,
                input.as_ptr(),
                2,
            );
            assert!(!owner.is_null());
            input.fill(0);
            assert_eq!(xy_vsfilter_case_replace_field(owner, 0, 0), 4);
            assert_eq!(xy_vsfilter_case_replace_field(owner, 1, 3), 97);
            assert_eq!(xy_vsfilter_case_replace_field(owner, 1, u32::MAX), 0);
            xy_vsfilter_case_replace_release(owner);
        }
    }
}
