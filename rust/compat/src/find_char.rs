// SPDX-License-Identifier: GPL-2.0-or-later
//! The legacy, charset-aware FindChar loop. Native code classifies lead bytes,
//! classifies digits and performs charset arithmetic/code-page translation.
#[derive(Default)]
pub struct Search {
    target: u16,
    position: i32,
    length: usize,
    index: usize,
    depth: usize,
    unit: u16,
    digit: usize,
    first_digit: usize,
    phase: u8,
    pub value: i32,
}
impl Search {
    pub fn new(target: u16, position: i32, length: u32) -> Self {
        Self {
            target,
            position,
            length: length as usize,
            value: -1,
            ..Self::default()
        }
    }
    /// Actions: 0 return value, 1 classify low byte, 2 zero charset,
    /// 3 classify wide digit, 4 accumulate wide digit, 5 translate charset
    /// (value=1 restores the original charset first). Each input loan ends
    /// before a native action; reply is the previous classification result.
    pub fn step(&mut self, input: &[u16], reply: bool) -> u32 {
        loop {
            match self.phase {
                0 => {
                    if self.index >= self.length {
                        self.value = -1;
                        self.phase = 6;
                        return 0;
                    }
                    self.unit = input.get(self.index).copied().unwrap_or(0);
                    self.value = i32::from(self.unit & 255);
                    self.phase = 1;
                    return 1;
                }
                1 => {
                    if reply {
                        self.index += 1;
                    } else if self.index as i64 >= i64::from(self.position)
                        && self.unit == self.target
                    {
                        self.value = self.index as i32;
                        self.phase = 6;
                        return 0;
                    }
                    if self.unit == u16::from(b'{') {
                        self.depth += 1;
                    } else if self.depth > 0 {
                        if self.unit == u16::from(b'}') {
                            self.depth -= 1;
                        } else if self.unit == u16::from(b'e')
                            && self.index >= 3
                            && self.index + 1 < self.length
                            && input.get(self.index - 2..self.index + 1) == Some(&[92, 102, 101])
                        {
                            self.first_digit = self.index + 1;
                            self.digit = self.first_digit;
                            self.phase = 2;
                            return 2;
                        }
                    }
                    self.index += 1;
                    self.phase = 0;
                }
                2 => {
                    // CString indexing at GetLength reads its initialized NUL.
                    self.value = i32::from(input.get(self.digit).copied().unwrap_or(0));
                    self.phase = 3;
                    return 3;
                }
                3 => {
                    if reply {
                        // Reload after classification, as the reference does.
                        self.value = i32::from(input.get(self.digit).copied().unwrap_or(0));
                        self.phase = 4;
                        return 4;
                    }
                    self.value = i32::from(self.digit == self.first_digit);
                    self.phase = 5;
                    return 5;
                }
                4 => {
                    self.digit += 1;
                    self.phase = 2;
                }
                5 => {
                    self.index += 1;
                    self.phase = 0;
                }
                _ => return 0,
            }
        }
    }
}
/// CString's Unicode Find begins at the supplied position, ignoring any earlier
/// NUL. It stops at the first subsequent NUL and can match the implicit sentinel,
/// but a start at or past GetLength returns -1 even when searching for NUL.
pub fn unicode(input: &[u16], target: u16, position: i32) -> i32 {
    let Ok(start) = usize::try_from(position) else {
        return -1;
    };
    if start >= input.len() {
        return -1;
    }
    for (offset, unit) in input[start..]
        .iter()
        .copied()
        .chain(core::iter::once(0))
        .enumerate()
    {
        if unit == target {
            return (start + offset) as i32;
        }
        if unit == 0 {
            break;
        }
    }
    -1
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn lead_byte_skips_match_but_still_tracks_original_brace() {
        let input = [123, 124, 124];
        let mut s = Search::new(124, -10, 3);
        assert_eq!(s.step(&input, false), 1);
        assert_eq!(s.step(&input, true), 1);
        assert_eq!(s.value, 124);
        assert_eq!(s.step(&input, false), 0);
        assert_eq!(s.value, 2);
        assert_eq!(s.depth, 1);
    }
    #[test]
    fn charset_scan_keeps_main_cursor_and_reloads_digit() {
        let mut input: alloc::vec::Vec<u16> = "{\\fe1}|".encode_utf16().collect();
        let mut s = Search::new(124, 0, input.len() as u32);
        for _ in 0..4 {
            assert_eq!(s.step(&input, false), 1);
        }
        assert_eq!(s.step(&input, false), 2);
        assert_eq!(s.step(&input, false), 3);
        assert_eq!(s.value, 49);
        input[4] = 50;
        assert_eq!(s.step(&input, true), 4);
        assert_eq!(s.value, 50);
        assert_eq!(s.step(&input, false), 3);
        assert_eq!(s.value, 125);
        assert_eq!(s.step(&input, false), 5);
        assert_eq!(s.value, 0);
        assert_eq!(s.step(&input, false), 1);
        assert_eq!(s.value, 50);
    }
}

#[cfg(test)]
mod unicode_tests {
    use super::unicode;
    #[test]
    fn nul_before_start_is_ignored_but_nul_at_start_terminates() {
        let input = [0, 0xd800, 65, 0, 66];
        assert_eq!(unicode(&input, 65, 0), -1);
        assert_eq!(unicode(&input, 65, 1), 2);
        assert_eq!(unicode(&input, 0, 4), 5);
        assert_eq!(unicode(&input, 0, 5), -1);
        assert_eq!(unicode(&[], 0, 0), -1);
    }
}
