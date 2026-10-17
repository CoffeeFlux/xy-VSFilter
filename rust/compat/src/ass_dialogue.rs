// SPDX-License-Identifier: GPL-2.0-or-later
//! ASS dialogue fields, in the pinned parser's order. Numeric conversion,
//! normalization and prepared entry construction. Numeric conversion and the
//! locale-sensitive Default comparison/text trimming remain native adapters.
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
    pub fn complete(&self) -> bool {
        self.stage == 14 && self.pending.is_none()
    }
    /// The parser removes leading stars before the native Default comparison.
    pub fn style_name(&self) -> &[u16] {
        let style = self.string(1);
        &style[style.iter().take_while(|&&unit| unit == 42).count()..]
    }
    fn timestamp(&self, first: usize) -> i64 {
        let n = &self.numbers[first..first + 4];
        // Explicit safe emulation of the pinned Windows machine arithmetic.
        // Every intermediate is i64; centiseconds are multiplied before adding.
        i64::from(n[0])
            .wrapping_mul(60)
            .wrapping_add(i64::from(n[1]))
            .wrapping_mul(60)
            .wrapping_add(i64::from(n[2]))
            .wrapping_mul(1000)
            .wrapping_add(i64::from(n[3]).wrapping_mul(10))
            .wrapping_mul(10000)
    }
    /// Called only after completion and native FastTrim/CompareNoCase. Rejection
    /// precedes CR/LF preparation, just as AddSTSEntryOnly does in the reference.
    pub fn record(
        &self,
        trimmed_text: &[u16],
        unicode: bool,
        default_style: bool,
    ) -> Result<Option<crate::document::PreparedEntry>, crate::document_prepare::Error> {
        let start = self.timestamp(1);
        let end = self.timestamp(5);
        if trimmed_text.is_empty() || start > end {
            return Ok(None);
        }
        let style = if default_style {
            &[68, 101, 102, 97, 117, 108, 116][..]
        } else {
            self.style_name()
        };
        let effect = self.string(3);
        let text = self.string(4);
        let length = effect.len().min(text.len());
        let effect = if effect[..length] == text[..length] {
            &[]
        } else {
            effect
        };
        let entry = crate::document::PreparedEntry {
            text: trimmed_text.to_vec(),
            style: style.to_vec(),
            actor: self.string(2).to_vec(),
            effect: effect.to_vec(),
            unicode,
            start,
            end,
            layer: self.number(0),
            readorder: -1,
            margins: [
                self.number(9),
                self.number(11),
                self.number(10),
                self.number(12),
            ],
        };
        crate::document_prepare::prepare_add_record(entry, true).map(Some)
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
