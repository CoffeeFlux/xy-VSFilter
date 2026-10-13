// SPDX-License-Identifier: GPL-2.0-or-later
//! The three native timeline searches, as a pull protocol. The caller performs
//! precisely the requested native time translations, in their original order.
//! This preserves CRT rounding and avoids invoking native callbacks from Rust.

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Mode {
    Entry,
    NextSegment,
    ContainingSegment,
}

#[derive(Clone, Copy)]
enum Phase {
    EntryLast,
    EntryMid,
    EntryPrevious,
    SegmentLast,
    SegmentFirst,
    SegmentMid,
    SegmentStart,
    Done,
}

pub struct Search {
    mode: Mode,
    phase: Phase,
    time: i64,
    count: i32,
    i: i32,
    j: i32,
    mid: i32,
    result: i32,
    found: bool,
}

impl Search {
    pub fn new(mode: Mode, time: i64, count: i32) -> Self {
        let count = count.max(0);
        Self {
            mode,
            phase: if count == 0 {
                Phase::Done
            } else if mode == Mode::Entry {
                Phase::EntryLast
            } else {
                Phase::SegmentLast
            },
            time,
            count,
            i: 0,
            j: count - 1,
            mid: 0,
            result: if mode == Mode::Entry { -1 } else { 0 },
            found: false,
        }
    }

    /// (start rather than end, index). No translation is requested after done.
    pub fn request(&self) -> Option<(bool, i32)> {
        Some(match self.phase {
            Phase::EntryLast => (true, self.j),
            Phase::EntryMid => (true, self.mid),
            Phase::EntryPrevious => (true, self.mid - 1),
            Phase::SegmentLast => (false, self.j),
            Phase::SegmentFirst => (false, self.i),
            Phase::SegmentMid => (false, self.mid),
            Phase::SegmentStart => (true, self.j),
            Phase::Done => return None,
        })
    }

    pub fn result(&self) -> (i32, bool) {
        (self.result, self.found)
    }

    fn finish(&mut self, result: i32, found: bool) {
        self.result = result;
        self.found = found;
        self.phase = Phase::Done;
    }

    fn entry_loop(&mut self) {
        if self.i < self.j {
            // Do not recreate signed index overflow for enormous native arrays.
            self.mid = ((i64::from(self.i) + i64::from(self.j)) >> 1) as i32;
            self.phase = Phase::EntryMid;
        } else {
            self.finish(self.result, self.result >= 0);
        }
    }

    fn entry_previous(&mut self) {
        if self.mid > 0 {
            self.phase = Phase::EntryPrevious;
        } else {
            self.finish(self.mid, true);
        }
    }

    fn segment_loop(&mut self) {
        if self.i < self.j - 1 {
            self.mid = ((i64::from(self.i) + i64::from(self.j)) >> 1) as i32;
            self.phase = Phase::SegmentMid;
        } else if self.mode == Mode::NextSegment {
            self.finish(self.j, self.j < self.count);
        } else if self.j < self.count {
            self.phase = Phase::SegmentStart;
        } else {
            self.finish(-1, false);
        }
    }

    pub fn supply(&mut self, value: i64) {
        match self.phase {
            Phase::EntryLast => {
                if self.time >= value {
                    self.finish(self.j, true);
                } else {
                    self.entry_loop();
                }
            }
            Phase::EntryMid => {
                if self.time == value {
                    self.entry_previous();
                } else {
                    if self.time < value {
                        self.result = -1;
                        if self.j == self.mid {
                            self.mid -= 1;
                        }
                        self.j = self.mid;
                    } else {
                        self.result = self.mid;
                        if self.i == self.mid {
                            self.mid += 1;
                        }
                        self.i = self.mid;
                    }
                    self.entry_loop();
                }
            }
            Phase::EntryPrevious => {
                if self.time == value {
                    self.mid -= 1;
                    self.entry_previous();
                } else {
                    self.finish(self.mid, true);
                }
            }
            Phase::SegmentLast => {
                if self.time >= value {
                    self.i = self.j;
                    self.j += 1;
                }
                self.phase = Phase::SegmentFirst;
            }
            Phase::SegmentFirst => {
                if self.time < value {
                    self.j = self.i;
                    self.i -= 1;
                }
                self.segment_loop();
            }
            Phase::SegmentMid => {
                if self.time < value {
                    self.j = self.mid;
                } else {
                    self.i = self.mid;
                }
                self.segment_loop();
            }
            Phase::SegmentStart => {
                if self.time >= value {
                    self.finish(self.j, true);
                } else {
                    self.finish(-1, false);
                }
            }
            Phase::Done => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn find(mode: Mode, time: i64, entries: &[(i64, i64)]) -> (i32, bool) {
        let mut search = Search::new(mode, time, entries.len() as i32);
        while let Some((start, index)) = search.request() {
            let entry = entries[index as usize];
            search.supply(if start { entry.0 } else { entry.1 });
        }
        search.result()
    }
    #[test]
    fn gaps_and_empty_timelines_have_different_output_indices() {
        assert_eq!(find(Mode::NextSegment, 3, &[(0, 2), (4, 6)]), (1, true));
        assert_eq!(
            find(Mode::ContainingSegment, 3, &[(0, 2), (4, 6)]),
            (-1, false)
        );
        assert_eq!(find(Mode::NextSegment, 6, &[(0, 2), (4, 6)]), (2, false));
        assert_eq!(
            find(Mode::ContainingSegment, 6, &[(0, 2), (4, 6)]),
            (-1, false)
        );
        assert_eq!(find(Mode::ContainingSegment, 0, &[]), (0, false));
        assert_eq!(find(Mode::Entry, 0, &[]), (-1, false));
    }
    #[test]
    fn last_entry_shortcut_does_not_rewind_equal_start_times() {
        assert_eq!(find(Mode::Entry, 1, &[(1, 3), (1, 4)]), (1, true));
        assert_eq!(find(Mode::Entry, 1, &[(1, 3), (1, 4), (2, 5)]), (0, true));
    }
}
