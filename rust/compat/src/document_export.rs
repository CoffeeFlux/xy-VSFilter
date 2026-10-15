// SPDX-License-Identifier: GPL-2.0-or-later
//! SaveAs body rows and fixed sections from pinned STS.cpp.
//! Native retains text/newline preparation, numeric casts, style rows,
//! iteration/skip and section emission policy, file writes and codepages. Raw %s fields terminate at
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
    InvalidSection,
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

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Section {
    Header = 0,
    Events = 1,
    Footer = 2,
    SidecarHeader = 3,
}
const SAMI_HEADER: &str = concat!(
    "<SAMI>\n<HEAD>\n<STYLE TYPE=\"text/css\">\n<!--\n",
    "P {margin-left: 16pt; margin-right: 16pt; margin-bottom: 16pt; margin-top: 16pt;\n",
    "   text-align: center; font-size: 18pt; font-family: arial; font-weight: bold; color: #f0f0f0;}\n",
    ".UNKNOWNCC {Name:Unknown; lang:en-US; SAMIType:CC;}\n-->\n</STYLE>\n</HEAD>\n\n<BODY>\n"
);
const SSA_STYLE_HEADER: &str = "[V4 Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, TertiaryColour, BackColour, Bold, Italic, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, AlphaLevel, Encoding\n";
const ASS_STYLE_HEADER: &str = "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n";
fn resolution(row: &mut Row, width: i32, height: i32) -> Result<(), Error> {
    row.literal("PlayResX: ")?;
    row.integer(width, 0)?;
    row.literal("\nPlayResY: ")?;
    row.integer(height, 0)?;
    row.literal("\n")
}
/// Fixed sections from pinned SaveAs. The caller retains emission order and
/// the live entry-count condition for Events; style rows remain native.
pub fn format_section(
    format: u32,
    section: u32,
    width: i32,
    height: i32,
    collisions: i32,
    scaled: bool,
) -> Result<Vec<u16>, Error> {
    if format >= 14 {
        return Err(Error::InvalidFormat);
    }
    if section >= 4 {
        return Err(Error::InvalidSection);
    }
    let mut r = Row(Vec::new());
    match section {
        0 if format == 2 => r.literal(SAMI_HEADER)?,
        0 if format == 4 || format == 5 => {
            r.literal("[Script Info]\n")?;
            r.literal(if format == 4 {
                "; This is a Sub Station Alpha v4 script.\n"
            } else {
                "; This is an Advanced Sub Station Alpha v4+ script.\n"
            })?;
            r.literal(concat!(
                "; For Sub Station Alpha info and downloads,\n",
                "; go to http://www.eswat.demon.co.uk/\n",
                "; or email kotus@eswat.demon.co.uk\n; \n"
            ))?;
            if format == 5 {
                r.literal(concat!("; Advanced Sub Station Alpha script format developed by #Anime-Fansubs@EfNET\n",
                    "; http://www.anime-fansubs.org\n; \n",
                    "; For additional info and downloads go to http://gabest.org/\n",
                    "; or email gabest@freemail.hu\n; \n"))?;
            }
            r.literal("; Note: This file was saved by Subresync.\n; \n")?;
            r.literal(if format == 4 {
                "ScriptType: v4.00\n"
            } else {
                "ScriptType: v4.00+\n"
            })?;
            r.literal(if collisions == 0 {
                "Collisions: Normal\n"
            } else {
                "Collisions: Reverse\n"
            })?;
            if format == 5 && scaled {
                r.literal("ScaledBorderAndShadow: Yes\n")?;
            }
            resolution(&mut r, width, height)?;
            r.literal("Timer: 100.0000\n\n")?;
            r.literal(if format == 4 {
                SSA_STYLE_HEADER
            } else {
                ASS_STYLE_HEADER
            })?;
        }
        1 if format == 4 || format == 5 => {
            r.literal("\n[Events]\n")?;
            r.literal(if format == 4 {
                "Format: Marked, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
            } else {
                "Format: Layer, Start, End, Style, Actor, MarginL, MarginR, MarginV, Effect, Text\n"
            })?;
        }
        2 if format == 2 => r.literal("</BODY>\n</SAMI>\n")?,
        3 => {
            r.literal("ScriptType: v4.00+\n")?;
            resolution(&mut r, width, height)?;
            r.literal("\n")?;
            r.literal(ASS_STYLE_HEADER)?;
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

    fn section_text(
        format: u32,
        section: u32,
        width: i32,
        height: i32,
        collisions: i32,
        scaled: bool,
    ) -> alloc::string::String {
        alloc::string::String::from_utf16(
            &format_section(format, section, width, height, collisions, scaled).unwrap(),
        )
        .unwrap()
    }
    #[test]
    fn exact_script_headers_and_signed_resolution() {
        let ssa = concat!(
            "[Script Info]\n; This is a Sub Station Alpha v4 script.\n",
            "; For Sub Station Alpha info and downloads,\n; go to http://www.eswat.demon.co.uk/\n",
            "; or email kotus@eswat.demon.co.uk\n; \n; Note: This file was saved by Subresync.\n; \n",
            "ScriptType: v4.00\nCollisions: Normal\nPlayResX: -2147483648\nPlayResY: 2147483647\nTimer: 100.0000\n\n",
            "[V4 Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, TertiaryColour, BackColour, Bold, Italic, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, AlphaLevel, Encoding\n"
        );
        assert_eq!(section_text(4, 0, i32::MIN, i32::MAX, 0, true), ssa);
        let ass = concat!(
            "[Script Info]\n; This is an Advanced Sub Station Alpha v4+ script.\n",
            "; For Sub Station Alpha info and downloads,\n; go to http://www.eswat.demon.co.uk/\n",
            "; or email kotus@eswat.demon.co.uk\n; \n",
            "; Advanced Sub Station Alpha script format developed by #Anime-Fansubs@EfNET\n",
            "; http://www.anime-fansubs.org\n; \n; For additional info and downloads go to http://gabest.org/\n",
            "; or email gabest@freemail.hu\n; \n; Note: This file was saved by Subresync.\n; \n",
            "ScriptType: v4.00+\nCollisions: Reverse\nScaledBorderAndShadow: Yes\nPlayResX: 0\nPlayResY: -1\nTimer: 100.0000\n\n",
            "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
        );
        assert_eq!(section_text(5, 0, 0, -1, -2, true), ass);
        assert_eq!(
            section_text(5, 0, 0, -1, 1, false),
            ass.replace("ScaledBorderAndShadow: Yes\n", "")
        );
        assert_eq!(
            section_text(4, 0, i32::MIN, i32::MAX, -1, false),
            ssa.replace("Collisions: Normal", "Collisions: Reverse")
        );
    }
    #[test]
    fn fixed_sami_events_sidecar_and_empty_sections() {
        assert_eq!(section_text(2,0,0,0,0,false),concat!(
            "<SAMI>\n<HEAD>\n<STYLE TYPE=\"text/css\">\n<!--\n",
            "P {margin-left: 16pt; margin-right: 16pt; margin-bottom: 16pt; margin-top: 16pt;\n",
            "   text-align: center; font-size: 18pt; font-family: arial; font-weight: bold; color: #f0f0f0;}\n",
            ".UNKNOWNCC {Name:Unknown; lang:en-US; SAMIType:CC;}\n-->\n</STYLE>\n</HEAD>\n\n<BODY>\n"));
        assert_eq!(section_text(2, 2, 0, 0, 0, false), "</BODY>\n</SAMI>\n");
        assert_eq!(section_text(4,1,0,0,0,false),"\n[Events]\nFormat: Marked, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
        assert_eq!(section_text(5,1,0,0,0,false),"\n[Events]\nFormat: Layer, Start, End, Style, Actor, MarginL, MarginR, MarginV, Effect, Text\n");
        let sidecar=concat!("ScriptType: v4.00+\nPlayResX: -1\nPlayResY: 0\n\n",
            "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n");
        for format in 0..14 {
            assert_eq!(section_text(format, 3, -1, 0, 7, true), sidecar);
            for section in 0..3 {
                if !matches!(
                    (format, section),
                    (2, 0) | (2, 2) | (4, 0) | (5, 0) | (4, 1) | (5, 1)
                ) {
                    assert!(format_section(format, section, 0, 0, 0, false)
                        .unwrap()
                        .is_empty());
                }
            }
        }
        assert_eq!(
            format_section(14, 0, 0, 0, 0, false),
            Err(Error::InvalidFormat)
        );
        assert_eq!(
            format_section(0, 4, 0, 0, 0, false),
            Err(Error::InvalidSection)
        );
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
