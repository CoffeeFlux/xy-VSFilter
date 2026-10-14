// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::text_writer::Writer;
/// # Safety
/// Nonempty input spans length initialized aligned u16 values, immutable during
/// this call; length*2 fits isize. Native caller has applied CString termination.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_writer_create(
    input: *const u16,
    length: u32,
    big_endian: u32,
) -> *mut Writer {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: initialized native unit copy, copied before returning.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    Box::into_raw(Box::new(Writer::new(input, big_endian != 0)))
}
/// # Safety
/// owner is live and immutable throughout this query.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_writer_field(
    owner: *const Writer,
    field: u32,
    index: u32,
) -> u32 {
    // SAFETY: query precedes release and cannot overlap mutation.
    let state = unsafe { &*owner };
    match field {
        0 => state.text().len() as u32,
        1 => u32::from(state.text().get(index as usize).copied().unwrap_or(0)),
        2 => state.bytes().len() as u32,
        _ => 0,
    }
}
/// # Safety
/// owner is live. Returned bytes are immutable and valid only until release;
/// consume only the length from field 2. Never retain them after native Write.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_writer_data(owner: *const Writer) -> *const u8 {
    // SAFETY: owner holds a fixed allocation and exposes no mutable operations.
    let state = unsafe { &*owner };
    state.bytes().as_ptr()
}
/// # Safety
/// Release the unique create result once, after queries and native Write finish.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_writer_release(owner: *mut Writer) {
    if !owner.is_null() {
        // SAFETY: unique owner is transferred back after all native loans end.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn write_bytes_outlive_source_but_not_the_unique_owner() {
        let mut input = [65u16, 10, 0xd800];
        // SAFETY: initialized source, fixed immutable output, release after loan.
        unsafe {
            let owner = xy_vsfilter_writer_create(input.as_ptr(), 3, 1);
            input.fill(7);
            let count = xy_vsfilter_writer_field(owner, 2, 0) as usize;
            let bytes = core::slice::from_raw_parts(xy_vsfilter_writer_data(owner), count);
            assert_eq!(bytes, &[0, 65, 0, 13, 0, 10, 0xd8, 0]);
            assert_eq!(xy_vsfilter_writer_field(owner, 0, 0), 4);
            assert_eq!(xy_vsfilter_writer_field(owner, 1, 3), 0xd800);
            assert_eq!(xy_vsfilter_writer_field(owner, 1, u32::MAX), 0);
            xy_vsfilter_writer_release(owner);
        }
    }
}
