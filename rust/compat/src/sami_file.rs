// SPDX-License-Identifier: GPL-2.0-or-later
//! SAMI file comment state, caption ownership and start times. Native uppercase,
//! timestamp digit callbacks/arithmetic, text conversion and Add remain ordered.
use alloc::vec::Vec;
#[derive(Default)]
pub struct File {
    caption: Vec<u16>,
    comment: bool,
    close_comment: bool,
    pub start: i32,
}
pub(crate) fn contains(input: &[u16], needle: &[u8]) -> bool {
    input
        .windows(needle.len())
        .any(|part| part.iter().zip(needle).all(|(&a, &b)| a == u16::from(b)))
}
impl File {
    /// The native caller has uppercased a fresh line. Actions: 0 ignore,
    /// 1 append, 2 emit the previous caption and then append this sync line.
    fn classified(&self, upper: &[u16]) -> (bool, bool, u32) {
        let upper = &upper[..upper
            .iter()
            .position(|&unit| unit == 0)
            .unwrap_or(upper.len())];
        let comment = self.comment || contains(upper, b"<!--") || contains(upper, b"<TITLE>");
        let close_comment = contains(upper, b"-->") || contains(upper, b"</TITLE>");
        let action = if comment {
            0
        } else if contains(upper, b"<SYNC START=") {
            2
        } else {
            1
        };
        (comment, close_comment, action)
    }
    /// Inspect the action before reserving bounded caption growth.
    pub fn action(&self, upper: &[u16]) -> u32 {
        self.classified(upper).2
    }
    pub fn begin(&mut self, upper: &[u16]) -> u32 {
        let (comment, close_comment, action) = self.classified(upper);
        self.comment = comment;
        self.close_comment = close_comment;
        action
    }
    pub fn flushed(&mut self, time: i32) {
        self.start = time;
        self.caption.clear();
    }
    pub fn append(&mut self, fragment: &[u16]) {
        self.caption.extend_from_slice(fragment);
    }
    pub fn end(&mut self) {
        if self.close_comment {
            self.comment = false;
        }
    }
    pub fn text(&self) -> &[u16] {
        &self.caption
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    fn units(text: &str) -> Vec<u16> {
        text.encode_utf16().collect()
    }
    #[test]
    fn same_line_title_and_comment_close_skip_the_whole_line() {
        let mut file = File::default();
        assert_eq!(file.begin(&units("<TITLE>X</TITLE><SYNC START=1>")), 0);
        file.end();
        assert_eq!(file.begin(&units("PREFIX<SYNC START=2>")), 2);
        file.append(&[65]);
        file.flushed(2);
        file.append(&[66]);
        file.end();
        assert_eq!(file.start, 2);
        assert_eq!(file.text(), &[66]);
        assert_eq!(file.begin(&units("<!--")), 0);
        file.end();
        assert_eq!(file.begin(&units("<SYNC START=3>-->")), 0);
        file.end();
        assert_eq!(file.begin(&units("X")), 1);
    }
    #[test]
    fn close_markers_after_nul_do_not_end_the_comment() {
        let mut file = File::default();
        file.begin(&units("<!--\0-->"));
        file.end();
        assert_eq!(file.begin(&units("<SYNC START=1>")), 0);
        file.append(&[0, 0xd800]);
        assert_eq!(file.text(), &[0, 0xd800]);
    }
}
