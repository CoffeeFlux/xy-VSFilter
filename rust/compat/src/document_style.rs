// SPDX-License-Identifier: GPL-2.0-or-later
//! Supplied, initialized STSStyle values; not authoritative storage or native defaults.
//! No native object layout, CString/FwRect handle, or Unicode interpretation.
//!
//! V1 LE header (192 bytes): 0..20 five i32s (charset, weight, alignment,
//! border style, relative-to); 20..36 four i32 margins (left, top, right, bottom);
//! 36..52 four u32 colors; 52..56 four alpha bytes; 56..59 italic/underline/
//! strikeout bool bytes; 59..64 reserved zero; 64..68 u32 font-name unit count;
//! 68..72 reserved zero; 72..192 fifteen f64 bit patterns in `floats()` order.
//! Raw u16 font-name units follow. Count must fit native INT_MAX, total length
//! must fit usize/isize, and input must have exactly that length. Allocation
//! failure is outside this codec's domain. Native comparison is separate from
//! bit-preserving encoding. Windows adapter differential validation is pending.
#![forbid(unsafe_code)]
use alloc::vec::Vec;

pub const HEADER_BYTES: usize = 192;
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    InvalidShape,
    InvalidBool,
    NonzeroReserved,
    CountOverflow,
    LengthOverflow,
}

