// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned ASS style remainder, version-dependent field order and integer
//! normalization. Native GetStr/GetInt/GetFloat and floating clamps remain.
use alloc::vec::Vec;
pub struct Style {
    remaining: Vec<u16>,
    version: i32,
    cursor: usize,
    normalized: [u32; 11],
}
impl Style {
    pub fn new(input: &[u16], version: i32) -> Self {
        Self {
            remaining: input.to_vec(),
            version,
            cursor: 0,
            normalized: [0; 11],
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
                return field;
            }
        }
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
}
#[cfg(test)]
mod tests {
    use super::*;
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
