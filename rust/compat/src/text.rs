// SPDX-License-Identifier: GPL-2.0-or-later
// Derived from CTextFile::NextChar, Copyright (C) 2003-2006 Gabest,
// and include/Utf8.h, Copyright (C) 2010 Francois-R. Boyer.
//! Raw code-unit decoding compatible with `CTextFile::NextChar`.
//!
//! In particular, UTF-8 continuation bytes are masked, NOT validated. Surrogates,
//! overlong sequences with accepted lead bytes, and values above U+10FFFF must
//! survive this layer. `CTextFile::ReadLine` decides what to do with the result.
//! Using `str::from_utf8`, `char`, or lossy Unicode conversion changes behavior.

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Encoding {
    Default = 0,
    Utf8 = 1,
    LittleEndian16 = 2,
    BigEndian16 = 3,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Decoded {
    /// Raw code unit/code point, or one of the legacy negative status codes.
    pub value: i32,
    pub consumed: u32,
}

pub const NEED_MORE: i32 = -1;
pub const REOPEN: i32 = -2;

/// Decode one value, preserving both the legacy result and cursor movement.
/// Empty input safely requests more data (the original caller never calls the
/// decoder with an empty buffer).
pub fn next_char(bytes: &[u8], encoding: Encoding) -> Decoded {
    let Some(&first) = bytes.first() else {
        return Decoded {
            value: NEED_MORE,
            consumed: 0,
        };
    };
    let (value, consumed) = match encoding {
        Encoding::Default => (i32::from(first), 1),
        Encoding::Utf8 => {
            if first < 0x80 {
                (i32::from(first), 1)
            } else if !(0xc2..0xf5).contains(&first) {
                (REOPEN, 1)
            } else {
                let length = if first < 0xe0 {
                    2
                } else if first < 0xf0 {
                    3
                } else {
                    4
                };
                if bytes.len() < length {
                    (NEED_MORE, 0)
                } else {
                    let mask = match length {
                        2 => 0x1f,
                        3 => 0x0f,
                        _ => 0x07,
                    };
                    let mut value = i32::from(first & mask);
                    for &byte in &bytes[1..length] {
                        value = (value << 6) | i32::from(byte & 0x3f);
                    }
                    (value, length as u32)
                }
            }
        }
        Encoding::LittleEndian16 | Encoding::BigEndian16 => {
            if let Some(&second) = bytes.get(1) {
                let pair = [first, second];
                let value = if encoding == Encoding::LittleEndian16 {
                    u16::from_le_bytes(pair)
                } else {
                    u16::from_be_bytes(pair)
                };
                (i32::from(value), 2)
            } else {
                (NEED_MORE, 0)
            }
        }
    };
    Decoded { value, consumed }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn preserves_malformed_utf8_instead_of_normalizing_it() {
        for (bytes, value) in [
            (&[0xc2, 0x41][..], 0x81),         // ASCII byte used as a continuation
            (&[0xe0, 0x80, 0x80][..], 0),      // overlong NUL
            (&[0xed, 0xa0, 0x80][..], 0xd800), // unpaired surrogate
            (&[0xf4, 0xbf, 0xbf, 0xbf][..], 0x13ffff), // beyond Unicode
        ] {
            assert_eq!(
                next_char(bytes, Encoding::Utf8),
                Decoded {
                    value,
                    consumed: bytes.len() as u32,
                }
            );
        }
    }

    #[test]
    fn rejection_and_refill_have_different_cursor_semantics() {
        assert_eq!(
            next_char(&[0xc0, 0x80], Encoding::Utf8),
            Decoded {
                value: REOPEN,
                consumed: 1
            }
        );
        assert_eq!(
            next_char(&[0xf5, 0x80, 0x80, 0x80], Encoding::Utf8),
            Decoded {
                value: REOPEN,
                consumed: 1
            }
        );
        for bytes in [
            &[][..],
            &[0xf0][..],
            &[0xf0, 0x41][..],
            &[0xf0, 0x41, 0x42][..],
        ] {
            assert_eq!(
                next_char(bytes, Encoding::Utf8),
                Decoded {
                    value: NEED_MORE,
                    consumed: 0
                }
            );
        }
    }

    #[test]
    fn utf16_is_raw_units_and_default_is_raw_bytes() {
        assert_eq!(
            next_char(&[0, 0xd8], Encoding::LittleEndian16).value,
            0xd800
        );
        assert_eq!(next_char(&[0xd8, 0], Encoding::BigEndian16).value, 0xd800);
        for byte in 0..=255 {
            assert_eq!(
                next_char(&[byte], Encoding::Default),
                Decoded {
                    value: i32::from(byte),
                    consumed: 1
                }
            );
            for encoding in [Encoding::LittleEndian16, Encoding::BigEndian16] {
                assert_eq!(
                    next_char(&[byte], encoding),
                    Decoded {
                        value: NEED_MORE,
                        consumed: 0
                    }
                );
            }
        }
    }
}
