// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned widened outlines and ordered linked-arc sweep. Ellipse arithmetic is
//! supplied by a scalar backend; no foreign span, list or queue is borrowed.
use crate::{
    arc_queue::{Item, Queue},
    span_merge::{self, Span},
};
use alloc::{collections::BTreeMap, vec::Vec};

pub trait Ellipse {
    fn initialize(&mut self, rx: i32, ry: i32) -> Result<(), u32>;
    fn cross(&mut self, right: bool, center: u64, base: u64) -> Result<i32, u32>;
    fn arc(&mut self, right: bool, center: u64, line: i32) -> Result<i32, u32>;
}
struct LinkSpan {
    left: Queue,
    right: Queue,
}
impl LinkSpan {
    fn new(capacity: usize) -> Result<Self, u32> {
        Ok(Self {
            left: Queue::new(capacity).ok_or(5u32)?,
            right: Queue::new(capacity).ok_or(5u32)?,
        })
    }
}
// The native macro ORs a signed int X with the shifted signed Y. A negative X
// sign-extends through the upper word; masking it would change the reference.
fn pack(x: i32, y: i32) -> u64 {
    ((y as u32 as u64) << 32) | (x as i64 as u64)
}
fn line(point: u64) -> i32 {
    (point >> 32) as u32 as i32
}

