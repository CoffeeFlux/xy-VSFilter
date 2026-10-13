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

/// Field dispatch deliberately uses the declared signed length, without an EOF
/// clamp. Mapping tables are skipped, and each line marker advances two rows.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FieldRequest {
    Done,
    Position,
    Byte,
    Pixels(Mode),
    Skip(u8),
    Unknown,
}

pub struct FieldDecoder {
    end: i32,
    start_x: i16,
    x: i16,
    y: i16,
    request: FieldRequest,
}

impl FieldDecoder {
    pub fn new(position: i32, length: i16, x: i16, y: i16) -> Self {
        Self {
            end: position.wrapping_add(i32::from(length)),
            start_x: x,
            x,
            y,
            request: FieldRequest::Position,
        }
    }

    pub fn request(&self) -> FieldRequest {
        self.request
    }

    pub fn coordinates(&self) -> (i16, i16) {
        (self.x, self.y)
    }

    /// Supplies a position, byte, or completed native action. Coordinates are
    /// copied back only after a pixel decoder returns; native callbacks never
    /// execute while this state is borrowed.
    pub fn supply(&mut self, value: i32, x: i16, y: i16) {
        self.request = match self.request {
            FieldRequest::Done => FieldRequest::Done,
            FieldRequest::Position if value < self.end => FieldRequest::Byte,
            FieldRequest::Position => FieldRequest::Done,
            FieldRequest::Byte => match value {
                0x10 => FieldRequest::Pixels(Mode::Two),
                0x11 => FieldRequest::Pixels(Mode::Four),
                0x12 => FieldRequest::Pixels(Mode::Eight),
                0x20 => FieldRequest::Skip(2),
                0x21 => FieldRequest::Skip(4),
                0x22 => FieldRequest::Skip(16),
                0xf0 => {
                    self.x = self.start_x;
                    self.y = self.y.wrapping_add(2);
                    FieldRequest::Position
                }
                _ => FieldRequest::Unknown,
            },
            FieldRequest::Pixels(_) => {
                self.x = x;
                self.y = y;
                FieldRequest::Position
            }
            FieldRequest::Skip(_) | FieldRequest::Unknown => FieldRequest::Position,
        };
    }
}

#[cfg(test)]
mod field_tests {
    use super::*;

    #[test]
    fn marker_resets_x_and_wraps_alternate_row() {
        let mut d = FieldDecoder::new(4, 8, -2, i16::MAX);
        d.supply(4, 0, 0);
        d.supply(0x12, 0, 0);
        assert_eq!(d.request(), FieldRequest::Pixels(Mode::Eight));
        d.supply(0, 123, i16::MAX);
        d.supply(7, 0, 0);
        d.supply(0xf0, 0, 0);
        assert_eq!(d.coordinates(), (-2, i16::MIN + 1));
        d.supply(12, 0, 0);
        assert_eq!(d.request(), FieldRequest::Done);
    }

    #[test]
    fn mapping_skip_is_not_clamped_to_remaining_field() {
        let mut d = FieldDecoder::new(4, 1, 0, 0);
        d.supply(4, 0, 0);
        d.supply(0x22, 0, 0);
        assert_eq!(d.request(), FieldRequest::Skip(16));
        d.supply(0, 0, 0);
        d.supply(21, 0, 0);
        assert_eq!(d.request(), FieldRequest::Done);
    }
}
