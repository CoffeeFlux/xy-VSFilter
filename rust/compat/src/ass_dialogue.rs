// SPDX-License-Identifier: GPL-2.0-or-later
//! ASS dialogue fields, in the pinned parser's order. Numeric conversion,
//! style/effect cleanup, timestamps and entry insertion remain native.
use crate::fields::next_field;
use alloc::vec::Vec;

pub struct Dialogue {
    input: Vec<u16>,
    position: usize,
    version: i32,
    stage: usize,
    pending: Option<usize>,
    numbers: [i32; 13],
    strings: [Vec<u16>; 5], // numeric token, style, actor, effect, remaining text
    pub separator: u16,
}
impl Dialogue {
    pub fn new(input: &[u16], version: i32) -> Self {
        Self {
            input: input.to_vec(),
            position: 0,
            version,
            stage: 0,
            pending: None,
            numbers: [0; 13],
            strings: core::array::from_fn(|_| Vec::new()),
            separator: 44,
        }
    }
    fn field(&mut self, separator: u16) -> Vec<u16> {
        let field = next_field(&self.input[self.position..], separator);
        let start = self.position + field.start;
        let end = self.position + field.end;
        self.position = if end < self.input.len() && self.input[end] != 0 {
            self.input[end] = 0;
            end + 1
        } else {
            start
        };
        self.input[start..end].to_vec()
    }
    /// 1 requests native NextInt on the token; 0 exposes completed fields.
    /// Missing delimiters deliberately leave the cursor at the field's start.
    pub fn step(&mut self, reply: i32) -> u32 {
        if let Some(index) = self.pending.take() {
            self.numbers[index] = reply;
            if index == 11 && self.version < 6 {
                self.numbers[12] = reply;
            }
        }
        loop {
            let stage = self.stage;
            if stage >= 14 {
                return 0;
            }
            self.stage += 1;
            self.separator = 44;
            match stage {
                0 => {
                    if self.version <= 4 {
                        self.field(61);
                    } else {
                        self.pending = Some(0);
                    }
                }
                1..=8 => {
                    self.separator = [58, 58, 46, 44][(stage - 1) % 4];
                    self.pending = Some(stage);
                }
                9 => {
                    self.strings[1] = self.field(44);
                    self.strings[2] = self.field(44);
                    self.pending = Some(9);
                }
                10 | 11 => {
                    self.pending = Some(stage);
                }
                12 => {
                    if self.version < 6 {
                        continue;
                    }
                    self.pending = Some(12);
                }
                13 => {
                    self.strings[3] = self.field(44);
                    let rest = &self.input[self.position..];
                    let length = rest
                        .iter()
                        .position(|&unit| unit == 0)
                        .unwrap_or(rest.len());
                    self.strings[4] = rest[..length].to_vec();
                    return 0;
                }
                _ => unreachable!(),
            }
            self.strings[0] = self.field(self.separator);
            return 1;
        }
    }
    pub fn number(&self, index: usize) -> i32 {
        self.numbers.get(index).copied().unwrap_or(0)
    }
    pub fn string(&self, group: usize) -> &[u16] {
        self.strings.get(group).map_or(&[], Vec::as_slice)
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn legacy_marked_value_is_consumed_but_not_used_as_layer() {
        let input: Vec<u16> = b"Marked=9,0:00:01.20,0:00:02.30,*Default,actor,1,2,3,effect,text"
            .iter()
            .map(|&b| b.into())
            .collect();
        let mut state = Dialogue::new(&input, 4);
        let replies = [9, 0, 0, 1, 20, 0, 0, 2, 30, 1, 2, 3];
        let mut reply = 0;
        for expected in replies {
            assert_eq!(state.step(reply), 1);
            reply = expected;
        }
        assert_eq!(state.step(reply), 0);
        assert_eq!(state.number(0), 0);
        assert_eq!(state.number(11), 3);
        assert_eq!(state.number(12), 3);
        assert_eq!(state.string(4), &[116, 101, 120, 116]);
    }
    #[test]
    fn missing_delimiters_reuse_final_field_and_stop_at_nul() {
        let mut state = Dialogue::new(&[32, 49, 0, 44, 50], 6);
        for _ in 0..13 {
            assert_eq!(state.step(7), 1);
            assert_eq!(state.string(0), &[49]);
        }
        assert_eq!(state.step(7), 0);
        for group in 1..5 {
            assert_eq!(state.string(group), &[49]);
        }
        for index in 0..13 {
            assert_eq!(state.number(index), 7);
        }
    }
}
