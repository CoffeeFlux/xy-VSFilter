// SPDX-License-Identifier: GPL-2.0-or-later
//! MicroDVD line/tag sequencing, output ownership and ordered style restoration.
use alloc::vec::Vec;
pub struct Converter {
    length: i32,
    position: i32,
    line_length: i32,
    index: i32,
    open: i32,
    close: i32,
    phase: u8,
    pub start: i32,
    pub count: i32,
    pub restore: u32,
    pub output: Vec<u16>,
}
impl Converter {
    pub fn new(length: i32) -> Self {
        Self {
            length,
            position: 0,
            line_length: 0,
            index: 0,
            open: 0,
            close: 0,
            phase: 0,
            start: 0,
            count: 0,
            restore: 0,
            output: Vec::new(),
        }
    }
    pub fn append(&mut self, text: &[u16], restore: u32) {
        self.output.extend_from_slice(text);
        self.restore = restore & 255;
    }
    /// 0 done, 1 find pipe in source, 2 native line slice (reply=line length),
    /// 3 find open brace, 4 append native literal slice, 5 find close brace,
    /// 6 native tag conversion followed by append with its updated restore mask.
    pub fn step(&mut self, reply: i32) -> u32 {
        loop {
            match self.phase {
                0 => {
                    if self.position >= self.length {
                        self.phase = 9;
                        return 0;
                    }
                    self.start = self.position;
                    self.phase = 1;
                    return 1;
                }
                1 => {
                    let end = if reply < 0 { self.length } else { reply };
                    self.start = self.position;
                    self.count = end - self.position;
                    self.position = end;
                    self.phase = 2;
                    return 2;
                }
                2 => {
                    self.line_length = reply;
                    self.index = 0;
                    self.phase = 3;
                }
                3 => {
                    if self.index >= self.line_length {
                        self.phase = 8;
                        continue;
                    }
                    self.start = self.index;
                    self.phase = 4;
                    return 3;
                }
                4 => {
                    // The reference uses the WHOLE source length on this miss.
                    self.open = if reply < 0 { self.length } else { reply };
                    self.start = self.index;
                    self.count = self.open - self.index;
                    self.phase = 5;
                    return 4;
                }
                5 => {
                    if self.open >= self.line_length {
                        self.phase = 8;
                        continue;
                    }
                    self.start = self.open;
                    self.phase = 6;
                    return 5;
                }
                6 => {
                    self.close = if reply < 0 { self.line_length } else { reply };
                    self.start = self.open;
                    self.count = self.close - self.open;
                    self.phase = 7;
                    return 6;
                }
                7 => {
                    self.index = self.close.saturating_add(1);
                    self.phase = 3;
                }
                8 => {
                    if self.position >= self.length {
                        self.phase = 9;
                        return 0;
                    }
                    for (bit, text) in [
                        b"{\\c}".as_slice(),
                        b"{\\fn}",
                        b"{\\fs}",
                        b"{\\fe}",
                        b"{\\b}",
                        b"{\\i}",
                        b"{\\u}",
                        b"{\\s}",
                    ]
                    .iter()
                    .enumerate()
                    {
                        if self.restore & (1 << bit) != 0 {
                            self.output.extend(text.iter().copied().map(u16::from));
                        }
                    }
                    self.restore = 0;
                    self.output.extend([92, 78]);
                    self.position += 1;
                    self.phase = 0;
                }
                _ => return 0,
            }
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn line_boundary_restores_in_native_order_and_keeps_trailing_break() {
        let mut c = Converter::new(1);
        assert_eq!(c.step(0), 1);
        assert_eq!(c.step(0), 2);
        c.append(&[0, 0xd800], 255);
        assert_eq!(c.step(0), 0);
        let expected: Vec<u16> = "{\\c}{\\fn}{\\fs}{\\fe}{\\b}{\\i}{\\u}{\\s}\\N"
            .encode_utf16()
            .collect();
        assert_eq!(&c.output[..2], &[0, 0xd800]);
        assert_eq!(&c.output[2..], expected);
        assert_eq!(c.restore, 0);
    }
    #[test]
    fn missing_open_brace_uses_whole_source_length() {
        let mut c = Converter::new(20);
        assert_eq!(c.step(0), 1);
        assert_eq!(c.step(3), 2);
        assert_eq!(c.step(3), 3);
        assert_eq!(c.step(-1), 4);
        assert_eq!((c.start, c.count), (0, 20));
        assert_eq!(c.step(0), 1);
        assert_eq!(c.start, 4);
    }
}