#[derive(Clone, Debug)]
pub struct StyleValue {
    pub char_set: i32,
    pub font_name: Vec<u16>,
    pub font_size: f64,
    pub font_weight: i32,
    pub italic: bool,
    pub underline: bool,
    pub strikeout: bool,
    pub margins: [i32; 4],
    pub screen_alignment: i32,
    pub border_style: i32,
    pub outline_width_x: f64,
    pub outline_width_y: f64,
    pub shadow_depth_x: f64,
    pub shadow_depth_y: f64,
    pub colors: [u32; 4],
    pub alpha: [u8; 4],
    pub font_scale_x: f64,
    pub font_scale_y: f64,
    pub font_spacing: f64,
    pub blur: f64,
    pub gaussian_blur: f64,
    pub font_angle_z: f64,
    pub font_angle_x: f64,
    pub font_angle_y: f64,
    pub font_shift_x: f64,
    pub font_shift_y: f64,
    pub relative_to: i32,
}
impl StyleValue {
    fn ints(&self) -> [i32; 9] {
        [
            self.char_set,
            self.font_weight,
            self.screen_alignment,
            self.border_style,
            self.relative_to,
            self.margins[0],
            self.margins[1],
            self.margins[2],
            self.margins[3],
        ]
    }
    /// Header order, independent of native field offsets.
    fn floats(&self) -> [f64; 15] {
        [
            self.font_size,
            self.outline_width_x,
            self.outline_width_y,
            self.shadow_depth_x,
            self.shadow_depth_y,
            self.font_scale_x,
            self.font_scale_y,
            self.font_spacing,
            self.blur,
            self.gaussian_blur,
            self.font_angle_z,
            self.font_angle_x,
            self.font_angle_y,
            self.font_shift_x,
            self.font_shift_y,
        ]
    }
    /// Native STSStyle equality: first-NUL CString font comparison and numeric
    /// double equality (NaN unequal; positive and negative zero equal).
    /// Floating-point status flags and enabled-trap behavior are not claimed.
    pub fn native_compatible_eq(&self, other: &Self) -> bool {
        fn prefix(v: &[u16]) -> &[u16] {
            &v[..v.iter().position(|&x| x == 0).unwrap_or(v.len())]
        }
        // Pinned operator== followed by IsFontStyleEqual short-circuit order.
        self.margins[0] == other.margins[0]
            && self.margins[1] == other.margins[1]
            && self.margins[2] == other.margins[2]
            && self.margins[3] == other.margins[3]
            && self.screen_alignment == other.screen_alignment
            && self.border_style == other.border_style
            && self.outline_width_x == other.outline_width_x
            && self.outline_width_y == other.outline_width_y
            && self.shadow_depth_x == other.shadow_depth_x
            && self.shadow_depth_y == other.shadow_depth_y
            && self.colors[0] == other.colors[0]
            && self.colors[1] == other.colors[1]
            && self.colors[2] == other.colors[2]
            && self.colors[3] == other.colors[3]
            && self.alpha[0] == other.alpha[0]
            && self.alpha[1] == other.alpha[1]
            && self.alpha[2] == other.alpha[2]
            && self.alpha[3] == other.alpha[3]
            && self.blur == other.blur
            && self.gaussian_blur == other.gaussian_blur
            && self.relative_to == other.relative_to
            && self.char_set == other.char_set
            && prefix(&self.font_name) == prefix(&other.font_name)
            && self.font_size == other.font_size
            && self.font_scale_x == other.font_scale_x
            && self.font_scale_y == other.font_scale_y
            && self.font_spacing == other.font_spacing
            && self.font_weight == other.font_weight
            && self.italic == other.italic
            && self.underline == other.underline
            && self.strikeout == other.strikeout
            && self.font_angle_z == other.font_angle_z
            && self.font_angle_x == other.font_angle_x
            && self.font_angle_y == other.font_angle_y
            && self.font_shift_x == other.font_shift_x
            && self.font_shift_y == other.font_shift_y
    }
}
fn total(count: usize) -> Result<usize, Error> {
    if count > i32::MAX as usize {
        return Err(Error::CountOverflow);
    }
    let size = count
        .checked_mul(2)
        .and_then(|n| n.checked_add(HEADER_BYTES))
        .ok_or(Error::LengthOverflow)?;
    if size > isize::MAX as usize {
        return Err(Error::LengthOverflow);
    }
    Ok(size)
}
pub fn encode(value: &StyleValue) -> Result<Vec<u8>, Error> {
    let length = total(value.font_name.len())?;
    let mut bytes = alloc::vec![0;length];
    for (i, v) in value.ints().iter().enumerate() {
        bytes[i * 4..i * 4 + 4].copy_from_slice(&v.to_le_bytes());
    }
    for (i, v) in value.colors.iter().enumerate() {
        bytes[36 + i * 4..40 + i * 4].copy_from_slice(&v.to_le_bytes());
    }
    bytes[52..56].copy_from_slice(&value.alpha);
    bytes[56] = u8::from(value.italic);
    bytes[57] = u8::from(value.underline);
    bytes[58] = u8::from(value.strikeout);
    bytes[64..68].copy_from_slice(&(value.font_name.len() as u32).to_le_bytes());
    for (i, v) in value.floats().iter().enumerate() {
        bytes[72 + i * 8..80 + i * 8].copy_from_slice(&v.to_bits().to_le_bytes());
    }
    for (i, v) in value.font_name.iter().enumerate() {
        bytes[HEADER_BYTES + i * 2..HEADER_BYTES + i * 2 + 2].copy_from_slice(&v.to_le_bytes());
    }
    Ok(bytes)
}
pub fn decode(bytes: &[u8]) -> Result<StyleValue, Error> {
    if bytes.len() < HEADER_BYTES {
        return Err(Error::InvalidShape);
    }
    if bytes[59..64].iter().chain(&bytes[68..72]).any(|&v| v != 0) {
        return Err(Error::NonzeroReserved);
    }
    if bytes[56..59].iter().any(|&v| v > 1) {
        return Err(Error::InvalidBool);
    }
    let count =
        u32::from_le_bytes(bytes[64..68].try_into().map_err(|_| Error::InvalidShape)?) as usize;
    if bytes.len() != total(count)? {
        return Err(Error::InvalidShape);
    }
    let mut ints = [0i32; 9];
    for (i, v) in ints.iter_mut().enumerate() {
        *v = i32::from_le_bytes(
            bytes[i * 4..i * 4 + 4]
                .try_into()
                .map_err(|_| Error::InvalidShape)?,
        );
    }
    let mut colors = [0u32; 4];
    for (i, v) in colors.iter_mut().enumerate() {
        *v = u32::from_le_bytes(
            bytes[36 + i * 4..40 + i * 4]
                .try_into()
                .map_err(|_| Error::InvalidShape)?,
        );
    }
    let mut f = [0f64; 15];
    for (i, v) in f.iter_mut().enumerate() {
        *v = f64::from_bits(u64::from_le_bytes(
            bytes[72 + i * 8..80 + i * 8]
                .try_into()
                .map_err(|_| Error::InvalidShape)?,
        ));
    }
    let font_name = bytes[HEADER_BYTES..]
        .chunks_exact(2)
        .map(|v| u16::from_le_bytes([v[0], v[1]]))
        .collect();
    Ok(StyleValue {
        char_set: ints[0],
        font_weight: ints[1],
        screen_alignment: ints[2],
        border_style: ints[3],
        relative_to: ints[4],
        margins: [ints[5], ints[6], ints[7], ints[8]],
        colors,
        alpha: bytes[52..56].try_into().map_err(|_| Error::InvalidShape)?,
        italic: bytes[56] != 0,
        underline: bytes[57] != 0,
        strikeout: bytes[58] != 0,
        font_name,
        font_size: f[0],
        outline_width_x: f[1],
        outline_width_y: f[2],
        shadow_depth_x: f[3],
        shadow_depth_y: f[4],
        font_scale_x: f[5],
        font_scale_y: f[6],
        font_spacing: f[7],
        blur: f[8],
        gaussian_blur: f[9],
        font_angle_z: f[10],
        font_angle_x: f[11],
        font_angle_y: f[12],
        font_shift_x: f[13],
        font_shift_y: f[14],
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    // Synthetic initialized transport fixture, deliberately not native defaults.
    fn fixture() -> StyleValue {
        StyleValue {
            char_set: i32::MIN,
            font_name: alloc::vec![0xd800, 0, 0xdcff, 0xffff],
            font_size: -0.0,
            font_weight: i32::MAX,
            italic: true,
            underline: false,
            strikeout: true,
            margins: [-1, 2, i32::MIN, i32::MAX],
            screen_alignment: -7,
            border_style: 5,
            outline_width_x: 1.5,
            outline_width_y: -2.5,
            shadow_depth_x: 3.5,
            shadow_depth_y: 4.5,
            colors: [0, u32::MAX, 0x12345678, 9],
            alpha: [0, 1, 128, 255],
            font_scale_x: 5.5,
            font_scale_y: 6.5,
            font_spacing: 7.5,
            blur: 8.5,
            gaussian_blur: 9.5,
            font_angle_z: 10.5,
            font_angle_x: 11.5,
            font_angle_y: 12.5,
            font_shift_x: 13.5,
            font_shift_y: f64::from_bits(0x7ff8000000000042),
            relative_to: -10,
        }
    }
    #[test]
    fn complete_roundtrip_and_offsets() {
        let v = fixture();
        let b = encode(&v).unwrap();
        assert_eq!(b.len(), 200);
        assert_eq!(&b[..4], &i32::MIN.to_le_bytes());
        assert_eq!(&b[36..40], &0u32.to_le_bytes());
        assert_eq!(&b[52..59], &[0, 1, 128, 255, 1, 0, 1]);
        assert_eq!(&b[64..68], &4u32.to_le_bytes());
        assert_eq!(&b[72..80], &(-0.0f64).to_bits().to_le_bytes());
        assert_eq!(&b[184..192], &0x7ff8000000000042u64.to_le_bytes());
        assert_eq!(&b[192..], &[0, 216, 0, 0, 255, 220, 255, 255]);
        let decoded = decode(&b).unwrap();
        assert_eq!(encode(&decoded).unwrap(), b);
        assert_eq!(decoded.font_name, v.font_name);
    }
    #[test]
    fn rejects_all_truncations_and_extra_input() {
        let b = encode(&fixture()).unwrap();
        for n in 0..b.len() {
            assert!(decode(&b[..n]).is_err(), "prefix {n}");
        }
        let mut extra = b;
        extra.push(0);
        assert!(matches!(decode(&extra), Err(Error::InvalidShape)));
    }
    #[test]
    fn rejects_shape_bool_reserved_and_count() {
        let b = encode(&fixture()).unwrap();
        for index in [56, 57, 58] {
            let mut x = b.clone();
            x[index] = 2;
            assert!(matches!(decode(&x), Err(Error::InvalidBool)));
        }
        for index in (59..64).chain(68..72) {
            let mut x = b.clone();
            x[index] = 1;
            assert!(matches!(decode(&x), Err(Error::NonzeroReserved)));
        }
        let mut x = b;
        x[64..68].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(decode(&x), Err(Error::CountOverflow)));
        assert_eq!(total(usize::MAX), Err(Error::CountOverflow));
        #[cfg(target_pointer_width = "32")]
        {
            assert_eq!(total(i32::MAX as usize), Err(Error::LengthOverflow));
        }
        #[cfg(target_pointer_width = "64")]
        {
            assert_eq!(total(i32::MAX as usize), Ok(4_294_967_486));
        }
    }
    #[test]
    fn native_equality_is_not_bit_or_raw_string_equality() {
        let mut a = fixture();
        a.font_shift_y = 14.5;
        let mut b = a.clone();
        b.font_size = 0.0;
        b.font_name = alloc::vec![0xd800, 0, 65];
        assert!(a.native_compatible_eq(&b));
        assert_ne!(encode(&a).unwrap(), encode(&b).unwrap());
        b.font_name[0] = 0xdc00;
        assert!(!a.native_compatible_eq(&b));
        b = a.clone();
        b.font_size = f64::NAN;
        assert!(!b.native_compatible_eq(&b));
        b = a.clone();
        b.colors[3] ^= 1;
        assert!(!a.native_compatible_eq(&b));
    }
    #[test]
    fn exceptional_bits_roundtrip_every_slot_without_float_comparison() {
        for bits in [
            0x7ff0000000000001u64,
            0xfff0000000000001,
            0x7ff0000000000000,
            0xfff0000000000000,
            1,
            0x8000000000000001,
        ] {
            for slot in 0..15 {
                let mut wire = encode(&fixture()).unwrap();
                wire[72 + slot * 8..80 + slot * 8].copy_from_slice(&bits.to_le_bytes());
                assert_eq!(encode(&decode(&wire).unwrap()).unwrap(), wire);
            }
        }
    }
    #[test]
    fn every_field_participates_in_native_equality() {
        let mut original = fixture();
        original.font_shift_y = 14.5;
        assert!(original.native_compatible_eq(&original));
        macro_rules! different {
            ($field:ident, $value:expr) => {{
                let mut changed = original.clone();
                changed.$field = $value;
                assert!(!original.native_compatible_eq(&changed), stringify!($field));
            }};
        }
        different!(char_set, original.char_set.wrapping_add(1));
        different!(font_weight, original.font_weight.wrapping_add(1));
        different!(screen_alignment, original.screen_alignment.wrapping_add(1));
        different!(border_style, original.border_style.wrapping_add(1));
        different!(relative_to, original.relative_to.wrapping_add(1));
        different!(font_size, original.font_size + 1.0);
        different!(outline_width_x, original.outline_width_x + 1.0);
        different!(outline_width_y, original.outline_width_y + 1.0);
        different!(shadow_depth_x, original.shadow_depth_x + 1.0);
        different!(shadow_depth_y, original.shadow_depth_y + 1.0);
        different!(font_scale_x, original.font_scale_x + 1.0);
        different!(font_scale_y, original.font_scale_y + 1.0);
        different!(font_spacing, original.font_spacing + 1.0);
        different!(blur, original.blur + 1.0);
        different!(gaussian_blur, original.gaussian_blur + 1.0);
        different!(font_angle_z, original.font_angle_z + 1.0);
        different!(font_angle_x, original.font_angle_x + 1.0);
        different!(font_angle_y, original.font_angle_y + 1.0);
        different!(font_shift_x, original.font_shift_x + 1.0);
        different!(font_shift_y, original.font_shift_y + 1.0);
        different!(italic, !original.italic);
        different!(underline, !original.underline);
        different!(strikeout, !original.strikeout);
        different!(font_name, alloc::vec![65]);
        for i in 0..4 {
            let mut changed = original.clone();
            changed.margins[i] = changed.margins[i].wrapping_add(1);
            assert!(!original.native_compatible_eq(&changed), "margins {i}");
        }
        for i in 0..4 {
            let mut changed = original.clone();
            changed.colors[i] = changed.colors[i].wrapping_add(1);
            assert!(!original.native_compatible_eq(&changed), "colors {i}");
        }
        for i in 0..4 {
            let mut changed = original.clone();
            changed.alpha[i] = changed.alpha[i].wrapping_add(1);
            assert!(!original.native_compatible_eq(&changed), "alpha {i}");
        }
    }
}
