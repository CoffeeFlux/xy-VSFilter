// SPDX-License-Identifier: GPL-2.0-or-later
//! Internal C ABI. No pointers, allocations, callbacks, or shared ownership.
#![deny(unsafe_op_in_unsafe_fn)]

use xy_vsfilter_compat::text::{next_char, Decoded, Encoding, REOPEN};

/// Bytes are packed little-endian independently of the host's byte order.
/// The low 32 result bits contain the signed value; the high 32 contain consumed.
/// At most four bytes are needed, even when the C++ buffer holds more.
///
/// SAFETY of the export: the symbol is private to our native bridge and has the
/// signature declared in include/xy_rust_text.h. Every possible argument is safe.
#[no_mangle]
pub extern "C" fn xy_vsfilter_next_char(packed: u32, available: u32, encoding: u32) -> u64 {
    let encoding = match encoding {
        0 => Some(Encoding::Default),
        1 => Some(Encoding::Utf8),
        2 => Some(Encoding::LittleEndian16),
        3 => Some(Encoding::BigEndian16),
        _ => None,
    };
    let bytes = packed.to_le_bytes();
    let result = match encoding {
        Some(encoding) => next_char(&bytes[..available.min(4) as usize], encoding),
        None => Decoded {
            value: REOPEN,
            consumed: 0,
        },
    };
    u64::from(result.value as u32) | (u64::from(result.consumed) << 32)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn abi_status_and_consumption() {
        assert_eq!(
            xy_vsfilter_next_char(0xc0, 1, 1),
            (1u64 << 32) | 0xffff_fffe
        );
        assert_eq!(xy_vsfilter_next_char(0xf0, 1, 1), 0xffff_ffff);
        assert_eq!(xy_vsfilter_next_char(0, u32::MAX, u32::MAX), 0xffff_fffe);
        assert_eq!(
            xy_vsfilter_next_char(0x4142, u32::MAX, 2),
            (2u64 << 32) | 0x4142
        );
        assert_eq!(xy_vsfilter_next_char(0, 0, 0), 0xffff_ffff);
    }
}
