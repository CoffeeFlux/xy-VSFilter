// SPDX-License-Identifier: GPL-2.0-or-later
//! Shared named colors and HTML/SAMI formatting from cleanup67da07cd.
//! Numeric conversion is a Windows CRT service. Raw UTF16 is never decoded.
use alloc::vec::Vec;

pub fn named(text: &[u16]) -> Option<u32> {
    let end = text.iter().position(|&u| u == 0).unwrap_or(text.len());
    COLORS
        .iter()
        .find(|(name, _)| text[..end].iter().copied().eq(name.bytes().map(u16::from)))
        .map(|(_, value)| *value)
}
/// Returned low24 bits are BGR. Named black bypasses the numeric-zero fallback.
pub fn html(input: &[u16], trim_hash: bool, number: impl FnOnce(&[u16]) -> i32) -> u32 {
    let begin = if trim_hash {
        input.iter().position(|&u| u != 35).unwrap_or(input.len())
    } else {
        0
    };
    let input = &input[begin..];
    let color = named(input).unwrap_or_else(|| {
        let n = number(input) as u32;
        if n == 0 {
            0xffffff
        } else {
            n
        }
    });
    ((color & 255) << 16) | (color & 0xff00) | ((color >> 16) & 255)
}
/// CString concatenation preserves every ctag unit, including embedded NULs.
pub fn tag(bgr: u32, ctag: &[u16]) -> Vec<u16> {
    let mut result = Vec::new();
    result.extend([123, 92]);
    result.extend_from_slice(ctag);
    result.extend([38, 72]);
    for shift in (0..6).rev() {
        result.push(u16::from(
            b"0123456789abcdef"[((bgr >> (shift * 4)) & 15) as usize],
        ));
    }
    result.extend([38, 125]);
    result
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
    fn u(s: &str) -> Vec<u16> {
        s.encode_utf16().collect()
    }
    #[test]
    fn named_black_differs_from_numeric_zero_and_names_use_first_nul() {
        assert_eq!(
            html(&u("black"), false, |_| panic!(
                "named value must not call CRT"
            )),
            0
        );
        assert_eq!(html(&u("0"), false, |_| 0), 0xffffff);
        assert_eq!(named(&u("red\0ignored")), Some(0xff0000));
        assert_eq!(named(&u("RED")), None);
        assert_eq!(named(&[0xd800, 0]), None);
        assert_eq!(named(&u("aqua")), Some(0x00ff80));
        assert_eq!(named(&u("paleturqoise")), Some(0xafeeee));
    }
    #[test]
    fn hash_policy_and_signed_channel_conversion_remain_distinct() {
        assert_eq!(
            html(&u("##red"), true, |_| panic!("named after trimming")),
            0xff
        );
        assert_eq!(
            html(&u("#red"), false, |s| {
                assert_eq!(s, u("#red"));
                0
            }),
            0xffffff
        );
        assert_eq!(html(&u("-1"), false, |_| -1), 0xffffff);
        assert_eq!(html(&u("raw"), false, |_| 0x80112233u32 as i32), 0x332211);
        assert_eq!(
            html(&[35, 35, 0, 65], true, |s| {
                assert_eq!(s, [0, 65]);
                0
            }),
            0xffffff
        );
    }
    #[test]
    fn tag_has_exact_lowercase_digits_and_preserves_raw_tag_units() {
        assert_eq!(tag(0x332211, &u("c")), u("{\\c&H332211&}"));
        let ctag = vec![0, 0xd800];
        let result = tag(0, &ctag);
        assert_eq!(&result[2..4], ctag);
        assert_eq!(&result[4..], u("&H000000&}"));
    }
}
