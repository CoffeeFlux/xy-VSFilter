// SPDX-License-Identifier: GPL-3.0-or-later
//! `CompositionObject::RenderHdmv` run decoding. Native code supplies EOF/byte
//! results and paints requested rectangles, in original call order. Palette,
//! RLE backing storage and pixel-format handling remain outside this machine.

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Request {
    Done,
    Eof,
    Byte,
    Paint {
        x: i16,
        y: i16,
        width: i16,
        palette: u8,
    },
}

#[derive(Clone, Copy)]
enum Phase {
    Done,
    Eof,
    Leading,
    Switch,
    Length,
    Palette,
    Paint,
}

pub struct Decoder {
    x_start: i16,
    y_end: i32,
    x: i16,
    y: i16,
    count: i16,
    palette: u8,
    explicit_palette: bool,
    phase: Phase,
}

impl Decoder {
    pub fn new(x: i16, y: i16, height: i16) -> Self {
        let mut decoder = Self {
            x_start: x,
            y_end: i32::from(y) + i32::from(height),
            x,
            y,
            count: 0,
            palette: 0,
            explicit_palette: false,
            phase: Phase::Done,
        };
        decoder.begin();
        decoder
    }

    pub fn request(&self) -> Request {
        match self.phase {
            Phase::Done => Request::Done,
            Phase::Eof => Request::Eof,
            Phase::Leading | Phase::Switch | Phase::Length | Phase::Palette => Request::Byte,
            Phase::Paint => Request::Paint {
                x: self.x,
                y: self.y,
                width: self.count,
                palette: self.palette,
            },
        }
    }

    /// Supply a native byte, EOF boolean (0/1), or paint completion (ignored).
    pub fn supply(&mut self, value: u8) {
        match self.phase {
            Phase::Done => (),
            Phase::Eof => {
                self.phase = if value != 0 {
                    Phase::Done
                } else {
                    Phase::Leading
                };
            }
            Phase::Leading => {
                if value == 0 {
                    self.phase = Phase::Switch;
                } else {
                    self.palette = value;
                    self.count = 1;
                    self.finish_run();
                }
            }
            Phase::Switch => {
                self.explicit_palette = value & 0x80 != 0;
                self.count = i16::from(value & 0x3f);
                if value & 0x40 != 0 {
                    self.count <<= 8;
                    self.phase = Phase::Length;
                } else if self.explicit_palette {
                    self.phase = Phase::Palette;
                } else {
                    if self.count > 0 {
                        self.palette = 0;
                    }
                    self.finish_run();
                }
            }
            Phase::Length => {
                self.count |= i16::from(value);
                if self.explicit_palette {
                    self.phase = Phase::Palette;
                } else {
                    self.palette = 0;
                    self.finish_run();
                }
            }
            Phase::Palette => {
                self.palette = value;
                self.finish_run();
            }
            Phase::Paint => self.advance_x(),
        }
    }

    fn begin(&mut self) {
        self.phase = if i32::from(self.y) < self.y_end {
            Phase::Eof
        } else {
            Phase::Done
        };
    }

    fn finish_run(&mut self) {
        if self.count > 0 {
            if self.palette == 0xff {
                self.advance_x();
            } else {
                self.phase = Phase::Paint;
            }
        } else {
            self.y = self.y.wrapping_add(1);
            self.x = self.x_start;
            self.begin();
        }
    }

    fn advance_x(&mut self) {
        self.x = self.x.wrapping_add(self.count);
        self.begin();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn truncated_explicit_run_still_requests_palette_before_drawing() {
        let mut d = Decoder::new(3, 4, 1);
        assert_eq!(d.request(), Request::Eof);
        d.supply(0);
        for byte in [0, 0xc1, 0, 0] {
            assert_eq!(d.request(), Request::Byte);
            d.supply(byte);
        }
        assert_eq!(
            d.request(),
            Request::Paint {
                x: 3,
                y: 4,
                width: 256,
                palette: 0
            }
        );
        d.supply(0);
        assert_eq!(d.request(), Request::Eof);
        d.supply(1);
        assert_eq!(d.request(), Request::Done);
    }

    #[test]
    fn transparent_runs_advance_wrapped_coordinates_without_painting() {
        let mut d = Decoder::new(i16::MAX, 0, 1);
        d.supply(0);
        d.supply(255);
        assert_eq!(d.request(), Request::Eof);
        d.supply(0);
        d.supply(7);
        assert_eq!(
            d.request(),
            Request::Paint {
                x: i16::MIN,
                y: 0,
                width: 1,
                palette: 7
            }
        );
    }
}
