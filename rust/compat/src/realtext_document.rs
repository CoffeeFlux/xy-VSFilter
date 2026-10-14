// SPDX-License-Identifier: GPL-3.0-or-later
//! RealText document control flow; native calls occur between owned state steps.
#[derive(Clone, Copy)]
enum Phase {
    Start,
    Extracted(bool),
    EndTime,
    RenderTime,
    FlushTime,
    Advance,
    Open,
    Close,
    Append,
    FlushFinal,
    Done(bool),
}
pub struct Document {
    phase: Phase,
}
impl Default for Document {
    fn default() -> Self {
        Self {
            phase: Phase::Start,
        }
    }
}
fn named(input: &[u16], name: &[u8]) -> bool {
    input.len() == name.len() && input.iter().zip(name).all(|(&a, &b)| a == u16::from(b))
}
impl Document {
    /// Native actions: 0/1 finish false/true, 2 extract tag, 3 extract text,
    /// 4 append tag, 5/6 start/end conversion, 7 render timed line, 8 insert it,
    /// 9 advance history, 10 set formatting tag, 11/12 open/close it,
    /// 13 append prepared tag, 14 clear, 15 save window, 16 center,
    /// 17 render final line, 18 insert final line.
    /// Flags match stored tags: open=1, close=2, comment=4, text=8.
    pub fn next(
        &mut self,
        empty: bool,
        first: u16,
        response: bool,
        name: &[u16],
        flags: u32,
    ) -> u32 {
        loop {
            match self.phase {
                Phase::Start => {
                    if empty {
                        self.phase = Phase::FlushFinal;
                        return 17;
                    }
                    let text = first != 60;
                    self.phase = Phase::Extracted(text);
                    return if text { 3 } else { 2 };
                }
                Phase::Extracted(text) => {
                    if !response {
                        self.phase = Phase::Done(false);
                        return 0;
                    }
                    self.phase = Phase::Start;
                    if text {
                        return 4;
                    }
                    if flags & 4 != 0 {
                        continue;
                    }
                    if named(name, b"time") {
                        self.phase = Phase::EndTime;
                        return 5;
                    }
                    if named(name, b"b") || named(name, b"i") || named(name, b"font") {
                        self.phase = Phase::Open;
                        return 10;
                    }
                    if named(name, b"clear") {
                        return 14;
                    }
                    if named(name, b"window") {
                        if flags & 1 != 0 {
                            return 15;
                        }
                        continue;
                    }
                    if named(name, b"center") {
                        return 16;
                    }
                    if named(name, b"required") || name.is_empty() {
                        continue;
                    }
                    return 4;
                }
                Phase::EndTime => {
                    self.phase = Phase::RenderTime;
                    return 6;
                }
                Phase::RenderTime => {
                    self.phase = Phase::FlushTime;
                    return 7;
                }
                Phase::FlushTime => {
                    self.phase = Phase::Advance;
                    return 8;
                }
                Phase::Advance => {
                    self.phase = Phase::Start;
                    return 9;
                }
                Phase::Open => {
                    self.phase = Phase::Close;
                    if flags & 1 != 0 {
                        return 11;
                    }
                }
                Phase::Close => {
                    self.phase = Phase::Append;
                    if flags & 2 != 0 {
                        return 12;
                    }
                }
                Phase::Append => {
                    self.phase = Phase::Start;
                    return 13;
                }
                Phase::FlushFinal => {
                    self.phase = Phase::Done(true);
                    return 18;
                }
                Phase::Done(result) => return u32::from(result),
            }
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn time_conversion_render_insertion_and_history_advance_keep_order() {
        let mut doc = Document::default();
        let name = [116, 105, 109, 101];
        assert_eq!(doc.next(false, 60, false, &[], 0), 2);
        for action in [5, 6, 7, 8, 9] {
            assert_eq!(doc.next(true, 0, true, &name, 3), action);
        }
        for action in [17, 18, 1] {
            assert_eq!(doc.next(true, 0, true, &name, 3), action);
        }
    }
    #[test]
    fn formatting_open_and_close_are_separate_before_append() {
        let mut doc = Document::default();
        assert_eq!(doc.next(false, 60, false, &[], 0), 2);
        for action in [10, 11, 12, 13] {
            assert_eq!(doc.next(true, 0, true, &[105], 3), action);
        }
        assert_eq!(doc.next(false, 60, true, &[], 0), 2);
        assert_eq!(doc.next(true, 0, false, &[], 0), 0);
    }
}
