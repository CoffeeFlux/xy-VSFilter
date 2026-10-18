// SPDX-License-Identifier: GPL-2.0-or-later
//! USF TimeToInt, ColorToDWORD, TranslateMargin and TranslateAlignment at
//! CoffeeFlux cleanup 67da07cd. Windows wcstol and CompareNoCase stay services.
//! Raw UTF-16 is retained; CString tokenization can resume after embedded NUL.
use crate::ass_document::Error;
use alloc::vec::Vec;

type Units = Vec<u16>;
const ALIGNMENTS: [&str; 9] = [
    "BottomLeft",
    "BottomCenter",
    "BottomRight",
    "MiddleLeft",
    "MiddleCenter",
    "MiddleRight",
    "TopLeft",
    "TopCenter",
    "TopRight",
];
fn prefix(text: &[u16]) -> &[u16] {
    &text[..text.iter().position(|&u| u == 0).unwrap_or(text.len())]
}
fn delimiter(u: u16) -> bool {
    matches!(u, 46 | 44 | 58)
}
fn tokens(input: &[u16]) -> Vec<Units> {
    let mut out = Vec::new();
    let mut at = 0;
    while at < input.len() {
        while at < input.len() && delimiter(input[at]) {
            at += 1;
        }
        let begin = at;
        while at < input.len() && input[at] != 0 && !delimiter(input[at]) {
            at += 1;
        }
        if at == begin {
            break;
        }
        out.push(input[begin..at].to_vec());
        // CString::Tokenize advances past the terminator of a nonempty token,
        // including an embedded NUL. A NUL at the next token's start ends it.
        at += 1;
    }
    out
}
fn color_value(raw: u32) -> i32 {
    (((raw & 255) << 16) | (raw & 0xff00ff00) | ((raw >> 16) & 255)) as i32
}

