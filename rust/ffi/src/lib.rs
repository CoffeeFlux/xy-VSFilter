// SPDX-License-Identifier: GPL-2.0-or-later
//! Internal C ABI. Scalar decoder calls and an isolated ASS lexer owner.
#![deny(unsafe_op_in_unsafe_fn)]
#![cfg_attr(all(feature = "native-runtime", not(test)), no_std)]

extern crate alloc;

mod ass_tags;
mod be_blur;
mod be_filter;
mod bilinear;
mod drawing;
mod drawing_tokens;
mod dvb;
mod fields;
mod fill;
mod golomb;
mod hdmv;
mod mask;
mod realtext;
#[cfg(all(windows, any(feature = "native-runtime", test)))]
mod runtime;
mod segment_insert;
mod segments;
mod subrip;
mod text_reader;
mod timeline_search;
mod webvtt;
#[cfg(all(feature = "native-runtime", not(windows)))]
compile_error!("native-runtime uses the Windows C runtime; omit it for portable tests");

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

/// Four raw UTF-16 units, packed into successive 16-bit lanes. The result is
/// three decoded bytes in the low 24 bits. The caller handles short final groups.
/// SAFETY of the export: private, value-only C ABI declared in xy_rust_text.h.
#[no_mangle]
pub extern "C" fn xy_vsfilter_decode_font_group(packed: u64) -> u32 {
    let input = [
        packed as u16,
        (packed >> 16) as u16,
        (packed >> 32) as u16,
        (packed >> 48) as u16,
    ];
    let [a, b, c] = xy_vsfilter_compat::font::decode_group(input);
    u32::from(a) | (u32::from(b) << 8) | (u32::from(c) << 16)
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

mod transpose;

mod byte_float;
mod cc;

mod gauss_integer;

mod vobsub_outline;
mod vobsub_packet;
mod vobsub_path;
mod vobsub_pixels;
mod vobsub_rle;
mod vobsub_scale;
mod vobsub_segment;
mod vobsub_trim;

mod find_char;

mod microdvd;

mod mpl2;

mod remove_tags;

mod case_replace;

mod sami;

mod encoding;

mod ass_dialogue;

mod ass_style;

mod ass_headers;

mod font_records;

mod microdvd_style;

mod sami_file;

mod subrip_cue;

mod pipe_line;

mod text_writer;