#[derive(Default)]
pub struct Region {
    border: i32,
    spans: Vec<Span>,
}
impl Region {
    pub fn convert<E: Ellipse>(
        &mut self,
        src: &[Span],
        rx: i32,
        ry: i32,
        ellipse: &mut E,
    ) -> Result<bool, u32> {
        let rx = rx.max(0);
        let ry = ry.max(0);
        self.border = rx.max(ry);
        self.spans.clear();
        if ry > 0 {
            self.sweep(src, rx, ry, ellipse)?;
        } else if rx > 0 {
            // Two merges at the same Y are observable cleanup behavior.
            let a = span_merge::merge(&self.spans, src, (-(rx as i64)) as u64, rx as u64)
                .ok_or(6u32)?;
            self.spans = a;
            self.spans = span_merge::merge(&self.spans, src, (-(rx as i64)) as u64, rx as u64)
                .ok_or(6u32)?;
        }
        Ok(true)
    }
    fn sweep<E: Ellipse>(
        &mut self,
        src: &[Span],
        rx: i32,
        ry: i32,
        ellipse: &mut E,
    ) -> Result<(), u32> {
        ellipse.initialize(rx, ry)?;
        let capacity = usize::try_from(
            ry.checked_mul(2)
                .and_then(|n| n.checked_add(2))
                .ok_or(6u32)?,
        )
        .map_err(|_| 6u32)?;
        let mut spans = Vec::new();
        spans.try_reserve(1).map_err(|_| 5u32)?;
        let mut sentinel = LinkSpan::new(capacity)?;
        sentinel.left.append(Item {
            center: pack(i32::MAX, 0),
            deadline: i32::MAX,
        });
        spans.push(sentinel);
        let mut position = None;
        let mut dst_line = 0;
        for source in src {
            let y = line(source[0]);
            let start = y.wrapping_sub(ry);
            if start > dst_line {
                self.add_line(&mut spans, dst_line, start, ellipse)?;
                dst_line = start;
                spans
                    .last_mut()
                    .ok_or(6u32)?
                    .left
                    .center(0, pack(i32::MAX, y.wrapping_sub(1)));
                position = Some(0);
            }
            let mut at = position.ok_or(6u32)?;
            loop {
                let span = spans.get_mut(at).ok_or(6u32)?;
                let cross = ellipse.cross(false, span.left.back().center, source[0])?;
                let cross = if cross == i32::MAX {
                    i32::MAX
                } else {
                    let front = ellipse.cross(false, span.left.at(0).center, source[0])?;
                    if front == i32::MIN {
                        i32::MIN
                    } else {
                        0
                    }
                };
                if cross > ry {
                    at += 1;
                } else if cross >= -ry {
                    add_arc(&mut span.left, source[0], false, ry, ellipse)?;
                    add_arc(&mut span.right, source[1], true, ry, ellipse)?;
                    position = Some(at + 1);
                    break;
                } else {
                    let mut added = LinkSpan::new(capacity)?;
                    let deadline = y.wrapping_add(ry).wrapping_add(1);
                    added.left.append(Item {
                        center: source[0],
                        deadline,
                    });
                    added.right.append(Item {
                        center: source[1],
                        deadline,
                    });
                    spans.try_reserve(1).map_err(|_| 5u32)?;
                    spans.insert(at, added);
                    // POSITION still names the old node after InsertBefore.
                    position = Some(at + 1);
                    break;
                }
            }
        }
        self.add_line(&mut spans, dst_line, i32::MAX, ellipse)
    }
    fn add_line<E: Ellipse>(
        &mut self,
        spans: &mut Vec<LinkSpan>,
        mut current: i32,
        deadline: i32,
        ellipse: &mut E,
    ) -> Result<(), u32> {
        let mut output = [0u64; 2];
        while current < deadline {
            if spans.len() <= 1 {
                break;
            }
            let mut at = 0;
            while at + 1 < spans.len() {
                let span = &mut spans[at];
                let left_top = span.left.at(0);
                let right_top = span.right.at(0);
                let left = pack(ellipse.arc(false, left_top.center, current)?, current);
                let right = pack(ellipse.arc(true, right_top.center, current)?, current);
                if output[1] >= left {
                    output[1] = output[1].max(right);
                } else {
                    if output[1] > output[0] {
                        self.push(output)?;
                    }
                    output = [left, right];
                }
                let next = current.wrapping_add(1);
                if next == left_top.deadline {
                    span.left.pop_front();
                }
                if next == right_top.deadline {
                    span.right.pop_front();
                }
                if span.left.is_empty() {
                    spans.remove(at);
                } else {
                    at += 1;
                }
            }
            current = current.checked_add(1).ok_or(6u32)?;
        }
        if output[1] > output[0] {
            self.push(output)?;
        }
        Ok(())
    }
    fn push(&mut self, span: Span) -> Result<(), u32> {
        self.spans.try_reserve(1).map_err(|_| 5u32)?;
        self.spans.push(span);
        Ok(())
    }
    pub fn wire_len(&self) -> Option<usize> {
        self.spans.len().checked_mul(16)?.checked_add(12)
    }
    pub fn encode(&self) -> Result<Vec<u8>, u32> {
        let mut out = Vec::new();
        out.try_reserve_exact(self.wire_len().ok_or(5u32)?)
            .map_err(|_| 5u32)?;
        out.extend_from_slice(&self.border.to_le_bytes());
        out.extend_from_slice(&(self.spans.len() as u64).to_le_bytes());
        for span in &self.spans {
            for value in span {
                out.extend_from_slice(&value.to_le_bytes());
            }
        }
        Ok(out)
    }
}
fn add_arc<E: Ellipse>(
    arc: &mut Queue,
    center: u64,
    right: bool,
    ry: i32,
    ellipse: &mut E,
) -> Result<(), u32> {
    let mut i = arc.len() as i64 - 1;
    let y = line(center);
    while i >= 1 {
        let cross = ellipse.cross(right, arc.at(i as usize).center, center)?;
        if cross > ry {
            break;
        } else if cross.wrapping_add(y) > arc.at(i as usize - 1).deadline {
            arc.deadline(i as usize, cross.wrapping_add(y));
            break;
        } else {
            arc.pop_back();
        }
        i -= 1;
    }
    if i == 0 {
        let cross = ellipse.cross(right, arc.at(0).center, center)?;
        if cross <= -ry {
            arc.pop_back();
        } else if !right || cross < i32::MAX {
            arc.deadline(0, cross.wrapping_add(y));
        }
    }
    arc.append(Item {
        center,
        deadline: y.wrapping_add(ry).wrapping_add(1),
    });
    Ok(())
}
pub struct Store {
    next: u64,
    values: BTreeMap<u64, Region>,
}
impl Default for Store {
    fn default() -> Self {
        Self::new()
    }
}
impl Store {
    pub const fn new() -> Self {
        Self {
            next: 1,
            values: BTreeMap::new(),
        }
    }
    pub fn create(&mut self) -> Result<u64, u32> {
        let id = self.next;
        self.next = self.next.checked_add(1).ok_or(5u32)?;
        self.values.insert(id, Region::default());
        Ok(id)
    }
    pub fn get(&self, id: u64) -> Result<&Region, u32> {
        self.values.get(&id).ok_or(1)
    }
    pub fn get_mut(&mut self, id: u64) -> Result<&mut Region, u32> {
        self.values.get_mut(&id).ok_or(1)
    }
    pub fn remove(&mut self, id: u64) -> Result<(), u32> {
        self.values.remove(&id).map(|_| ()).ok_or(1)
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[derive(Default)]
    struct Numeric {
        calls: Vec<u32>,
    }
    impl Ellipse for Numeric {
        fn initialize(&mut self, _: i32, _: i32) -> Result<(), u32> {
            self.calls.push(0);
            Ok(())
        }
        fn cross(&mut self, right: bool, _: u64, _: u64) -> Result<i32, u32> {
            self.calls.push(if right { 2 } else { 1 });
            Ok(i32::MIN)
        }
        fn arc(&mut self, right: bool, center: u64, _: i32) -> Result<i32, u32> {
            self.calls.push(if right { 4 } else { 3 });
            Ok((center as u32 as i32) + if right { 1 } else { -1 })
        }
    }
    #[test]
    fn sweep_preserves_sentinel_queries_output_rows_and_empty_initialization() {
        let mut numeric = Numeric::default();
        let mut region = Region::default();
        region
            .convert(&[[pack(10, 100), pack(20, 100)]], 1, 1, &mut numeric)
            .unwrap();
        assert_eq!(numeric.calls, [0, 1, 1, 3, 4, 3, 4, 3, 4]);
        assert_eq!(
            region.spans,
            [
                [pack(9, 99), pack(21, 99)],
                [pack(9, 100), pack(21, 100)],
                [pack(9, 101), pack(21, 101)]
            ]
        );
        numeric.calls.clear();
        region.convert(&[], 2, 3, &mut numeric).unwrap();
        assert_eq!(numeric.calls, [0]);
        assert!(region.spans.is_empty());
        assert_eq!(region.border, 3);
        assert_eq!(pack(-1, 123), u64::MAX);
    }
    #[test]
    fn horizontal_merges_clamping_reset_and_stale_ids() {
        let mut region = Region::default();
        let mut numeric = Numeric::default();
        region
            .convert(
                &[
                    [pack(10, 100), pack(20, 100)],
                    [pack(21, 100), pack(30, 100)],
                ],
                2,
                -4,
                &mut numeric,
            )
            .unwrap();
        assert_eq!(region.spans, [[pack(8, 100), pack(32, 100)]]);
        assert!(numeric.calls.is_empty());
        region.convert(&[], -1, -1, &mut numeric).unwrap();
        assert_eq!(region.encode().unwrap(), [0; 12]);
        let mut store = Store::new();
        let id = store.create().unwrap();
        store.remove(id).unwrap();
        assert!(store.get(id).is_err());
        assert_ne!(id, store.create().unwrap());
    }
}
