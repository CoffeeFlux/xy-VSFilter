// SPDX-License-Identifier: GPL-3.0-or-later
//! RealText text extraction; native output and source updates stay ordered.
use alloc::vec::Vec;

#[derive(Clone, Copy)]
enum Phase {
    Initial,
    Space,
    Scan,
    AfterAppend(bool),
    Done(bool),
}
pub struct Text {
    phase: Phase,
    waiting_space: bool,
    pub position: u32,
    pub character: u16,
    pub text: Vec<u16>,
}
impl Default for Text {
    fn default() -> Self {
        Self::new()
    }
}
impl Text {
    pub fn new() -> Self {
        Self {
            phase: Phase::Initial,
            waiting_space: false,
            position: 0,
            character: 0,
            text: Vec::new(),
        }
    }
    fn finish(&mut self, success: bool) -> u32 {
        self.phase = Phase::Done(success);
        u32::from(success)
    }
    /// Actions: 0/1 return false/true, 2 classify native whitespace, 3 clear
    /// output, 4 append owned text, 5 consume source prefix through position.
    pub fn next(&mut self, input: &[u16], ignore: bool, response: bool) -> u32 {
        if self.waiting_space {
            self.waiting_space = false;
            if response {
                self.position = self.position.wrapping_add(1);
            } else {
                if self.position as usize >= input.len() {
                    return self.finish(false);
                }
                self.phase = Phase::Scan;
            }
        }
        loop {
            match self.phase {
                Phase::Initial => {
                    if input.is_empty() || input.first() == Some(&60) {
                        if !ignore {
                            return self.finish(false);
                        }
                        self.phase = Phase::Done(true);
                        return 3;
                    }
                    self.phase = Phase::Space;
                }
                Phase::Space => {
                    let Some(&unit) = input.get(self.position as usize) else {
                        return self.finish(false);
                    };
                    self.character = unit;
                    self.waiting_space = true;
                    return 2;
                }
                Phase::Scan => {
                    while let Some(&unit) = input.get(self.position as usize) {
                        if unit == 60 {
                            break;
                        }
                        self.text.push(unit);
                        self.position = self.position.wrapping_add(1);
                    }
                    self.phase = Phase::AfterAppend((self.position as usize) < input.len());
                    return 4;
                }
                Phase::AfterAppend(complete) => {
                    if !complete {
                        return self.finish(false);
                    }
                    self.phase = Phase::Done(true);
                    return 5;
                }
                Phase::Done(success) => return u32::from(success),
            }
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn eof_appends_text_but_does_not_consume_source() {
        let input = [97u16, 0, 0xd800];
        let mut text = Text::new();
        assert_eq!(text.next(&input, true, false), 2);
        assert_eq!(text.next(&input, true, false), 4);
        assert_eq!(text.text, input);
        assert_eq!(text.next(&input, true, false), 0);
        assert_eq!(text.position, 3);
    }
}
