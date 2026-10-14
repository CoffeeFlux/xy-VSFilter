// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned RemoveSSATags text with native charset-aware brace search checkpoints.
use alloc::vec::Vec;
pub struct Remover {
    pub text: Vec<u16>,
    pub position: i32,
    phase: u8,
}
fn replace(text: &[u16], from: &[u8], to: &[u8]) -> Vec<u16> {
    let mut result = Vec::with_capacity(text.len());
    let mut i = 0;
    while i < text.len() {
        if text
            .get(i..i + from.len())
            .is_some_and(|part| part.iter().zip(from).all(|(&a, &b)| a == u16::from(b)))
        {
            result.extend(to.iter().copied().map(u16::from));
            i += from.len();
        } else {
            result.push(text[i]);
            i += 1;
        }
    }
    result
}
impl Remover {
    pub fn new(input: &[u16]) -> Option<Self> {
        // Leave CString replacement around embedded NULs at its native boundary.
        if input.contains(&0) {
            return None;
        }
        let text = replace(&replace(input, b"{\\i1}", b"<i>"), b"{\\i}", b"</i>");
        Some(Self {
            text,
            position: 0,
            phase: 0,
        })
    }
    fn finish(&mut self) -> u32 {
        for (from, to) in [
            (b"\\N".as_slice(), b"\n".as_slice()),
            (b"\\n", b"\n"),
            (b"\\h", b" "),
        ] {
            self.text = replace(&self.text, from, to);
        }
        self.phase = 3;
        0
    }
    /// 0 done, 1 native FindChar('{', position), 2 FindChar('}', position).
    /// reply is the requested search's result on the current complete text.
    pub fn step(&mut self, reply: i32) -> u32 {
        loop {
            match self.phase {
                0 => {
                    if self.position as usize >= self.text.len() {
                        return self.finish();
                    }
                    self.phase = 1;
                    return 1;
                }
                1 => {
                    if reply < 0 {
                        return self.finish();
                    }
                    self.position = reply;
                    self.phase = 2;
                    return 2;
                }
                2 => {
                    if reply < 0 {
                        return self.finish();
                    }
                    self.text.drain(self.position as usize..=reply as usize);
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
    fn italic_replacement_precedes_removal_and_escapes_follow_it() {
        let input: Vec<u16> = "{\\i1}{x}a{\\i}\\N\\h".encode_utf16().collect();
        let mut r = Remover::new(&input).unwrap();
        assert_eq!(r.step(0), 1);
        assert_eq!(r.step(3), 2);
        assert_eq!(r.step(5), 1);
        assert_eq!(r.position, 3);
        assert_eq!(r.step(-1), 0);
        assert!(r.text.into_iter().eq("<i>a</i>\n ".encode_utf16()));
        assert!(Remover::new(&[0, 123, 125]).is_none());
    }
    #[test]
    fn unclosed_brace_remains_and_finish_is_idempotent() {
        let mut r = Remover::new(&[123, 92, 78, 0xd800]).unwrap();
        assert_eq!(r.step(0), 1);
        assert_eq!(r.step(0), 2);
        assert_eq!(r.step(-1), 0);
        assert_eq!(r.text, alloc::vec![123, 10, 0xd800]);
        assert_eq!(r.step(0), 0);
        assert_eq!(r.text, alloc::vec![123, 10, 0xd800]);
    }
}
