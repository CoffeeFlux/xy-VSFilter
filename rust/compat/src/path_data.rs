// SPDX-License-Identifier: GPL-2.0-or-later
//! Persistent path ownership. Foreign users exchange detached packets only.
use crate::path_align::Point;
use alloc::{collections::BTreeMap, vec::Vec};
#[derive(Clone, Default, PartialEq, Eq)]
pub struct Path {
    points: Vec<Point>,
    types: Vec<u8>,
}
impl Path {
    pub fn points(&self) -> &[Point] {
        &self.points
    }
    pub fn buffers(&mut self) -> (&mut [Point], &mut [u8]) {
        (&mut self.points, &mut self.types)
    }
    pub fn allocate(count: i32) -> Result<Self, u32> {
        if count < 0 {
            return Err(6);
        }
        let n = count as usize;
        let mut p = Self::default();
        p.points.try_reserve_exact(n).map_err(|_| 5u32)?;
        p.types.try_reserve_exact(n).map_err(|_| 5u32)?;
        p.points.resize(n, Point { x: 0, y: 0 });
        p.types.resize(n, 0);
        Ok(p)
    }
    pub fn assign(&mut self, source: &Self) {
        // Native operator= deliberately leaves the destination on empty input.
        if !source.points.is_empty() {
            *self = source.clone();
        }
    }
    pub fn append(&mut self, source: &Self, dx: i32, dy: i32) -> Result<(), u32> {
        let n = self
            .points
            .len()
            .checked_add(source.points.len())
            .ok_or(5u32)?;
        if n > i32::MAX as usize {
            return Err(5);
        }
        self.points
            .try_reserve_exact(source.points.len())
            .map_err(|_| 5u32)?;
        self.types
            .try_reserve_exact(source.types.len())
            .map_err(|_| 5u32)?;
        for (p, t) in source.points.iter().zip(&source.types) {
            self.points.push(Point {
                x: p.x.wrapping_add(dx),
                y: p.y.wrapping_add(dy),
            });
            self.types.push(*t);
        }
        Ok(())
    }
    pub fn align(&mut self) -> [i32; 4] {
        let Some(first) = self.points.first() else {
            self.clear();
            return [0; 4];
        };
        let (mut x, mut y, mut xx, mut yy) = (first.x, first.y, first.x, first.y);
        for p in &self.points {
            x = x.min(p.x);
            y = y.min(p.y);
            xx = xx.max(p.x);
            yy = yy.max(p.y);
        }
        x = (x >> 3) & !7;
        y = (y >> 3) & !7;
        xx = xx.wrapping_add(7) >> 3;
        yy = yy.wrapping_add(7) >> 3;
        for p in &mut self.points {
            p.x = p.x.wrapping_sub(x.wrapping_mul(8));
            p.y = p.y.wrapping_sub(y.wrapping_mul(8));
        }
        [
            x,
            y,
            xx.wrapping_add(1).wrapping_sub(x),
            yy.wrapping_add(1).wrapping_sub(y),
        ]
    }
    pub fn clear(&mut self) {
        *self = Self::default();
    }
    pub fn hash(&self) -> u32 {
        let mix = |a: u32, b: u32| a.wrapping_mul(33).wrapping_add(b);
        let mut h = mix(515, self.points.len() as u32);
        for t in &self.types {
            h = mix(h, *t as u32);
        }
        for p in &self.points {
            h = mix(mix(h, p.x as u32), p.y as u32);
        }
        h
    }
    pub fn encode(&self) -> Result<Vec<u8>, u32> {
        let n = self.points.len();
        let len = n
            .checked_mul(9)
            .and_then(|n| n.checked_add(4))
            .ok_or(5u32)?;
        let mut out = Vec::new();
        out.try_reserve_exact(len).map_err(|_| 5u32)?;
        out.extend_from_slice(&(n as u32).to_le_bytes());
        for (p, t) in self.points.iter().zip(&self.types) {
            out.extend_from_slice(&p.x.to_le_bytes());
            out.extend_from_slice(&p.y.to_le_bytes());
            out.push(*t);
        }
        Ok(out)
    }
    pub fn decode(input: &[u8]) -> Result<Self, u32> {
        let header = input.get(..4).ok_or(3u32)?;
        let n = u32::from_le_bytes(header.try_into().unwrap());
        if n > i32::MAX as u32
            || (n as usize).checked_mul(9).and_then(|n| n.checked_add(4)) != Some(input.len())
        {
            return Err(3);
        }
        let mut p = Self::allocate(n as i32)?;
        for ((point, t), bytes) in p
            .points
            .iter_mut()
            .zip(&mut p.types)
            .zip(input[4..].chunks_exact(9))
        {
            point.x = i32::from_le_bytes(bytes[..4].try_into().unwrap());
            point.y = i32::from_le_bytes(bytes[4..8].try_into().unwrap());
            *t = bytes[8];
        }
        Ok(p)
    }
}
pub struct Store {
    next: Option<u64>,
    paths: BTreeMap<u64, Path>,
}
impl Default for Store {
    fn default() -> Self {
        Self::new()
    }
}
impl Store {
    pub const fn new() -> Self {
        Self {
            next: Some(1),
            paths: BTreeMap::new(),
        }
    }
    pub fn create(&mut self, p: Path) -> Result<u64, u32> {
        let id = self.next.ok_or(5u32)?;
        self.next = id.checked_add(1);
        self.paths.insert(id, p);
        Ok(id)
    }
    pub fn get(&self, id: u64) -> Result<&Path, u32> {
        self.paths.get(&id).ok_or(1)
    }
    pub fn get_mut(&mut self, id: u64) -> Result<&mut Path, u32> {
        self.paths.get_mut(&id).ok_or(1)
    }
    pub fn remove(&mut self, id: u64) -> Result<(), u32> {
        self.paths.remove(&id).map(|_| ()).ok_or(1)
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn owned_copy_empty_assignment_and_packet_validation() {
        let mut a = Path::allocate(1).unwrap();
        a.points[0] = Point { x: 13, y: -7 };
        a.types[0] = 255;
        let mut b = a.clone();
        a.points[0].x = 99;
        assert_eq!(b.points[0].x, 13);
        b.assign(&Path::default());
        assert_eq!(b.points.len(), 1);
        let bytes = b.encode().unwrap();
        assert!(Path::decode(&bytes).unwrap() == b);
        assert!(Path::decode(&bytes[..bytes.len() - 1]).is_err());
        let mut s = Store::new();
        let id = s.create(b).unwrap();
        s.remove(id).unwrap();
        assert!(s.get(id).is_err());
        assert_ne!(s.create(a).unwrap(), id);
    }
    #[test]
    fn translation_and_geometry_preserve_machine_integer_results() {
        let mut a = Path::allocate(1).unwrap();
        a.points[0] = Point {
            x: i32::MAX,
            y: i32::MIN,
        };
        a.types[0] = 6;
        let mut b = Path::default();
        b.append(&a, 1, -1).unwrap();
        assert_eq!(
            b.points[0],
            Point {
                x: i32::MIN,
                y: i32::MAX
            }
        );
        assert_eq!(b.align(), [-268435456, 268435448, 1, -536870903]);
        assert_eq!(b.points[0], Point { x: 0, y: 63 });
    }
}
