// SPDX-License-Identifier: GPL-2.0-or-later
//! SaveAs body row formatting from pinned STS.cpp:3460–3538.
//! Native retains text/newline preparation, numeric casts, headers, style rows,
//! iteration/skip policy, file writes and codepages. Raw %s fields terminate at
//! the first NUL; no Unicode normalization or LF processing occurs here.
use crate::document::PreparedEntry;
use alloc::vec::Vec;

#[derive(Clone, Copy, Debug)]
pub struct Numbers {
    pub sequence: i32,
    pub start_ms: i32,
    pub end_ms: i32,
    pub frame_start: i32,
    pub frame_end: i32,
    pub margin_v: i32,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    InvalidFormat,
    CountOverflow,
}
fn checked_len(current: usize, added: usize) -> Result<usize, Error> {
    current
        .checked_add(added)
        .filter(|&n| {
            n <= i32::MAX as usize
                && n.checked_add(1)
                    .and_then(|units| units.checked_mul(core::mem::size_of::<u16>()))
                    .is_some_and(|bytes| bytes <= isize::MAX as usize)
        })
        .ok_or(Error::CountOverflow)
}
struct Row(Vec<u16>);
impl Row {
    fn units(&mut self, value: &[u16]) -> Result<(), Error> {
        checked_len(self.0.len(), value.len())?;
        self.0.extend_from_slice(value);
        Ok(())
    }
    fn literal(&mut self, value: &str) -> Result<(), Error> {
        // Templates are ASCII only.
        for byte in value.bytes() {
            self.units(&[u16::from(byte)])?;
        }
        Ok(())
    }
    fn string(&mut self, value: &[u16]) -> Result<(), Error> {
        self.units(&value[..value.iter().position(|&u| u == 0).unwrap_or(value.len())])
    }
    fn integer(&mut self, value: i32, width: usize) -> Result<(), Error> {
        let mut digits = [0u16; 10];
        let mut magnitude = i64::from(value).unsigned_abs();
        let mut offset = digits.len();
        loop {
            offset -= 1;
            digits[offset] = 48 + (magnitude % 10) as u16;
            magnitude /= 10;
            if magnitude == 0 {
                break;
            }
        }
        let negative = value < 0;
        if negative {
            self.literal("-")?;
        }
        for _ in (digits.len() - offset + usize::from(negative))..width {
            self.literal("0")?;
        }
        self.units(&digits[offset..])
    }
    fn time(
        &mut self,
        ms: i32,
        hour_width: usize,
        fraction: Option<(char, usize)>,
    ) -> Result<(), Error> {
        self.integer(ms / 60 / 60 / 1000, hour_width)?;
        self.literal(":")?;
        self.integer((ms / 60 / 1000) % 60, 2)?;
        self.literal(":")?;
        self.integer((ms / 1000) % 60, 2)?;
        if let Some((separator, width)) = fraction {
            self.literal(if separator == ',' { "," } else { "." })?;
            self.integer(
                if width == 3 {
                    ms % 1000
                } else {
                    (ms % 1000) / 10
                },
                width,
            )?;
        }
        Ok(())
    }
}
pub fn format_row(format: u32, n: Numbers, entry: &PreparedEntry) -> Result<Vec<u16>, Error> {
    if format >= 14 {
        return Err(Error::InvalidFormat);
    }
    let mut r = Row(Vec::new());
    match format {
        0 => {
            r.integer(n.sequence, 0)?;
            r.literal("\n")?;
            r.time(n.start_ms, 2, Some((',', 3)))?;
            r.literal(" --> ")?;
            r.time(n.end_ms, 2, Some((',', 3)))?;
            r.literal("\n")?;
            r.string(&entry.text)?;
            r.literal("\n\n")?;
        }
        1 => {
            r.literal("{")?;
            r.integer(n.frame_start, 0)?;
            r.literal("}{")?;
            r.integer(n.frame_end, 0)?;
            r.literal("}")?;
            r.string(&entry.text)?;
            r.literal("\n")?;
        }
        2 => {
            r.literal("<SYNC Start=")?;
            r.integer(n.start_ms, 0)?;
            r.literal("><P Class=UNKNOWNCC>\n")?;
            r.string(&entry.text)?;
            r.literal("\n<SYNC Start=")?;
            r.integer(n.end_ms, 0)?;
            r.literal("><P Class=UNKNOWNCC>&nbsp;\n")?;
        }
        3 => {
            r.literal("{")?;
            r.time(n.start_ms, 0, None)?;
            r.literal("}{")?;
            r.time(n.end_ms, 0, None)?;
            r.literal("}")?;
            r.string(&entry.text)?;
            r.literal("\n")?;
        }
        4 | 5 => {
            r.literal("Dialogue: ")?;
            if format == 4 {
                r.literal("Marked=0")?;
            } else {
                r.integer(entry.layer, 0)?;
            }
            r.literal(",")?;
            r.time(n.start_ms, 0, Some(('.', 2)))?;
            r.literal(",")?;
            r.time(n.end_ms, 0, Some(('.', 2)))?;
            for value in [&entry.style, &entry.actor] {
                r.literal(",")?;
                r.string(value)?;
            }
            for value in [entry.margins[0], entry.margins[2], n.margin_v] {
                r.literal(",")?;
                r.integer(value, 4)?;
            }
            for value in [&entry.effect, &entry.text] {
                r.literal(",")?;
                r.string(value)?;
            }
            r.literal("\n")?;
        }
        _ => {}
    }
    Ok(r.0)
}
#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;
    fn entry() -> PreparedEntry {
        PreparedEntry {
            text: "Text".encode_utf16().collect(),
            style: "Style".encode_utf16().collect(),
            actor: "Actor".encode_utf16().collect(),
            effect: "Effect".encode_utf16().collect(),
            unicode: true,
            margins: [-7, 99, 12345, 88],
            layer: -2,
            start: 0,
            end: 0,
            readorder: 0,
        }
    }
    fn numbers() -> Numbers {
        Numbers {
            sequence: 12,
            start_ms: 3723004,
            end_ms: -61012,
            frame_start: 42,
            frame_end: -9,
            margin_v: -13,
        }
    }
    #[test]
    fn all_six_pinned_templates_and_signed_components() {
        let expected=[
            "12\n01:02:03,004 --> 00:-1:-1,-12\nText\n\n",
            "{42}{-9}Text\n",
            "<SYNC Start=3723004><P Class=UNKNOWNCC>\nText\n<SYNC Start=-61012><P Class=UNKNOWNCC>&nbsp;\n",
            "{1:02:03}{0:-1:-1}Text\n",
            "Dialogue: Marked=0,1:02:03.00,0:-1:-1.-1,Style,Actor,-007,12345,-013,Effect,Text\n",
            "Dialogue: -2,1:02:03.00,0:-1:-1.-1,Style,Actor,-007,12345,-013,Effect,Text\n",
        ];
        let e = entry();
        let original = e.clone();
        for (format, text) in expected.iter().enumerate() {
            assert_eq!(
                format_row(format as u32, numbers(), &e).unwrap(),
                text.encode_utf16().collect::<Vec<_>>()
            );
        }
        assert_eq!(e, original);
    }
    #[test]
    fn minimum_integer_and_widths_are_not_truncating() {
        let mut r = Row(Vec::new());
        r.integer(i32::MIN, 4).unwrap();
        r.literal(",").unwrap();
        r.integer(-1, 4).unwrap();
        r.literal(",").unwrap();
        r.integer(12345, 4).unwrap();
        assert_eq!(
            r.0,
            "-2147483648,-001,12345".encode_utf16().collect::<Vec<_>>()
        );
        let n = Numbers {
            sequence: i32::MIN,
            start_ms: i32::MAX,
            end_ms: i32::MIN,
            ..numbers()
        };
        let row = format_row(0, n, &entry()).unwrap();
        assert_eq!(
            row,
            "-2147483648\n596:31:23,647 --> -596:-31:-23,-648\nText\n\n"
                .encode_utf16()
                .collect::<Vec<_>>()
        );
    }
    #[test]
    fn raw_prefixes_preserve_surrogates_without_preparing_newlines() {
        let mut e = entry();
        e.text = vec![0xd800, 13, 10, 0, 65];
        e.style = vec![0, 66];
        e.actor = vec![0xdc00, 0, 67];
        e.effect = vec![0xdfff];
        let row = format_row(5, numbers(), &e).unwrap();
        let mut expected = "Dialogue: -2,1:02:03.00,0:-1:-1.-1,,"
            .encode_utf16()
            .collect::<Vec<_>>();
        expected.extend([0xdc00]);
        expected.extend(",-007,12345,-013,".encode_utf16());
        expected.extend([0xdfff, 44, 0xd800, 13, 10, 10]);
        assert_eq!(row, expected);
    }
    #[test]
    fn native_string_count_bound_without_large_allocation() {
        let maximum =
            (i32::MAX as usize).min(isize::MAX as usize / core::mem::size_of::<u16>() - 1);
        assert_eq!(checked_len(maximum - 1, 1), Ok(maximum));
        assert_eq!(checked_len(maximum, 1), Err(Error::CountOverflow));
        assert_eq!(checked_len(usize::MAX, 1), Err(Error::CountOverflow));
    }
    #[test]
    fn unsupported_valid_formats_empty_and_private_invalid_refused() {
        for format in 6..14 {
            assert!(format_row(format, numbers(), &entry()).unwrap().is_empty());
        }
        assert_eq!(
            format_row(14, numbers(), &entry()),
            Err(Error::InvalidFormat)
        );
        assert_eq!(
            format_row(u32::MAX, numbers(), &entry()),
            Err(Error::InvalidFormat)
        );
    }
}
