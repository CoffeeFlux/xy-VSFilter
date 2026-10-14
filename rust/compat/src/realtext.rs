// SPDX-License-Identifier: GPL-3.0-or-later
//! Owned raw UTF-16 output for the pinned RealText RenderTags routine.
use alloc::vec::Vec;

fn is_name(name: &[u16], ascii: &[u8]) -> bool {
    name.len() == ascii.len() && name.iter().zip(ascii).all(|(&a, &b)| a == u16::from(b))
}

pub struct Tags {
    output: Vec<u16>,
    ignore: u32,
    minimum: i32,
    maximum: i32,
    has_text: bool,
    attributes: bool,
    close_font: bool,
}

impl Tags {
    /// Ignore bits: font, size, color, weight (also italic), face.
    pub fn new(ignore: u32, minimum: i32, maximum: i32) -> Self {
        Self {
            output: Vec::new(),
            ignore,
            minimum,
            maximum,
            has_text: false,
            attributes: false,
            close_font: false,
        }
    }

    fn literal(&mut self, ascii: &[u8]) {
        self.output.extend(ascii.iter().copied().map(u16::from));
    }

    /// Flags are open=1, close=2, text=4. A true result requests the native
    /// ordered attribute iteration, followed by end_font().
    pub fn begin(&mut self, name: &[u16], flags: u32) -> bool {
        self.attributes = false;
        self.close_font = false;
        if is_name(name, b"br") {
            self.literal(b"\n");
        } else if is_name(name, b"b") || is_name(name, b"i") {
            if self.ignore & 8 == 0 {
                if flags & 1 != 0 {
                    self.literal(if is_name(name, b"b") { b"<b>" } else { b"<i>" });
                } else if flags & 2 != 0 {
                    self.literal(if is_name(name, b"b") {
                        b"</b>"
                    } else {
                        b"</i>"
                    });
                }
            }
        } else if is_name(name, b"font") {
            if self.ignore & 1 == 0 {
                if flags & 1 != 0 {
                    self.literal(b"<font");
                    self.attributes = true;
                    self.close_font = flags & 2 != 0;
                    return true;
                }
                if flags & 2 != 0 {
                    self.literal(b"</font>");
                }
            }
        } else if flags & 4 != 0 {
            self.output.extend_from_slice(name);
            self.has_text |= !name.is_empty();
        }
        false
    }

    /// Zero skips the attribute before native classification/conversion; two
    /// requests the original iswdigit/_wtoi check for a size attribute.
    pub fn attribute_kind(&self, name: &[u16]) -> u32 {
        if !self.attributes {
            return 0;
        }
        if is_name(name, b"size") {
            if self.ignore & 2 != 0 {
                0
            } else {
                2
            }
        } else if (self.ignore & 4 != 0 && is_name(name, b"color"))
            || (self.ignore & 16 != 0 && is_name(name, b"face"))
        {
            0
        } else {
            1
        }
    }

    pub fn attribute(&mut self, name: &[u16], value: &[u16], numeric: bool, size: i32) {
        let kind = self.attribute_kind(name);
        if kind == 0
            || (kind == 2 && numeric && ((size > 0 && size < self.minimum) || size > self.maximum))
        {
            return;
        }
        self.literal(b" ");
        self.output.extend_from_slice(name);
        self.literal(b"=\"");
        self.output.extend_from_slice(value);
        self.literal(b"\"");
    }

    pub fn end_font(&mut self) {
        if self.attributes {
            self.literal(b">");
            if self.close_font {
                self.literal(b"</font>");
            }
            self.attributes = false;
            self.close_font = false;
        }
    }

    pub fn result(&self) -> &[u16] {
        if self.has_text {
            &self.output
        } else {
            &[]
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn units(s: &str) -> Vec<u16> {
        s.encode_utf16().collect()
    }
    #[test]
    fn formatting_only_is_empty_and_open_close_rules_differ_by_tag() {
        let mut tags = Tags::new(0, 14, 25);
        assert!(!tags.begin(&units("b"), 3));
        assert!(tags.result().is_empty());
        assert!(tags.begin(&units("font"), 3));
        tags.attribute(&units("size"), &units("13"), true, 13);
        tags.attribute(&units("size"), &units("-1"), false, 0);
        tags.end_font();
        tags.begin(&[0xd800, 0], 4);
        let mut expected = units("<b><font size=\"-1\"></font>");
        expected.extend([0xd800, 0]);
        assert_eq!(tags.result(), expected);
    }
}
