// SPDX-License-Identifier: GPL-2.0-or-later
use xy_vsfilter_compat::fields::{next_field, string_field};

/// Return trimmed prefix in the low 32 bits and field length in the high 32.
/// UINT32_MAX as field length means throw after removing the trimmed prefix.
///
/// # Safety
/// For nonzero length, input and whitespace are readable, aligned arrays with
/// `length` entries, immutable throughout this call. Whitespace entries are
/// 0 or 1 from the native CRT. Native CString lengths cannot exceed i32::MAX.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_string_field(
    input: *const u16,
    whitespace: *const u8,
    length: u32,
    separator: u16,
) -> u64 {
    if length == 0 {
        return u64::from(u32::MAX) << 32;
    }
    if input.is_null() || whitespace.is_null() || u64::from(length) > isize::MAX as u64 / 2 {
        return u64::MAX;
    }
    // SAFETY: both arrays have the supplied length and remain immutable here.
    let input = unsafe { core::slice::from_raw_parts(input, length as usize) };
    let whitespace = unsafe { core::slice::from_raw_parts(whitespace, length as usize) };
    let field = string_field(input, whitespace, separator);
    let size = if field.start == input.len() {
        u32::MAX
    } else {
        (field.end - field.start) as u32
    };
    field.start as u64 | (u64::from(size) << 32)
}

/// Return start in the low 32 bits and end in the high 32 bits.
/// The input borrow ends on return; no allocation or pointer escapes.
///
/// # Safety
/// A nonempty input must be an aligned, readable array of `length` u16 units,
/// immutable throughout the call. A null pointer is permitted for length zero.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_next_field(
    input: *const u16,
    length: u32,
    separator: u16,
) -> u64 {
    let input = if length == 0 {
        &[]
    } else {
        if input.is_null() || u64::from(length) > isize::MAX as u64 / 2 {
            return u64::MAX;
        }
        // SAFETY: caller keeps the typed, immutable array live for this call.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    let field = next_field(input, separator);
    field.start as u64 | ((field.end as u64) << 32)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn borrowed_units_return_only_bounded_offsets() {
        let input = [32, 0xd800, 44, 0xffff];
        // SAFETY: the array lives through the call; empty inputs need no array.
        unsafe {
            assert_eq!(xy_vsfilter_next_field(input.as_ptr(), 4, 44), 1 | (2 << 32));
            assert_eq!(xy_vsfilter_next_field(core::ptr::null(), 0, 0), 0);
            assert_eq!(xy_vsfilter_next_field(core::ptr::null(), 4, 0), u64::MAX);
            assert_eq!(
                xy_vsfilter_string_field(input.as_ptr(), [1u8, 0, 0, 0].as_ptr(), 4, 44),
                1 | (1 << 32)
            );
            assert_eq!(
                xy_vsfilter_string_field(core::ptr::null(), core::ptr::null(), 0, 44),
                u64::from(u32::MAX) << 32
            );
        }
    }
}
