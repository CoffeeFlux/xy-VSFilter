// SPDX-License-Identifier: GPL-2.0-or-later
//! SAMI tag removal and paired original/lowercase buffers after preprocessing.
use alloc::vec::Vec;
pub struct Sami {
    pub text: Vec<u16>,
    lower: Vec<u16>,
    pub arguments: Vec<u16>,
    position: usize,
    start: usize,
    span: usize,
    inserted: usize,
    pending: bool,
    done: bool,
}
impl Sami {
    pub fn new(text: &[u16], lower: &[u16]) -> Option<Self> {
        if text.len() != lower.len() || text.contains(&0) || lower.contains(&0) {
            return None;
        }
        Some(Self {
            text: text.to_vec(),
            lower: lower.to_vec(),
            arguments: Vec::new(),
            position: 0,
            start: 0,
            span: 0,
            inserted: 0,
            pending: false,
            done: false,
        })
    }
    pub fn insert(&mut self, piece: &[u16], advance: u32) {
        let at = (self.start + self.span + self.inserted).min(self.text.len());
        // Native inserts the same literal, without lowercasing it, in both copies.
        self.lower.splice(at..at, piece.iter().copied());
        self.text.splice(at..at, piece.iter().copied());
        self.inserted += advance as usize;
    }
    fn erase(&mut self) {
        let end = (self.start + self.span).min(self.text.len());
        self.text.drain(self.start..end);
        self.lower.drain(self.start..end);
        self.position = self.start + self.inserted;
    }
    /// 0 done, 1 parse the owned font arguments natively and insert each resulting
    /// color fragment, then call step again to remove the original tag.
    pub fn step(&mut self) -> u32 {
        if self.done {
            return 0;
        }
        if self.pending {
            self.erase();
            self.pending = false;
        }
        while self.position < self.text.len() {
            let Some(offset) = self.lower[self.position..].iter().position(|&c| c == 60) else {
                break;
            };
            self.start = self.position + offset;
            let mut span = 1;
            while self.start + span < self.text.len() && self.lower[self.start + span] != 62 {
                span += 1;
            }
            self.span = span + 1; // Missing '>' still adds one, even beyond the end.
            self.inserted = 0;
            let starts = |needle: &[u8]| {
                self.lower
                    .get(self.start..self.start + needle.len())
                    .is_some_and(|part| part.iter().zip(needle).all(|(&a, &b)| a == u16::from(b)))
            };
            if starts(b"<font ") {
                let begin = (self.start + 6).min(self.lower.len());
                let end = (self.start + self.span).min(self.lower.len());
                self.arguments = self.lower[begin..end].to_vec();
                self.pending = true;
                return 1;
            }
            if starts(b"</font>") {
                self.insert(&[123, 92, 99, 125], 4);
            }
            self.erase();
        }
        self.done = true;
        0
    }
}
/// Owned argument remainder and current color value. Creation receives the
/// non-NUL argument span admitted by the outer SAMI owner. Native TrimLeft runs
/// on each exported remainder before accept receives the resulting fresh text.
pub struct Arguments {
    pub remaining: Vec<u16>,
    pub value: Vec<u16>,
}
impl Arguments {
    pub fn new(raw: &[u16]) -> Self {
        Self {
            remaining: raw
                .iter()
                .copied()
                .filter(|&c| c != 34 && c != 35)
                .collect(),
            value: Vec::new(),
        }
    }
    /// 0 end, 1 ignored/empty color, 2 nonempty color value. Consume the complete
    /// token before requesting any native lookup/conversion, including on skips.
    pub fn accept(&mut self, trimmed: &[u16]) -> u32 {
        let end = trimmed
            .iter()
            .position(|&c| matches!(c, 0 | 9 | 32 | 62))
            .unwrap_or(trimmed.len());
        self.remaining = trimmed[end..].to_vec();
        self.value.clear();
        if end == 0 {
            return 0;
        }
        let token = &trimmed[..end];
        if token.len() > 6
            && token[..6]
                .iter()
                .zip(b"color=")
                .all(|(&a, &b)| a == u16::from(b))
        {
            self.value.extend_from_slice(&token[6..]);
            2
        } else {
            1
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn u(s: &str) -> Vec<u16> {
        s.encode_utf16().collect()
    }
    #[test]
    fn argument_removal_and_native_trim_results_keep_original_token_rules() {
        let mut a = Arguments::new(&u("  co\"lor=\"#red color= color=blue>"));
        assert_eq!(a.remaining, u("  color=red color= color=blue>"));
        assert_eq!(a.accept(&u("color=red color= color=blue>")), 2);
        assert_eq!(a.value, u("red"));
        assert_eq!(a.remaining, u(" color= color=blue>"));
        assert_eq!(a.accept(&u("color= color=blue>")), 1);
        assert!(a.value.is_empty());
        assert_eq!(a.accept(&u("color=blue>")), 2);
        assert_eq!(a.remaining, u(">"));
        assert_eq!(a.accept(&u(">")), 0);
        assert_eq!(a.accept(&u("color=red\0tail")), 2);
        assert_eq!(a.remaining, u("\0tail"));
        assert_eq!(a.accept(&u("\0tail")), 0);
        assert_eq!(a.accept(&u("color=red\r\nother")), 2);
        assert_eq!(a.value, u("red\r\nother"));
    }
    #[test]
    fn unterminated_font_removes_first_inserted_unit_too() {
        let mut s = Sami::new(&u("<FONT color=red"), &u("<font color=red")).unwrap();
        assert_eq!(s.step(), 1);
        assert_eq!(s.arguments, u("color=red"));
        s.insert(&u("{\\c&H0000ff&}"), 13);
        assert_eq!(s.step(), 0);
        assert_eq!(s.text, u("\\c&H0000ff&}"));
    }
    #[test]
    fn closing_font_and_unknown_tags_keep_source_case() {
        let mut s = Sami::new(&u("A<X>B</FONT>C"), &u("a<x>b</font>c")).unwrap();
        assert_eq!(s.step(), 0);
        assert_eq!(s.text, u("AB{\\c}C"));
        assert!(Sami::new(&[0], &[0]).is_none());
    }
}
