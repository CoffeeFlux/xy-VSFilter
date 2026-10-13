// SPDX-License-Identifier: GPL-3.0-or-later
//! DVB pixel-code strings. Native code retains coordinate references, width
//! checks, dirty rectangles and final byte alignment; Rust decodes each run.

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    Two,
    Four,
    Eight,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Request {
    Done,
    Eof,
    Bits(u8),
    Byte,
    Run { count: i16, palette: u8 },
}

#[derive(Clone, Copy)]
enum Phase {
    Done,
    Eof,
    Leading,
    Switch1,
    Switch2,
    Switch3,
    Length,
    Palette,
    Run,
}

pub struct Decoder {
    mode: Mode,
    phase: Phase,
    count: i16,
    palette: u8,
    quit: bool,
    length_bits: u8,
    base: i16,
    palette_next: bool,
    zero_quits: bool,
}

impl Decoder {
    pub fn new(mode: Mode) -> Self {
        Self {
            mode,
            phase: Phase::Eof,
            count: 0,
            palette: 0,
            quit: false,
            length_bits: 0,
            base: 0,
            palette_next: false,
            zero_quits: false,
        }
    }

    pub fn request(&self) -> Request {
        match self.phase {
            Phase::Done => Request::Done,
            Phase::Eof => Request::Eof,
            Phase::Leading | Phase::Palette => match self.mode {
                Mode::Two => Request::Bits(2),
                Mode::Four => Request::Bits(4),
                Mode::Eight => Request::Byte,
            },
            Phase::Switch1 | Phase::Switch2 => Request::Bits(1),
            Phase::Switch3 => Request::Bits(2),
            Phase::Length => {
                if self.length_bits == 8 {
                    Request::Byte
                } else {
                    Request::Bits(self.length_bits)
                }
            }
            Phase::Run => Request::Run {
                count: self.count,
                palette: self.palette,
            },
        }
    }

    /// Supply native EOF, bit/byte result or run completion. Zero-count runs
    /// are still returned: the native width check occurs even when quitting.
    pub fn supply(&mut self, value: u8) {
        match self.phase {
            Phase::Done => (),
            Phase::Eof => {
                self.count = 0;
                self.palette = 0;
                self.phase = if value != 0 {
                    Phase::Done
                } else {
                    Phase::Leading
                };
            }
            Phase::Leading => {
                if value == 0 {
                    self.phase = Phase::Switch1;
                } else {
                    self.palette = value;
                    self.run(1);
                }
            }
            Phase::Switch1 => match self.mode {
                Mode::Two if value == 1 => self.length(3, 3, true, false),
                Mode::Four if value == 0 => self.length(3, 2, false, true),
                Mode::Eight => self.length(7, 0, value != 0, value == 0),
                _ => self.phase = Phase::Switch2,
            },
            Phase::Switch2 => match self.mode {
                Mode::Two if value != 0 => self.run(1),
                Mode::Four if value == 0 => self.length(2, 4, true, false),
                _ => self.phase = Phase::Switch3,
            },
            Phase::Switch3 => match (self.mode, value) {
                (Mode::Two, 0) => {
                    self.quit = true;
                    self.run(0);
                }
                (Mode::Four, 0) => self.run(1),
                (_, 1) => self.run(2),
                (Mode::Two, 2) => self.length(4, 12, true, false),
                (Mode::Four, 2) => self.length(4, 9, true, false),
                (Mode::Two, 3) => self.length(8, 29, true, false),
                (Mode::Four, 3) => self.length(8, 25, true, false),
                _ => self.phase = Phase::Done,
            },
            Phase::Length => {
                self.count = self.base + i16::from(value);
                if self.zero_quits && value == 0 {
                    self.count = 0;
                    self.quit = true;
                }
                self.phase = if self.palette_next {
                    Phase::Palette
                } else {
                    Phase::Run
                };
            }
            Phase::Palette => {
                self.palette = value;
                self.phase = Phase::Run;
            }
            Phase::Run => self.phase = if self.quit { Phase::Done } else { Phase::Eof },
        }
    }

    fn run(&mut self, count: i16) {
        self.count = count;
        self.phase = Phase::Run;
    }

    fn length(&mut self, bits: u8, base: i16, palette_next: bool, zero_quits: bool) {
        self.length_bits = bits;
        self.base = base;
        self.palette_next = palette_next;
        self.zero_quits = zero_quits;
        self.phase = Phase::Length;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn explicit_zero_eight_bit_run_consumes_palette_and_continues() {
        let mut d = Decoder::new(Mode::Eight);
        for value in [0, 0, 1, 0, 255] {
            d.supply(value);
        }
        assert_eq!(
            d.request(),
            Request::Run {
                count: 0,
                palette: 255
            }
        );
        d.supply(0);
        assert_eq!(d.request(), Request::Eof);
    }
    #[test]
    fn quit_still_returns_zero_run_before_alignment() {
        let mut d = Decoder::new(Mode::Four);
        for value in [0, 0, 0, 0] {
            d.supply(value);
        }
        assert_eq!(
            d.request(),
            Request::Run {
                count: 0,
                palette: 0
            }
        );
        d.supply(0);
        assert_eq!(d.request(), Request::Done);
    }
}