enum Pending {
    Time {
        tokens: Vec<Units>,
        index: usize,
        total: i32,
    },
    Color {
        hex: Units,
    },
    Margin {
        input: Units,
        width: i32,
        percent: bool,
    },
    Alignment {
        input: Units,
        index: usize,
    },
    Done(i32),
}
pub struct Run {
    pending: Pending,
}
impl Run {
    /// kind1 time,2 color,3 margin,4 alignment. Only margin accepts an argument.
    pub fn new(kind: u32, input: &[u16], argument: i32) -> Result<Self, Error> {
        if kind != 3 && argument != 0 {
            return Err(Error::Protocol);
        }
        let pending = match kind {
            1 => {
                let mut tokens = tokens(input);
                tokens.reverse();
                match tokens.len() {
                    0 => Pending::Done(0),
                    1..=4 => Pending::Time {
                        tokens,
                        index: 0,
                        total: 0,
                    },
                    _ => Pending::Done(-1),
                }
            }
            2 => {
                if input.is_empty() {
                    Pending::Done(0)
                } else if input[0] == 35 {
                    let first = input.iter().position(|&u| u != 35).unwrap_or(input.len());
                    Pending::Color {
                        hex: input[first..].to_vec(),
                    }
                } else {
                    Pending::Done(color_value(crate::colors::named(input).unwrap_or(0)))
                }
            }
            3 => {
                if input.is_empty() {
                    Pending::Done(0)
                } else {
                    Pending::Margin {
                        input: input.to_vec(),
                        width: argument,
                        percent: prefix(input).contains(&37),
                    }
                }
            }
            4 => Pending::Alignment {
                input: input.to_vec(),
                index: 0,
            },
            _ => return Err(Error::Protocol),
        };
        Ok(Self { pending })
    }
    /// op1 wcstol response includes whether any numeric prefix was consumed;
    /// op2 CompareNoCase response requires consumed0. Invalid replies do not advance.
    pub fn step(&mut self, op: u32, value: i32, consumed: u32) -> Result<(), Error> {
        let expected = match self.pending {
            Pending::Done(_) => return Err(Error::Protocol),
            Pending::Alignment { .. } => 2,
            _ => 1,
        };
        if op != expected || consumed > 1 || op == 2 && consumed != 0 {
            return Err(Error::Protocol);
        }
        match &mut self.pending {
            Pending::Time {
                tokens,
                index,
                total,
            } => {
                if consumed == 0 {
                    self.pending = Pending::Done(-1);
                } else {
                    *total =
                        total.wrapping_add(value.wrapping_mul([1, 1000, 60000, 3600000][*index]));
                    *index += 1;
                    if *index == tokens.len() {
                        self.pending = Pending::Done(*total);
                    }
                }
            }
            Pending::Color { .. } => self.pending = Pending::Done(color_value(value as u32)),
            Pending::Margin { width, percent, .. } => {
                self.pending = Pending::Done(if *percent {
                    width.wrapping_mul(value) / 100
                } else {
                    value
                })
            }
            Pending::Alignment { index, .. } => {
                if value == 0 {
                    self.pending = Pending::Done((*index + 1) as i32);
                } else {
                    *index += 1;
                    if *index == ALIGNMENTS.len() {
                        self.pending = Pending::Done(2);
                    }
                }
            }
            Pending::Done(_) => unreachable!(),
        }
        Ok(())
    }
    /// op/base/result/a-count/b-count:u32, then both raw UTF-16 strings.
    /// op3 is done and result is the original i32/u32 bit pattern.
    pub fn snapshot(&self) -> Result<Vec<u8>, Error> {
        let mut b = Vec::new();
        let alignment;
        let (op, base, result, a, c): (u32, i32, i32, &[u16], &[u16]) = match &self.pending {
            Pending::Time { tokens, index, .. } => (1, 10, 0, &tokens[*index], &[]),
            Pending::Color { hex } => (1, 16, 0, hex, &[]),
            Pending::Margin { input, .. } => (1, 10, 0, input, &[]),
            Pending::Alignment { input, index } => {
                alignment = ALIGNMENTS[*index].encode_utf16().collect::<Units>();
                (2, 0, 0, input, &alignment)
            }
            Pending::Done(value) => (3, 0, *value, &[], &[]),
        };
        if a.len() > i32::MAX as usize || c.len() > i32::MAX as usize {
            return Err(Error::CountOverflow);
        }
        b.extend(op.to_le_bytes());
        b.extend(base.to_le_bytes());
        b.extend(result.to_le_bytes());
        b.extend((a.len() as u32).to_le_bytes());
        b.extend((c.len() as u32).to_le_bytes());
        b.extend(a.iter().chain(c).flat_map(|u| u.to_le_bytes()));
        Ok(b)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;
    fn units(s: &str) -> Units {
        s.encode_utf16().collect()
    }
    fn state(r: &Run) -> (u32, i32, i32, Units, Units) {
        let b = r.snapshot().unwrap();
        let words: Vec<u32> = b[..20]
            .chunks_exact(4)
            .map(|v| u32::from_le_bytes(v.try_into().unwrap()))
            .collect();
        let raw: Units = b[20..]
            .chunks_exact(2)
            .map(|v| u16::from_le_bytes(v.try_into().unwrap()))
            .collect();
        (
            words[0],
            words[1] as i32,
            words[2] as i32,
            raw[..words[3] as usize].to_vec(),
            raw[words[3] as usize..].to_vec(),
        )
    }
    #[test]
    fn time_tokenization_resumes_after_nul_and_preserves_reverse_conversion_and_failure_order() {
        let raw = [49, 0, 50, 58, 51];
        let mut run = Run::new(1, &raw, 0).unwrap();
        assert_eq!(state(&run).3, units("3"));
        run.step(1, 3, 1).unwrap();
        assert_eq!(state(&run).3, units("2"));
        run.step(1, 2, 1).unwrap();
        assert_eq!(state(&run).3, units("1"));
        run.step(1, 1, 1).unwrap();
        assert_eq!(state(&run).2, 62003);
        let mut run = Run::new(1, &units("1:x:2"), 0).unwrap();
        run.step(1, 2, 1).unwrap();
        assert_eq!(state(&run).3, units("x"));
        run.step(1, 0, 0).unwrap();
        assert_eq!(state(&run).0, 3);
        assert_eq!(state(&run).2, -1);
        assert_eq!(
            state(&Run::new(1, &[49, 0, 0, 58, 50], 0).unwrap()).3,
            units("1")
        );
        assert_eq!(state(&Run::new(1, &units("1:2:3:4:5"), 0).unwrap()).2, -1);
        assert_eq!(state(&Run::new(1, &[0, 49, 58, 50], 0).unwrap()).2, 0);
    }
    #[test]
    fn integer_math_wraps_and_percent_search_stops_at_nul() {
        let mut r = Run::new(3, &units("2147483647%"), 640).unwrap();
        r.step(1, i32::MAX, 1).unwrap();
        assert_eq!(state(&r).2, -6);
        let mut r = Run::new(3, &[50, 0, 37], 640).unwrap();
        r.step(1, 2, 1).unwrap();
        assert_eq!(state(&r).2, 2);
        let mut r = Run::new(1, &units("2147483647:2147483647"), 0).unwrap();
        r.step(1, i32::MAX, 1).unwrap();
        r.step(1, i32::MAX, 1).unwrap();
        assert_eq!(state(&r).2, i32::MAX.wrapping_mul(1001));
    }
    #[test]
    fn colors_preserve_cleanup_table_quirks_raw_prefix_and_full_alpha_bits() {
        for (name, value) in [
            ("aqua", 0x0080ff00),
            ("purple", 0x00db7093),
            ("fuchsia", 0x00ee00ff),
            ("paleturquoise", 0),
            ("cornflowerblue", 0),
            ("RED", 0),
        ] {
            assert_eq!(
                state(&Run::new(2, &units(name), 0).unwrap()).2 as u32,
                value
            );
        }
        assert_eq!(
            state(&Run::new(2, &[114, 101, 100, 0, 0xd800], 0).unwrap()).2,
            255
        );
        let mut r = Run::new(2, &[35, 35, 0, 49], 0).unwrap();
        assert_eq!(state(&r).3, vec![0, 49]);
        assert_eq!(state(&r).1, 16);
        r.step(1, 0x80112233u32 as i32, 1).unwrap();
        assert_eq!(state(&r).2 as u32, 0x80332211);
    }
    #[test]
    fn alignment_requests_ordered_native_comparisons_and_rejects_bad_replies() {
        let mut r = Run::new(4, &[84, 111, 112, 0, 0xd800], 0).unwrap();
        for name in ALIGNMENTS {
            let before = r.snapshot().unwrap();
            assert_eq!(state(&r).4, units(name));
            assert_eq!(r.step(1, 0, 0), Err(Error::Protocol));
            assert_eq!(r.step(2, 0, 1), Err(Error::Protocol));
            assert_eq!(r.snapshot().unwrap(), before);
            r.step(2, 1, 0).unwrap();
        }
        assert_eq!(state(&r).2, 2);
        assert_eq!(r.step(2, 0, 0), Err(Error::Protocol));
        assert!(Run::new(5, &[], 0).is_err());
        assert!(Run::new(1, &[], 1).is_err());
    }
}
