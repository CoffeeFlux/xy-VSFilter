// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned cleanup scan conversion. Paths, figure state, edges and final spans
//! remain in safe Rust. Target arithmetic is isolated behind a scalar backend.
use crate::{edge_storage::Storage, path_align::Point, path_data::Path};
use alloc::{collections::BTreeMap, vec::Vec};

pub trait Curve: Sized {
    fn new(points: [Point; 4], spline: bool, first_set: bool) -> Self;
    fn origin(&self) -> Point;
    fn point(&mut self) -> Point;
    fn advance(&mut self) -> bool;
    fn endpoint(&mut self) -> Point;
}
#[derive(Default)]
pub struct Scan {
    size: [i32; 2],
    next: u32,
    capacity: u32,
    first_set: bool,
    first: Option<Point>,
    last: Option<Point>,
    spans: Vec<[u64; 2]>,
}
impl Scan {
    pub fn spans(&self) -> &[[u64; 2]] {
        &self.spans
    }
    pub fn convert<C: Curve>(&mut self, path: &Path, size: [i32; 2]) -> Result<bool, u32> {
        self.spans.clear();
        if path.points().is_empty() {
            self.size = [0; 2];
            return Ok(false);
        }
        self.size = size;
        if size[1] < 0 {
            return Err(6);
        }
        self.next = 1;
        self.capacity = 2048;
        let mut edges = Storage::new(size[1], self.capacity).ok_or(5u32)?;
        self.first_set = false;
        self.first = Some(Point { x: 0, y: 0 });
        self.last = self.first;
        let mut moved = false;
        let mut i = 0usize;
        while i < path.points().len() {
            match path.types()[i] & !1 {
                6 => {
                    if moved && self.first != self.last {
                        self.line(&mut edges, self.last.unwrap(), self.first.unwrap())?;
                    }
                    moved = true;
                    self.first_set = false;
                    self.last = Some(path.points()[i]);
                }
                2 => {
                    let previous = i.checked_sub(1).ok_or(6u32)?;
                    self.line(&mut edges, path.points()[previous], path.points()[i])?;
                }
                kind @ (4 | 0xfc | 0xfa) => {
                    // Native signed count checks can admit negative bases. An
                    // admitted prefix read before the buffer is unresolved OOB.
                    let offset = if kind == 0xfa { 3 } else { 1 };
                    let signed_base = i as i64 - offset;
                    if path.points().len() as i64 - signed_base >= 4 {
                        let base = usize::try_from(signed_base).map_err(|_| 6u32)?;
                        let points: [Point; 4] = path
                            .points()
                            .get(base..base + 4)
                            .ok_or(6u32)?
                            .try_into()
                            .unwrap();
                        let mut curve = C::new(points, kind != 4, self.first_set);
                        if !self.first_set {
                            let p = curve.origin();
                            self.first = Some(p);
                            self.last = Some(p);
                            self.first_set = true;
                        }
                        loop {
                            let p = curve.point();
                            self.line(&mut edges, self.last.unwrap(), p)?;
                            if !curve.advance() {
                                break;
                            }
                        }
                        let p = curve.endpoint();
                        self.line(&mut edges, self.last.unwrap(), p)?;
                    }
                    if kind != 0xfa {
                        i += 2;
                    }
                }
                _ => {}
            }
            i += 1;
        }
        if moved && self.first != self.last {
            self.line(&mut edges, self.last.unwrap(), self.first.unwrap())?;
        }
        self.spans
            .try_reserve((self.next / 2) as usize)
            .map_err(|_| 5u32)?;
        for row in 0..size[1] {
            let spans = edges.spans(row).ok_or(6u32)?;
            self.spans
                .try_reserve(spans.as_slice().len())
                .map_err(|_| 5u32)?;
            self.spans.extend_from_slice(spans.as_slice());
        }
        Ok(true)
    }
    fn line(&mut self, edges: &mut Storage, start: Point, end: Point) -> Result<(), u32> {
        let last = self.last.ok_or(6u32)?;
        if last != start {
            self.segment(edges, last, start)?;
        }
        self.segment(edges, start, end)
    }
    fn segment(&mut self, edges: &mut Storage, start: Point, end: Point) -> Result<(), u32> {
        if !self.first_set {
            self.first = Some(start);
            self.first_set = true;
        }
        self.last = Some(end);
        let result = match end.y.cmp(&start.y) {
            core::cmp::Ordering::Greater => Some(edges.line(self.next, start, end, true)),
            core::cmp::Ordering::Less => Some(edges.line(self.next, end, start, false)),
            core::cmp::Ordering::Equal => None,
        };
        if let Some(result) = result {
            let [next, capacity] = result.map_err(|e| if e == 3 { 6 } else { e })?;
            self.next = next;
            self.capacity = capacity;
        }
        Ok(())
    }
    pub fn wire_len(&self) -> Option<usize> {
        self.spans.len().checked_mul(16)?.checked_add(44)
    }
    pub fn encode(&self) -> Result<Vec<u8>, u32> {
        let length = self.wire_len().ok_or(5u32)?;
        let mut bytes = Vec::new();
        bytes.try_reserve_exact(length).map_err(|_| 5u32)?;
        let zero = Point { x: 0, y: 0 };
        let first = self.first.unwrap_or(zero);
        let last = self.last.unwrap_or(zero);
        for value in [
            self.size[0] as u32,
            self.size[1] as u32,
            self.next,
            self.capacity,
            u32::from(self.first_set),
            first.x as u32,
            first.y as u32,
            last.x as u32,
            last.y as u32,
        ] {
            bytes.extend_from_slice(&value.to_le_bytes());
        }
        bytes.extend_from_slice(&(self.spans.len() as u64).to_le_bytes());
        for span in &self.spans {
            for value in span {
                bytes.extend_from_slice(&value.to_le_bytes());
            }
        }
        Ok(bytes)
    }
}
pub struct Store {
    next: u64,
    values: BTreeMap<u64, Scan>,
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
        self.values.insert(id, Scan::default());
        Ok(id)
    }
    pub fn get(&self, id: u64) -> Result<&Scan, u32> {
        self.values.get(&id).ok_or(1)
    }
    pub fn get_mut(&mut self, id: u64) -> Result<&mut Scan, u32> {
        self.values.get_mut(&id).ok_or(1)
    }
    pub fn remove(&mut self, id: u64) -> Result<(), u32> {
        self.values.remove(&id).map(|_| ()).ok_or(1)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    struct Endpoints([Point; 4]);
    impl Curve for Endpoints {
        fn new(p: [Point; 4], _: bool, _: bool) -> Self {
            Self(p)
        }
        fn origin(&self) -> Point {
            self.0[0]
        }
        fn point(&mut self) -> Point {
            self.0[0]
        }
        fn advance(&mut self) -> bool {
            false
        }
        fn endpoint(&mut self) -> Point {
            self.0[3]
        }
    }
    fn path(points: &[Point], types: &[u8]) -> Path {
        let mut p = Path::allocate(points.len() as i32).unwrap();
        let (xy, t) = p.buffers();
        xy.copy_from_slice(points);
        t.copy_from_slice(types);
        p
    }
    #[test]
    fn closed_figure_then_empty_retains_state_but_clears_owned_spans() {
        let p = path(
            &[
                Point { x: 0, y: 0 },
                Point { x: 64, y: 0 },
                Point { x: 64, y: 64 },
            ],
            &[6, 2, 2],
        );
        let mut scan = Scan::default();
        assert_eq!(scan.convert::<Endpoints>(&p, [64, 8]), Ok(true));
        assert_eq!(scan.first, scan.last);
        assert!(scan.first_set);
        assert!(!scan.spans.is_empty());
        let before = scan.encode().unwrap();
        assert_eq!(
            scan.convert::<Endpoints>(&Path::default(), [1, 1]),
            Ok(false)
        );
        let after = scan.encode().unwrap();
        assert_eq!(after.len(), 44);
        assert_eq!(&after[..8], &[0; 8]);
        assert_eq!(&after[8..36], &before[8..36]);
        let mut owners = Store::new();
        let id = owners.create().unwrap();
        owners.remove(id).unwrap();
        assert!(owners.get(id).is_err());
        assert_ne!(owners.create().unwrap(), id);
    }
    #[test]
    fn truncated_curves_are_skipped_but_admitted_prefix_oob_is_unresolved() {
        let p = Point { x: 0, y: 0 };
        let mut scan = Scan::default();
        assert_eq!(
            scan.convert::<Endpoints>(&path(&[p, p], &[4, 4]), [8, 8]),
            Ok(true)
        );
        assert!(scan.spans.is_empty());
        assert!(!scan.first_set);
        assert_eq!(
            scan.convert::<Endpoints>(&path(&[p, p, p], &[4, 4, 4]), [8, 8]),
            Err(6)
        );
        assert_eq!(scan.convert::<Endpoints>(&path(&[p], &[2]), [8, 8]), Err(6));
        // The ignored move-NC point is connected by the next line, preserving
        // the initial real move as first point and closing back to it.
        let q = path(
            &[p, Point { x: 64, y: 0 }, Point { x: 64, y: 64 }],
            &[6, 0xfe, 2],
        );
        assert_eq!(scan.convert::<Endpoints>(&q, [64, 8]), Ok(true));
        assert_eq!(scan.first, Some(p));
        assert_eq!(scan.last, Some(p));
    }
}
