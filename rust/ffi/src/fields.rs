// SPDX-License-Identifier: GPL-2.0-or-later
use xy_vsfilter_compat::fields::next_field;

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
        }
    }
}
