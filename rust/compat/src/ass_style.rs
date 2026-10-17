// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned ASS style remainder, version-dependent field order and integer
//! normalization. Experimental value assembly also lives here; native
//! GetStr/GetInt/GetFloat remain explicit conversion adapters.
use alloc::vec::Vec;
pub struct Style {
    remaining: Vec<u16>,
    version: i32,
    cursor: usize,
    normalized: [u32; 11],
    current: u32,
    supplied: u32,
    value: Option<crate::ass_style_value::Builder>,
    result: Vec<u8>,
}
impl Style {
    pub fn new(input: &[u16], version: i32) -> Self {
        Self {
            remaining: input.to_vec(),
            version,
            cursor: 0,
            normalized: [0; 11],
            current: 0,
            supplied: 0,
            value: None,
            result: Vec::new(),
        }
    }
    pub fn input(&self) -> &[u16] {
        &self.remaining
    }
    pub fn accept(&mut self, input: &[u16]) {
        self.remaining.clear();
        self.remaining.extend_from_slice(input);
    }
    /// Fields 1..26 follow the original parser; 27 assigns the completed margin
    /// rectangle before alpha/encoding callbacks. Zero signals completion.
    pub fn next_field(&mut self) -> u32 {
        const ORDER: [u32; 27] = [
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 27,
            24, 25, 26,
        ];
        while let Some(&field) = ORDER.get(self.cursor) {
            self.cursor += 1;
            let enabled = match field {
                10..=15 => self.version >= 5,
                16 => self.version >= 4,
                23 | 26 => self.version >= 6,
                24 => self.version <= 4,
                _ => true,
            };
            if enabled {
                self.current = field;
                return field;
            }
        }
        self.current = 0;
        0
    }
    pub fn normalize(
        &mut self,
        colors: [u32; 4],
        alpha: i32,
        encoding: i32,
        border: i32,
        alignment: i32,
    ) {
        self.normalized[..4].copy_from_slice(&colors);
        if self.version <= 4 {
            self.normalized[2] = colors[3];
            self.normalized[4..7].fill(alpha.clamp(0, 255) as u32);
            self.normalized[7] = 128;
        } else {
            for (index, color) in colors.into_iter().enumerate() {
                self.normalized[index] = color & 0xffffff;
                self.normalized[index + 4] = color >> 24;
            }
        }
        self.normalized[8] = if encoding < 0 { 1 } else { encoding as u32 };
        self.normalized[9] = u32::from(border == 3);
        self.normalized[10] = if self.version <= 4 {
            ((alignment & 3)
                + if alignment & 4 != 0 {
                    6
                } else if alignment & 8 != 0 {
                    3
                } else {
                    0
                }) as u32
        } else {
            alignment as u32
        };
    }
    pub fn number(&self, index: usize) -> u32 {
        self.normalized.get(index).copied().unwrap_or(0)
    }
    fn supply(
        &mut self,
        field: u32,
        write: impl FnOnce(&mut crate::ass_style_value::Builder) -> bool,
    ) -> bool {
        if field == 0
            || field > 26
            || self.current != field
            || self.supplied & (1 << field) != 0
            || !self.result.is_empty()
        {
            return false;
        }
        if !write(self.value.get_or_insert_with(Default::default)) {
            return false;
        }
        self.supplied |= 1 << field;
        true
    }
    pub fn integer(&mut self, field: u32, number: i32) -> bool {
        self.supply(field, |v| v.integer(field, number))
    }
    pub fn float(&mut self, field: u32, number: f64) -> bool {
        self.supply(field, |v| v.float(field, number))
    }
    pub fn text(&mut self, field: u32, input: &[u16]) -> bool {
        self.supply(field, |v| v.text(field, input))
    }
    pub fn finish(&mut self) -> bool {
        if self.current != 0 || self.cursor != 27 || !self.result.is_empty() {
            return false;
        }
        let mut expected = Style::new(&[], self.version);
        let mut required = 0;
        loop {
            let field = expected.next_field();
            if field == 0 {
                break;
            }
            if field != 27 {
                required |= 1 << field;
            }
        }
        if self.supplied != required {
            return false;
        }
        let Some(value) = self.value.as_ref() else {
            return false;
        };
        self.normalize(
            value.value.colors,
            value.alpha,
            value.encoding,
            value.value.border_style,
            value.value.screen_alignment,
        );
        let value = self.value.as_mut().unwrap();
        value.normalize(self.version, &self.normalized);
        match crate::document_style::encode(&value.value) {
            Ok(bytes) if bytes.len() <= u32::MAX as usize => {
                self.result = bytes;
                true
            }
            _ => false,
        }
    }
    pub fn result(&self) -> &[u8] {
        &self.result
    }
    pub fn name(&self) -> &[u16] {
        self.value.as_ref().map_or(&[], |v| v.name.as_slice())
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn value_builder_requires_each_enabled_field_once_and_owns_raw_names() {
        for version in 3..=6 {
            let mut state = Style::new(&[], version);
            assert!(!state.finish());
            assert!(!state.integer(4, 0));
            loop {
                let field = state.next_field();
                if field == 0 {
                    break;
                }
                if field == 27 {
                    continue;
                }
                if field == 1 || field == 2 {
                    assert!(!state.integer(field, 99));
                    assert!(state.text(field, &[42, 42, 0, 0xd800]));
                    assert!(!state.text(field, &[65]));
                } else if field == 3 || (12..=15).contains(&field) || field == 17 || field == 18 {
                    assert!(!state.integer(field, 99));
                    assert!(state.float(field, -0.0));
                } else {
                    assert!(!state.float(field, 1.0));
                    assert!(state.integer(field, -1));
                }
            }
            assert!(state.finish());
            assert!(!state.finish());
            let value = crate::document_style::decode(state.result()).unwrap();
            assert_eq!(state.name(), &[0, 0xd800]);
            assert_eq!(value.font_name, &[42, 42, 0, 0xd800]);
            assert_eq!(value.font_size.to_bits(), (-0.0f64).to_bits());
            assert_eq!(value.font_weight, 700);
            assert!(value.italic);
            assert_eq!(value.underline, version >= 5);
            assert_eq!(value.strikeout, version >= 5);
            assert_eq!(value.font_scale_x, if version >= 5 { 0.0 } else { 100.0 });
            assert_eq!(value.relative_to, if version >= 6 { -1 } else { 2 });
            assert_eq!(value.char_set, 1);
            assert_eq!(value.margins, [-1; 4]);
        }
        let mut missing = Style::new(&[], 5);
        while missing.next_field() != 0 {}
        assert!(!missing.finish());
        assert!(missing.result().is_empty());
    }
    #[test]
    fn version_fields_keep_margin_assignment_before_alpha_and_encoding() {
        for version in 3..=6 {
            let mut state = Style::new(&[], version);
            let mut order = Vec::new();
            loop {
                let field = state.next_field();
                if field == 0 {
                    break;
                }
                order.push(field);
            }
            assert_eq!(order.contains(&10), version >= 5);
            assert_eq!(order.contains(&16), version >= 4);
            assert_eq!(order.contains(&23), version >= 6);
            assert_eq!(order.contains(&24), version <= 4);
            let margin = order.iter().position(|&f| f == 27).unwrap();
            assert_eq!(order[margin + 1], if version <= 4 { 24 } else { 25 });
            assert_eq!(state.next_field(), 0);
        }
    }
    #[test]
    fn legacy_colors_alignment_and_modern_alpha_keep_all_bits() {
        let mut state = Style::new(&[], 4);
        state.normalize([0xff112233, 2, 3, 0x80445566], -1, -1, 3, -1);
        assert_eq!(state.number(0), 0xff112233);
        assert_eq!(state.number(2), 0x80445566);
        assert_eq!(state.number(4), 0);
        assert_eq!(state.number(7), 128);
        assert_eq!(state.number(8), 1);
        assert_eq!(state.number(9), 1);
        assert_eq!(state.number(10), 9);
        let mut modern = Style::new(&[], 5);
        modern.normalize([0xff112233, 2, 3, 0x80445566], 999, 128, 1, -1);
        assert_eq!(modern.number(0), 0x112233);
        assert_eq!(modern.number(4), 255);
        assert_eq!(modern.number(7), 128);
        assert_eq!(modern.number(10), u32::MAX);
    }
}
