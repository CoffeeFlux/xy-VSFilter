// SPDX-License-Identifier: GPL-3.0-or-later
//! RealText tag extraction, with native output patches and callback checkpoints.
use alloc::vec::Vec;

#[derive(Clone, Copy)]
enum Phase {
    Initial,
    AfterPrefix,
    Lead,
    Space,
    OpenClose,
    Name,
    AfterName(bool),
    Attributes,
    AfterAttributes,
    End,
    CommentName,
    CommentTail,
    Done(bool),
}

pub struct Tag {
    phase: Phase,
    waiting_space: bool,
    waiting_attributes: bool,
    pub position: u32,
    pub patch: u32,
    pub character: u16,
    pub consume: usize,
    pub text: Vec<u16>,
}
impl Default for Tag {
    fn default() -> Self {
        Self::new()
    }
}
impl Tag {
    pub fn new() -> Self {
        Self {
            phase: Phase::Initial,
            waiting_space: false,
            waiting_attributes: false,
            position: 1,
            patch: 0,
            character: 0,
            consume: 0,
            text: Vec::new(),
        }
    }
    fn finish(&mut self, success: bool) -> u32 {
        self.phase = Phase::Done(success);
        u32::from(success)
    }
    fn patch(&mut self, mask: u32, value: u32) {
        self.patch |= mask;
        self.patch = (self.patch & !(mask << 8)) | ((value & mask) << 8);
    }

    /// Actions: 0/1 finish false/true, 2 native whitespace, 3 lowercase name,
    /// 4 parse attributes, 5 append name, 6 replace name, 7 consume source prefix,
    /// 8 native checked read at position (preserving its exception).
    /// Patch bits (mask low byte, values next byte): comment=1, open=2, close=4.
    pub fn next(&mut self, input: &[u16], ignore: bool, response: bool, position: u32) -> u32 {
        self.patch = 0;
        if self.waiting_space {
            self.waiting_space = false;
            if response {
                self.position = self.position.wrapping_add(1);
            } else {
                if self.position as usize >= input.len() {
                    return self.finish(false);
                }
                self.phase = Phase::OpenClose;
            }
        }
        if self.waiting_attributes {
            self.waiting_attributes = false;
            self.position = position;
            if !response {
                return self.finish(false);
            }
        }
        loop {
            let pos = self.position as usize;
            match self.phase {
                Phase::Initial => {
                    if input.len() < 2 || input.first() != Some(&60) {
                        if !ignore {
                            return self.finish(false);
                        }
                        if let Some(index) = input.iter().position(|&unit| unit == 60) {
                            self.consume = index;
                            self.phase = Phase::AfterPrefix;
                            return 7;
                        }
                    }
                    self.phase = Phase::Lead;
                }
                Phase::AfterPrefix => {
                    if input.len() < 2 {
                        return self.finish(false);
                    }
                    self.phase = Phase::Lead;
                }
                Phase::Lead => {
                    let Some(&unit) = input.get(pos) else {
                        return 8;
                    };
                    if unit == 33 {
                        self.patch(1, 1);
                        self.text.clear();
                        self.phase = Phase::CommentName;
                    } else {
                        self.patch(1, 0);
                        self.phase = Phase::Space;
                    }
                }
                Phase::Space => {
                    let Some(&unit) = input.get(pos) else {
                        return self.finish(false);
                    };
                    self.character = unit;
                    self.waiting_space = true;
                    return 2;
                }
                Phase::OpenClose => {
                    let Some(&unit) = input.get(pos) else {
                        return 8;
                    };
                    if unit == 47 {
                        self.patch(6, 4);
                        self.position = self.position.wrapping_add(1);
                    } else {
                        self.patch(6, 2);
                    }
                    self.text.clear();
                    self.phase = Phase::Name;
                }
                Phase::Name | Phase::CommentName => {
                    let comment = matches!(self.phase, Phase::CommentName);
                    while let Some(&unit) = input.get(self.position as usize) {
                        if (comment && unit == 62)
                            || (!comment && [13, 10, 9, 32, 47, 62].contains(&unit))
                        {
                            break;
                        }
                        self.text.push(unit);
                        self.position = self.position.wrapping_add(1);
                    }
                    if comment {
                        self.phase = Phase::CommentTail;
                        return 6;
                    }
                    self.phase = Phase::AfterName((self.position as usize) < input.len());
                    return 5;
                }
                Phase::AfterName(complete) => {
                    if !complete {
                        return self.finish(false);
                    }
                    self.phase = Phase::Attributes;
                    return 3;
                }
                Phase::Attributes => {
                    self.waiting_attributes = true;
                    self.phase = Phase::AfterAttributes;
                    return 4;
                }
                Phase::AfterAttributes => {
                    let Some(&unit) = input.get(pos) else {
                        return 8;
                    };
                    if unit == 47 {
                        self.position = self.position.wrapping_add(1);
                        self.patch(4, 4);
                        if self.position as usize >= input.len() {
                            return self.finish(false);
                        }
                    }
                    self.phase = Phase::End;
                }
                Phase::End => {
                    let Some(&unit) = input.get(pos) else {
                        return 8;
                    };
                    if unit == 62 {
                        self.position = self.position.wrapping_add(1);
                        self.consume = self.position as usize;
                    } else {
                        if !ignore {
                            return self.finish(false);
                        }
                        let Some(index) = input.iter().position(|&unit| unit == 62) else {
                            return self.finish(false);
                        };
                        if index == 0 {
                            return self.finish(false);
                        }
                        self.consume = index + 1;
                    }
                    self.phase = Phase::Done(true);
                    return 7;
                }
                Phase::CommentTail => {
                    self.position = self.position.wrapping_add(1);
                    if self.position as usize >= input.len() {
                        return self.finish(false);
                    }
                    self.consume = self.position as usize;
                    self.phase = Phase::Done(true);
                    return 7;
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
    fn comments_keep_the_bang_and_refuse_a_delimiter_at_eof() {
        let input: Vec<u16> = "<!note>".encode_utf16().collect();
        let mut tag = Tag::new();
        assert_eq!(tag.next(&input, true, false, 0), 6);
        assert_eq!(tag.patch, 0x101);
        assert!(tag.text.iter().copied().eq("!note".encode_utf16()));
        assert_eq!(tag.next(&input, true, false, 0), 0);
        let input: Vec<u16> = "<!note>x".encode_utf16().collect();
        let mut tag = Tag::new();
        assert_eq!(tag.next(&input, true, false, 0), 6);
        assert_eq!(tag.next(&input, true, false, 0), 7);
        assert_eq!(tag.consume, 7);
    }
}
