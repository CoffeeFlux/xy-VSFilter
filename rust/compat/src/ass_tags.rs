// SPDX-License-Identifier: GPL-2.0-or-later
// Derived from RTS.cpp, Copyright (C) 2003-2006 Gabest.
//! Lexical portion of CRenderedTextSubtitle::ParseSSATag.
//!
//! Whitespace is supplied by the native CRT, not Rust's Unicode tables. Spans
//! refer to raw UTF-16 units. Command lookup and style application remain native.

use std::ops::Range;

#[derive(Debug, PartialEq, Eq)]
pub struct Tag {
    pub command: Range<usize>,
    pub parameters: Vec<Range<usize>>,
}

/// CString's searches and the old pointer loops stop at the first NUL. Empty
/// commands still append a tag, and do not consume a following parameter list.
/// Parentheses do not nest: the first ')' ends the parameter list. Once a '\\'
/// precedes the next comma, the entire remaining parameter text is one item.
pub fn tokenize(input: &[u16], whitespace: &[u8]) -> Vec<Tag> {
    assert_eq!(input.len(), whitespace.len());
    let end = input.iter().position(|&c| c == 0).unwrap_or(input.len());
    let trim_left = |mut start: usize, end: usize| {
        while start < end && whitespace[start] != 0 {
            start += 1;
        }
        start
    };
    let trim_right = |start: usize, mut end: usize| {
        while end > start && whitespace[end - 1] != 0 {
            end -= 1;
        }
        end
    };
    let find = |start: usize, end: usize, unit: u16| {
        input[start..end]
            .iter()
            .position(|&c| c == unit)
            .map_or(end, |offset| start + offset)
    };
    let mut tags = Vec::new();
    let mut cursor = 0;
    while cursor < end {
        cursor = find(cursor, end, b'\\' as u16);
        if cursor == end {
            break;
        }
        let start = trim_left(cursor + 1, end);
        cursor = start;
        while cursor < end && input[cursor] != b'(' as u16 && input[cursor] != b'\\' as u16 {
            cursor += 1;
        }
        let mut tag = Tag {
            command: start..trim_right(start, cursor),
            parameters: Vec::new(),
        };
        if !tag.command.is_empty() && cursor < end && input[cursor] == b'(' as u16 {
            let start = trim_left(cursor + 1, end);
            cursor = find(start, end, b')' as u16);
            let param_end = trim_right(start, cursor);
            let mut param_start = start;
            while param_start < param_end {
                param_start = trim_left(param_start, param_end);
                let comma = find(param_start, param_end, b',' as u16);
                let slash = find(param_start, param_end, b'\\' as u16);
                if comma < slash {
                    if comma > param_start {
                        let stop = trim_right(param_start, comma);
                        if stop > param_start {
                            tag.parameters.push(param_start..stop);
                        }
                    }
                    param_start = comma + 1;
                } else if param_start < param_end {
                    tag.parameters.push(param_start..param_end);
                    param_start = param_end;
                }
            }
        }
        tags.push(tag);
    }
    tags
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn keeps_empty_tags_and_non_nested_parameter_recovery() {
        let input: Vec<u16> = "\\ (\\b2)\\t(, 0, 20 ,\\bord3,9)\\pos(1,(2))\\\0\\i1"
            .encode_utf16()
            .collect();
        let white: Vec<u8> = input.iter().map(|&c| u8::from(c == 32)).collect();
        let tags = tokenize(&input, &white);
        let text = |span: &Range<usize>| String::from_utf16(&input[span.clone()]).unwrap();
        assert_eq!(tags.len(), 5);
        assert!(tags[0].command.is_empty());
        assert_eq!(text(&tags[1].command), "b2)");
        assert_eq!(
            tags[2].parameters.iter().map(text).collect::<Vec<_>>(),
            ["0", "20", "\\bord3,9"]
        );
        assert_eq!(
            tags[3].parameters.iter().map(text).collect::<Vec<_>>(),
            ["1", "(2"]
        );
        assert!(tags[4].command.is_empty());
    }

    #[test]
    fn uses_supplied_whitespace_and_preserves_raw_surrogates() {
        let input = [92, 0xd800, 0x2003, 40, 0xdc00, 41];
        assert_eq!(tokenize(&input, &[0; 6])[0].command, 1..3);
        let tags = tokenize(&input, &[0, 0, 1, 0, 0, 0]);
        assert_eq!(tags[0].command, 1..2);
        assert_eq!(tags[0].parameters.len(), 1);
        assert_eq!(tags[0].parameters[0], 4..5);
    }
}
