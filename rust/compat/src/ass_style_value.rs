// SPDX-License-Identifier: GPL-2.0-or-later
//! Safe assembly of an ASS style from ordered native CRT conversion results.
//! Defaults and normalization are from cleanup STSStyle::SetDefault and
//! OpenSubStationAlpha. Raw code units and floating bit patterns are retained.
use crate::document_style::StyleValue;
use alloc::vec::Vec;

pub struct Builder {
    pub value: StyleValue,
    pub name: Vec<u16>,
    pub alpha: i32,
    pub encoding: i32,
}
impl Default for Builder {
    fn default() -> Self {
        Self {
            name: Vec::new(),
            alpha: 0,
            encoding: 0,
            value: StyleValue {
                rectangle_owner: None,
                char_set: 1,
                font_name: alloc::vec![65, 114, 105, 97, 108],
                font_size: 18.0,
                font_weight: 700,
                italic: false,
                underline: false,
                strikeout: false,
                margins: [20; 4],
                screen_alignment: 2,
                border_style: 0,
                outline_width_x: 2.0,
                outline_width_y: 2.0,
                shadow_depth_x: 3.0,
                shadow_depth_y: 3.0,
                colors: [0xffffff, 0xffff, 0, 0],
                alpha: [0, 0, 0, 0x80],
                font_scale_x: 100.0,
                font_scale_y: 100.0,
                font_spacing: 0.0,
                blur: 0.0,
                gaussian_blur: 0.0,
                font_angle_z: 0.0,
                font_angle_x: 0.0,
                font_angle_y: 0.0,
                font_shift_x: 0.0,
                font_shift_y: 0.0,
                relative_to: 2,
            },
        }
    }
}
impl Builder {
    pub fn integer(&mut self, field: u32, number: i32) -> bool {
        let value = &mut self.value;
        match field {
            4..=7 => value.colors[(field - 4) as usize] = number as u32,
            8 => value.font_weight = if number != 0 { 700 } else { 400 },
            9 => value.italic = number != 0,
            10 => value.underline = number != 0,
            11 => value.strikeout = number != 0,
            16 => value.border_style = number,
            19 => value.screen_alignment = number,
            20 => value.margins[0] = number,
            21 => value.margins[2] = number,
            22 => {
                value.margins[1] = number;
                value.margins[3] = number;
            }
            23 => value.margins[3] = number,
            24 => self.alpha = number,
            25 => self.encoding = number,
            26 => value.relative_to = number,
            _ => return false,
        }
        true
    }
    pub fn float(&mut self, field: u32, number: f64) -> bool {
        let value = &mut self.value;
        match field {
            3 => value.font_size = number,
            12 => value.font_scale_x = number,
            13 => value.font_scale_y = number,
            14 => value.font_spacing = number,
            15 => value.font_angle_z = number,
            17 => {
                value.outline_width_x = number;
                value.outline_width_y = number;
            }
            18 => {
                value.shadow_depth_x = number;
                value.shadow_depth_y = number;
            }
            _ => return false,
        }
        true
    }
    pub fn text(&mut self, field: u32, input: &[u16]) -> bool {
        match field {
            1 => self.name = input.to_vec(),
            2 => self.value.font_name = input.to_vec(),
            _ => return false,
        }
        true
    }
    pub fn normalize(&mut self, version: i32, numbers: &[u32; 11]) {
        let value = &mut self.value;
        value.colors.copy_from_slice(&numbers[..4]);
        for (alpha, &number) in value.alpha.iter_mut().zip(&numbers[4..8]) {
            *alpha = number as u8;
        }
        value.char_set = numbers[8] as i32;
        value.border_style = numbers[9] as i32;
        value.screen_alignment = numbers[10] as i32;
        // Windows max(a,0) uses a > 0, which maps NaNs and negative zero to
        // positive zero. Rust f64::max/clamp are not substitutes for this.
        fn nonnegative(value: f64) -> f64 {
            if value > 0.0 {
                value
            } else {
                0.0
            }
        }
        if version >= 5 {
            value.font_scale_x = nonnegative(value.font_scale_x);
            value.font_scale_y = nonnegative(value.font_scale_y);
            value.font_spacing = nonnegative(value.font_spacing);
        }
        value.font_angle_x = 0.0;
        value.font_angle_y = 0.0;
        value.outline_width_x = nonnegative(value.outline_width_x);
        value.outline_width_y = nonnegative(value.outline_width_y);
        value.shadow_depth_x = nonnegative(value.shadow_depth_x);
        value.shadow_depth_y = nonnegative(value.shadow_depth_y);
        let stars = self.name.iter().take_while(|&&unit| unit == 42).count();
        self.name.drain(..stars);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn clamps_keep_native_nan_and_signed_zero_rules_without_clamping_size_or_angle() {
        for number in [
            f64::from_bits(0x7ff8000000000123),
            f64::NEG_INFINITY,
            -0.0,
            -1.0,
            0.0,
            1.0,
            f64::INFINITY,
        ] {
            let mut builder = Builder::default();
            for field in [3, 12, 13, 14, 15, 17, 18] {
                assert!(builder.float(field, number));
            }
            assert!(builder.text(1, &[42, 42, 0, 0xd800]));
            builder.normalize(5, &[1, 2, 3, 4, 5, 6, 7, 8, 1, 0, 2]);
            assert_eq!(builder.value.font_size.to_bits(), number.to_bits());
            assert_eq!(builder.value.font_angle_z.to_bits(), number.to_bits());
            let expected = if number > 0.0 { number.to_bits() } else { 0 };
            for value in [
                builder.value.font_scale_x,
                builder.value.font_scale_y,
                builder.value.font_spacing,
                builder.value.outline_width_x,
                builder.value.outline_width_y,
                builder.value.shadow_depth_x,
                builder.value.shadow_depth_y,
            ] {
                assert_eq!(value.to_bits(), expected);
            }
            assert_eq!(builder.name, [0, 0xd800]);
        }
    }
}
