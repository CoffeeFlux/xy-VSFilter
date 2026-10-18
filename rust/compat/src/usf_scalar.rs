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
fn named_color(text: &[u16]) -> u32 {
    let text = prefix(text);
    COLORS
        .iter()
        .find(|(name, _)| text.iter().copied().eq(name.bytes().map(u16::from)))
        .map_or(0, |(_, value)| *value)
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
                    Pending::Done(color_value(named_color(input)))
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

// Generated exactly from cleanup STS.cpp htmlcolors[]. Names and unusual values
// are intentional; this is not a CSS/ASS specification color table.
const COLORS: [(&str, u32); 138] = [
    ("white", 0xffffff),
    ("whitesmoke", 0xf5f5f5),
    ("ghostwhite", 0xf8f8ff),
    ("snow", 0xfffafa),
    ("gainsboro", 0xdcdcdc),
    ("lightgrey", 0xd3d3d3),
    ("silver", 0xc0c0c0),
    ("darkgray", 0xa9a9a9),
    ("gray", 0x808080),
    ("dimgray", 0x696969),
    ("lightslategray", 0x778899),
    ("slategray", 0x708090),
    ("darkslategray", 0x2f4f4f),
    ("black", 0x000000),
    ("azure", 0xf0ffff),
    ("aliceblue", 0xf0f8ff),
    ("mintcream", 0xf5fffa),
    ("honeydew", 0xf0fff0),
    ("lightcyan", 0xe0ffff),
    ("paleturqoise", 0xafeeee),
    ("powderblue", 0xb0e0e6),
    ("lightblue", 0xadd8ed),
    ("lightsteelblue", 0xb0c4de),
    ("skyblue", 0x87ceeb),
    ("lightskyblue", 0x87cefa),
    ("cyan", 0x00ffff),
    ("aqua", 0x00ff80),
    ("deepskyblue", 0x00bfff),
    ("aquamarine", 0x7fffd4),
    ("turquoise", 0x40e0d0),
    ("darkturquoise", 0x00ced1),
    ("lightseagreen", 0x20b2aa),
    ("mediumturquoise", 0x40e0dd),
    ("mediumaquamarine", 0x66cdaa),
    ("cadetblue", 0x5f9ea0),
    ("teal", 0x008080),
    ("darkcyan", 0x008b8b),
    ("comflowerblue", 0x6495ed),
    ("dodgerblue", 0x1e90ff),
    ("steelblue", 0x4682b4),
    ("royalblue", 0x4169e1),
    ("blue", 0x0000ff),
    ("mediumblue", 0x0000cd),
    ("mediumslateblue", 0x7b68ee),
    ("slateblue", 0x6a5acd),
    ("darkslateblue", 0x483d8b),
    ("darkblue", 0x00008b),
    ("midnightblue", 0x191970),
    ("navy", 0x000080),
    ("palegreen", 0x98fb98),
    ("lightgreen", 0x90ee90),
    ("mediumspringgreen", 0x00fa9a),
    ("springgreen", 0x00ff7f),
    ("chartreuse", 0x7fff00),
    ("lawngreen", 0x7cfc00),
    ("lime", 0x00ff00),
    ("limegreen", 0x32cd32),
    ("greenyellow", 0xadff2f),
    ("yellowgreen", 0x9acd32),
    ("darkseagreen", 0x8fbc8f),
    ("mediumseagreen", 0x3cb371),
    ("seagreen", 0x2e8b57),
    ("olivedrab", 0x6b8e23),
    ("forestgreen", 0x228b22),
    ("green", 0x008000),
    ("darkkhaki", 0xbdb76b),
    ("olive", 0x808000),
    ("darkolivegreen", 0x556b2f),
    ("darkgreen", 0x006400),
    ("floralwhite", 0xfffaf0),
    ("seashell", 0xfff5ee),
    ("ivory", 0xfffff0),
    ("beige", 0xf5f5dc),
    ("cornsilk", 0xfff8dc),
    ("lemonchiffon", 0xfffacd),
    ("lightyellow", 0xffffe0),
    ("lightgoldenrodyellow", 0xfafad2),
    ("papayawhip", 0xffefd5),
    ("blanchedalmond", 0xffedcd),
    ("palegoldenrod", 0xeee8aa),
    ("khaki", 0xf0eb8c),
    ("bisque", 0xffe4c4),
    ("moccasin", 0xffe4b5),
    ("navajowhite", 0xffdead),
    ("peachpuff", 0xffdab9),
    ("yellow", 0xffff00),
    ("gold", 0xffd700),
    ("wheat", 0xf5deb3),
    ("orange", 0xffa500),
    ("darkorange", 0xff8c00),
    ("oldlace", 0xfdf5e6),
    ("linen", 0xfaf0e6),
    ("antiquewhite", 0xfaebd7),
    ("lightsalmon", 0xffa07a),
    ("darksalmon", 0xe9967a),
    ("salmon", 0xfa8072),
    ("lightcoral", 0xf08080),
    ("indianred", 0xcd5c5c),
    ("coral", 0xff7f50),
    ("tomato", 0xff6347),
    ("orangered", 0xff4500),
    ("red", 0xff0000),
    ("crimson", 0xdc143c),
    ("firebrick", 0xb22222),
    ("maroon", 0x800000),
    ("darkred", 0x8b0000),
    ("lavender", 0xe6e6fe),
    ("lavenderblush", 0xfff0f5),
    ("mistyrose", 0xffe4e1),
    ("thistle", 0xd8bfd8),
    ("pink", 0xffc0cb),
    ("lightpink", 0xffb6c1),
    ("palevioletred", 0xdb7093),
    ("hotpink", 0xff69b4),
    ("fuchsia", 0xff00ee),
    ("magenta", 0xff00ff),
    ("mediumvioletred", 0xc71585),
    ("deeppink", 0xff1493),
    ("plum", 0xdda0dd),
    ("violet", 0xee82ee),
    ("orchid", 0xda70d6),
    ("mediumorchid", 0xba55d3),
    ("mediumpurple", 0x9370db),
    ("purple", 0x9370db),
    ("blueviolet", 0x8a2be2),
    ("darkviolet", 0x9400d3),
    ("darkorchid", 0x9932cc),
    ("tan", 0xd2b48c),
    ("burlywood", 0xdeb887),
    ("sandybrown", 0xf4a460),
    ("peru", 0xcd853f),
    ("goldenrod", 0xdaa520),
    ("darkgoldenrod", 0xb8860b),
    ("chocolate", 0xd2691e),
    ("rosybrown", 0xbc8f8f),
    ("sienna", 0xa0522d),
    ("saddlebrown", 0x8b4513),
    ("brown", 0xa52a2a),
];

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
