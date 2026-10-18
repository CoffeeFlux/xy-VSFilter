// SPDX-License-Identifier: GPL-2.0-or-later
//! Renderer ParseHtmlTag from cleanup67da07cd. CString operations retain raw
//! UTF16 length, first-NUL search/trim quirks and Windows service ordering.
use crate::document_style::{self, StyleValue};
use alloc::vec::Vec;

pub trait Services {
    fn space(&mut self, unit: u16) -> bool;
    fn lower(&mut self, text: &mut [u16]);
    fn number(&mut self, text: &[u16], base: i32) -> i32;
}
fn prefix(s: &[u16]) -> &[u16] {
    &s[..s.iter().position(|&u| u == 0).unwrap_or(s.len())]
}
fn eq(s: &[u16], literal: &str) -> bool {
    prefix(s).iter().copied().eq(literal.bytes().map(u16::from))
}
fn find(s: &[u16], ch: u16) -> Option<usize> {
    prefix(s).iter().position(|&u| u == ch)
}
fn mid(s: &[u16], first: usize) -> Vec<u16> {
    s[first.min(s.len())..].to_vec()
}
fn trim(s: &mut Vec<u16>, mut matches: impl FnMut(u16) -> bool) {
    // CString TrimRight scans only to the first NUL. A trailing match there
    // truncates the entire stored string, including any post-NUL suffix.
    let mut last = None;
    for (i, &u) in prefix(s).iter().enumerate() {
        if matches(u) {
            if last.is_none() {
                last = Some(i);
            }
        } else {
            last = None;
        }
    }
    if let Some(last) = last {
        s.truncate(last);
    }
    let mut first = 0;
    while first < s.len() && s[first] != 0 && matches(s[first]) {
        first += 1;
    }
    s.drain(..first);
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Karaoke {
    pub kind: i32,
    pub start: i32,
    pub end: i32,
}
pub fn parse(
    input: &[u16],
    current: &mut StyleValue,
    original: &StyleValue,
    k: &mut Karaoke,
    svc: &mut impl Services,
) -> bool {
    if input.starts_with(&[33, 45, 45]) {
        return true;
    }
    let closing = input.first() == Some(&47);
    let mut rest = input.to_vec();
    trim(&mut rest, |u| u == 32 || u == 47);
    let split = find(&rest, 32).unwrap_or(rest.len());
    let mut tag = rest[..split].to_vec();
    svc.lower(&mut tag);
    rest = mid(&rest, split);
    trim(&mut rest, |u| svc.space(u));
    let mut attributes = Vec::new();
    while let Some(split) = find(&rest, 61).filter(|&i| i > 0) {
        let mut name = rest[..split].to_vec();
        trim(&mut name, |u| svc.space(u));
        svc.lower(&mut name);
        rest = mid(&rest, split + 1);
        let mut first = 0;
        while first < rest.len() && svc.space(rest[first]) {
            first += 1;
        }
        rest = mid(&rest, first);
        let end = if rest.first() == Some(&34) {
            rest = mid(&rest, 1);
            find(&rest, 34)
        } else {
            find(&rest, 32)
        }
        .unwrap_or(rest.len());
        let mut value = rest[..end].to_vec();
        trim(&mut value, |u| svc.space(u));
        svc.lower(&mut value);
        attributes.push((name, value));
        rest = mid(&rest, end + 1);
    }
    if eq(&tag, "text") {
    } else if eq(&tag, "b") || eq(&tag, "strong") {
        current.font_weight = if closing { original.font_weight } else { 700 };
    } else if eq(&tag, "i") || eq(&tag, "em") {
        current.italic = if closing { original.italic } else { true };
    } else if eq(&tag, "u") {
        current.underline = if closing { original.underline } else { true };
    } else if eq(&tag, "s") || eq(&tag, "strike") || eq(&tag, "del") {
        current.strikeout = if closing { original.strikeout } else { true };
    } else if eq(&tag, "font") {
        if closing {
            current.font_name = original.font_name.clone();
            current.font_size = original.font_size;
            current.colors = original.colors;
        } else {
            for (name, value) in attributes {
                if value.is_empty() {
                    continue;
                }
                if eq(&name, "face") {
                    current.font_name = value;
                } else if eq(&name, "size") {
                    let n = svc.number(&value, 10) as f64;
                    match value[0] {
                        43 => current.font_size += n,
                        45 => current.font_size -= n,
                        _ => current.font_size = n,
                    }
                } else if eq(&name, "outline-level") {
                    let n = svc.number(&value, 10) as f64;
                    current.outline_width_x = n;
                    current.outline_width_y = n;
                } else if eq(&name, "shadow-level") {
                    let n = svc.number(&value, 10) as f64;
                    current.shadow_depth_x = n;
                    current.shadow_depth_y = n;
                } else if let Some(index) = if eq(&name, "color") {
                    Some(0)
                } else if eq(&name, "outline-color") {
                    Some(2)
                } else if eq(&name, "shadow-color") {
                    Some(3)
                } else {
                    None
                } {
                    current.colors[index] =
                        crate::colors::html(&value, true, |text| svc.number(text, 16));
                }
            }
        }
    } else if eq(&tag, "k") && attributes.len() == 1 && eq(&attributes[0].0, "t") {
        k.kind = 1;
        k.start = k.end;
        k.end = k.end.wrapping_add(svc.number(&attributes[0].1, 10));
    } else {
        return false;
    }
    true
}
#[derive(Debug, PartialEq, Eq)]
pub enum Error {
    Shape,
    Overflow,
}
fn word(b: &[u8], at: usize) -> Result<u32, Error> {
    Ok(u32::from_le_bytes(
        b.get(at..at + 4)
            .ok_or(Error::Shape)?
            .try_into()
            .map_err(|_| Error::Shape)?,
    ))
}
/// Wire: tag-unit/current-byte/original-byte counts, then3karaoke i32s;
/// rawtag units and2complete style records follow. Reply: accepted,3karaoke
/// i32s,style-byte count,style. No native layout or pointer enters this codec.
pub fn packet(input: &[u8], capacity: usize, svc: &mut impl Services) -> Result<Vec<u8>, Error> {
    let count = word(input, 0)? as usize;
    let clen = word(input, 4)? as usize;
    let olen = word(input, 8)? as usize;
    if count > i32::MAX as usize {
        return Err(Error::Overflow);
    }
    let tag_end = count
        .checked_mul(2)
        .and_then(|n| n.checked_add(24))
        .ok_or(Error::Overflow)?;
    let cend = tag_end.checked_add(clen).ok_or(Error::Overflow)?;
    let end = cend.checked_add(olen).ok_or(Error::Overflow)?;
    if end != input.len() {
        return Err(Error::Shape);
    }
    let mut k = Karaoke {
        kind: word(input, 12)? as i32,
        start: word(input, 16)? as i32,
        end: word(input, 20)? as i32,
    };
    let tag: Vec<u16> = input[24..tag_end]
        .chunks_exact(2)
        .map(|b| u16::from_le_bytes([b[0], b[1]]))
        .collect();
    let mut current = document_style::decode(&input[tag_end..cend]).map_err(|_| Error::Shape)?;
    let original = document_style::decode(&input[cend..]).map_err(|_| Error::Shape)?;
    let max_font = count
        .max(current.font_name.len())
        .max(original.font_name.len());
    let maximum = max_font
        .checked_mul(2)
        .and_then(|n| n.checked_add(212))
        .ok_or(Error::Overflow)?;
    if capacity < maximum {
        return Err(Error::Shape);
    }
    let accepted = parse(&tag, &mut current, &original, &mut k, svc);
    let style = document_style::encode(&current).map_err(|_| Error::Overflow)?;
    let length = u32::try_from(style.len()).map_err(|_| Error::Overflow)?;
    let mut out = Vec::new();
    for value in [
        u32::from(accepted),
        k.kind as u32,
        k.start as u32,
        k.end as u32,
        length,
    ] {
        out.extend(value.to_le_bytes());
    }
    out.extend(style);
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::{string::String, vec};
    struct Native;
    impl Services for Native {
        fn space(&mut self, u: u16) -> bool {
            matches!(u, 9..=13 | 32)
        }
        fn lower(&mut self, s: &mut [u16]) {
            for u in s.iter_mut().take_while(|u| **u != 0) {
                if (65..=90).contains(u) {
                    *u += 32;
                }
            }
        }
        fn number(&mut self, s: &[u16], base: i32) -> i32 {
            i32::from_str_radix(
                String::from_utf16(prefix(s))
                    .unwrap()
                    .trim()
                    .trim_start_matches('+'),
                base as u32,
            )
            .unwrap_or(0)
        }
    }
    fn u(s: &str) -> Vec<u16> {
        s.encode_utf16().collect()
    }
    fn style() -> StyleValue {
        document_style::decode(&[0; 192]).unwrap()
    }
    fn k() -> Karaoke {
        Karaoke {
            kind: 7,
            start: 10,
            end: i32::MAX,
        }
    }
    #[test]
    fn negative_size_adds_and_closing_font_restores_only_reference_fields() {
        let mut s = style();
        s.font_size = 23.5;
        s.alpha = [1, 2, 3, 4];
        let mut org = style();
        org.font_size = 31.25;
        org.font_name = u("Original");
        org.colors = [5, 6, 7, 8];
        assert!(parse(
            &u("font face=Arial size=-2 outline-level=5 color=red"),
            &mut s,
            &org,
            &mut k(),
            &mut Native
        ));
        assert_eq!(s.font_name, u("arial"));
        assert_eq!(s.font_size, 25.5);
        assert_eq!(s.colors[0], 255);
        assert!(parse(&u("/font"), &mut s, &org, &mut k(), &mut Native));
        assert_eq!(s.font_name, org.font_name);
        assert_eq!(s.font_size, 31.25);
        assert_eq!(s.colors, org.colors);
        assert_eq!(s.alpha, [1, 2, 3, 4]);
        assert_eq!(s.outline_width_x, 5.0);
    }
    #[test]
    fn karaoke_wraps_and_duplicate_attributes_refuse_without_mutation() {
        let mut s = style();
        let mut state = k();
        assert!(parse(
            &u("k t=2"),
            &mut s,
            &style(),
            &mut state,
            &mut Native
        ));
        assert_eq!(
            state,
            Karaoke {
                kind: 1,
                start: i32::MAX,
                end: i32::MIN + 1
            }
        );
        let before = state;
        assert!(!parse(
            &u("k t=1 t=2"),
            &mut s,
            &style(),
            &mut state,
            &mut Native
        ));
        assert_eq!(state, before);
    }
    #[test]
    fn trim_before_nul_discards_tail_only_when_a_trailing_match_exists() {
        let mut s = u(" a \0tail");
        trim(&mut s, |u| u == 32);
        assert_eq!(s, u("a"));
        let mut s = u(" a\0tail");
        trim(&mut s, |u| u == 32);
        assert_eq!(s, u("a\0tail"));
        let mut s = style();
        assert!(parse(
            &u("font face=Ab\0CD"),
            &mut s,
            &style(),
            &mut k(),
            &mut Native
        ));
        assert_eq!(s.font_name, u("ab\0CD"));
    }
    #[test]
    fn closing_is_detected_before_trim_and_comment_preserves_every_bit() {
        let mut s = style();
        let mut org = style();
        org.font_weight = 321;
        s.font_size = f64::from_bits(0x7ff8123456789abc);
        let before = s.clone();
        assert!(parse(
            &u("!-- font size=0"),
            &mut s,
            &org,
            &mut k(),
            &mut Native
        ));
        assert_eq!(s, before);
        assert!(parse(&u(" /b"), &mut s, &org, &mut k(), &mut Native));
        assert_eq!(s.font_weight, 700);
        assert!(parse(&u("/ b"), &mut s, &org, &mut k(), &mut Native));
        assert_eq!(s.font_weight, 321);
    }
    #[test]
    fn packet_bounds_precede_services_and_reply_is_detached() {
        let mut bytes = vec![0; 24 + 384];
        bytes[4..8].copy_from_slice(&192u32.to_le_bytes());
        bytes[8..12].copy_from_slice(&192u32.to_le_bytes());
        assert_eq!(packet(&bytes, 211, &mut Native), Err(Error::Shape));
        let out = packet(&bytes, 212, &mut Native).unwrap();
        assert_eq!(out.len(), 212);
        assert_eq!(&out[..4], &[0; 4]);
        bytes.fill(255);
        assert_eq!(&out[20..], &[0; 192]);
    }
}
