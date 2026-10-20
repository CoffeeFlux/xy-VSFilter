// SPDX-License-Identifier: GPL-2.0-or-later
//! Output operations over an owned address-indexed byte image. Aliases share
//! state, including writes observed by later rows/planes. Unknown bytes are not
//! imported or read merely because they lie between touched ranges.
use crate::be_blur::zeroed;
use alloc::{collections::BTreeMap, vec::Vec};

#[derive(Clone, Copy)]
pub struct Parameters {
    pub mode: i32,             // 0 packed blend, 1 planar blend, 2 copy, 3 flip, 4 solid
    pub destination: [i32; 4], // width, height, bpp, pitch
    pub position: [i32; 2],
    pub size: [i32; 2],
    pub source_pitch: i32,
    pub color: u32,
    pub planar: bool,
}
#[derive(Clone, Copy)]
pub struct Span {
    pub address: usize,
    pub len: usize,
    pub read: bool,
    pub anchor: usize,
}
#[derive(Clone, Copy)]
enum Kind {
    Packed,
    Plane(bool),
    Copy,
    Flip,
    Solid(bool),
}
struct Row {
    dst: usize,
    src: usize,
    alpha: usize,
    width: usize,
    color: u32,
    kind: Kind,
    anchor: usize,
}
pub struct Plan {
    spans: Vec<Span>,
    rows: Vec<Row>,
}
fn address(base: usize, delta: i64) -> Result<usize, u32> {
    usize::try_from(base as i128 + i128::from(delta)).map_err(|_| 6)
}
impl Plan {
    pub fn new(p: Parameters, bases: [usize; 5]) -> Result<Self, u32> {
        let mut out = Self {
            spans: Vec::new(),
            rows: Vec::new(),
        };
        let [dw, dh, bpp, dp] = p.destination;
        let [mut x, mut y] = p.position;
        let [mut w, mut h] = p.size;
        let (mut sx, mut sy) = (0i32, 0i32);
        if p.mode == 3 {
            if w <= 0 || h <= 0 {
                return Ok(out);
            }
            let extent = dp.wrapping_mul(h);
            if extent <= 0 {
                return Ok(out);
            }
            if dp <= 0 {
                return Err(6);
            } // positive wrapped extent with backward traversal can hang
            let rows = (i64::from(extent) + i64::from(dp) - 1) / i64::from(dp);
            for row in 0..rows {
                out.push(Row {
                    dst: address(bases[0], row * i64::from(dp))?,
                    src: 0,
                    alpha: 0,
                    width: w as usize,
                    color: 0,
                    kind: Kind::Flip,
                    anchor: 0,
                })?;
            }
            return Ok(out);
        }
        if p.mode == 4 {
            if !p.planar && y.checked_add(h).is_none() {
                return Err(6); // overflowing loop bounds are an unresolved reference outcome
            }
            if w <= 0 || h <= 0 {
                return Ok(out);
            }
            let start = address(bases[0], i64::from(dp.wrapping_mul(y)))?;
            let start = address(
                start,
                i64::from(x.wrapping_mul(if p.planar { 1 } else { 4 })),
            )?;
            if p.planar {
                let plane = dp.wrapping_mul(dh);
                for i in [1, 2, 3, 0] {
                    let value = match i {
                        1 => (p.color >> 16) & 255,
                        2 => (p.color >> 8) & 255,
                        3 => p.color & 255,
                        _ => 0,
                    };
                    let base = address(start, i64::from(plane) * i64::from(i))?;
                    for row in 0..h {
                        out.push(Row {
                            dst: address(base, i64::from(row) * i64::from(dp))?,
                            src: 0,
                            alpha: 0,
                            width: w as usize,
                            color: (p.color & 0xff000000) | value,
                            kind: Kind::Solid(true),
                            anchor: 0,
                        })?;
                    }
                }
            } else {
                let end = y.wrapping_add(h);
                if end <= y {
                    return Ok(out);
                }
                for row in 0..i64::from(end) - i64::from(y) {
                    out.push(Row {
                        dst: address(start, row * i64::from(dp))?,
                        src: 0,
                        alpha: 0,
                        width: w as usize,
                        color: p.color,
                        kind: Kind::Solid(false),
                        anchor: 0,
                    })?;
                }
            }
            return Ok(out);
        }
        if !(0..=2).contains(&p.mode) {
            return Err(6);
        }
        if x < 0 {
            sx = x.wrapping_neg();
            w = w.wrapping_add(x);
            x = 0;
        }
        if y < 0 {
            sy = y.wrapping_neg();
            h = h.wrapping_add(y);
            y = 0;
        }
        if x.wrapping_add(w) > dw {
            w = dw.wrapping_sub(x);
        }
        if y.wrapping_add(h) > dh {
            h = dh.wrapping_sub(y);
        }
        if h <= 0 {
            return Ok(out);
        }
        if p.mode == 2 && w < 0 {
            return Err(6);
        } // negative memcpy count is not a benign empty clip
        if p.mode == 2 && w.checked_mul(4).is_none() {
            return Err(6); // overflowing memcpy size needs separate reference evidence
        }
        let planar = p.mode == 1;
        let bytes = if planar { w } else { w.wrapping_mul(4) };
        if bytes <= 0 {
            return Ok(out);
        }
        let width = if planar {
            bytes as usize
        } else {
            bytes as usize / 4
        };
        let dst = address(
            address(bases[0], i64::from(dp.wrapping_mul(y)))?,
            i64::from(x.wrapping_mul(bpp) >> 3),
        )?;
        let offset = i64::from(sy.wrapping_mul(p.source_pitch))
            + i64::from(sx.wrapping_mul(if planar { 1 } else { 4 }));
        if planar {
            let plane = dp.wrapping_mul(dh);
            for i in 0..4 {
                let destination = address(dst, i64::from(plane) * i as i64)?;
                let source = address(bases[i + 1], offset)?;
                let alpha = address(bases[1], offset)?;
                for row in 0..h {
                    out.push(Row {
                        dst: address(destination, i64::from(row) * i64::from(dp))?,
                        src: address(source, i64::from(row) * i64::from(p.source_pitch))?,
                        alpha: address(alpha, i64::from(row) * i64::from(p.source_pitch))?,
                        width,
                        color: 0,
                        kind: Kind::Plane(i != 0),
                        anchor: i + 1,
                    })?;
                }
            }
        } else {
            let src = address(bases[1], offset)?;
            for row in 0..h {
                out.push(Row {
                    dst: address(dst, i64::from(row) * i64::from(dp))?,
                    src: address(src, i64::from(row) * i64::from(p.source_pitch))?,
                    alpha: 0,
                    width,
                    color: 0,
                    anchor: 1,
                    kind: if p.mode == 2 {
                        Kind::Copy
                    } else {
                        Kind::Packed
                    },
                })?;
            }
        }
        Ok(out)
    }
    fn span(&mut self, address: usize, len: usize, read: bool, anchor: usize) -> Result<(), u32> {
        if address == 0 || len > isize::MAX as usize || address.checked_add(len).is_none() {
            return Err(6);
        }
        self.spans.try_reserve(1).map_err(|_| 5u32)?;
        self.spans.push(Span {
            address,
            len,
            read,
            anchor,
        });
        Ok(())
    }
    fn push(&mut self, row: Row) -> Result<(), u32> {
        let packed = matches!(
            row.kind,
            Kind::Packed | Kind::Copy | Kind::Flip | Kind::Solid(false)
        );
        let len = row
            .width
            .checked_mul(if packed { 4 } else { 1 })
            .ok_or(6u32)?;
        if len == 0 {
            return Ok(());
        }
        if !matches!(row.kind, Kind::Copy) {
            self.span(row.dst, len, true, 0)?;
        }
        if matches!(row.kind, Kind::Packed | Kind::Copy | Kind::Plane(true)) {
            self.span(row.src, len, true, row.anchor)?;
        }
        if matches!(row.kind, Kind::Plane(_)) {
            self.span(row.alpha, len, true, 1)?;
        }
        self.span(row.dst, len, false, 0)?;
        self.rows.try_reserve(1).map_err(|_| 5u32)?;
        self.rows.push(row);
        Ok(())
    }
    pub fn spans(&self) -> &[Span] {
        &self.spans
    }
    pub fn initial_reads(&self) -> Result<Vec<Span>, u32> {
        let mut written: BTreeMap<usize, usize> = BTreeMap::new();
        let mut reads = Vec::new();
        for s in &self.spans {
            let end = s.address.checked_add(s.len).ok_or(6u32)?;
            if s.read {
                let mut cursor = s.address;
                if let Some((_, &hi)) = written.range(..=cursor).next_back() {
                    cursor = cursor.max(hi).min(end);
                }
                for (&lo, &hi) in written.range(s.address..end) {
                    if lo > cursor {
                        reads.try_reserve(1).map_err(|_| 5u32)?;
                        reads.push(Span {
                            address: cursor,
                            len: lo - cursor,
                            read: true,
                            anchor: s.anchor,
                        });
                    }
                    cursor = cursor.max(hi).min(end);
                }
                if cursor < end {
                    reads.try_reserve(1).map_err(|_| 5u32)?;
                    reads.push(Span {
                        address: cursor,
                        len: end - cursor,
                        read: true,
                        anchor: s.anchor,
                    });
                }
            } else {
                let mut lo = s.address;
                let mut hi = end;
                if let Some((&a, &b)) = written.range(..=lo).next_back() {
                    if b >= lo {
                        lo = a;
                        hi = hi.max(b);
                    }
                }
                loop {
                    let next = written.range(lo..=hi).next().map(|(&a, &b)| (a, b));
                    let Some((a, b)) = next else {
                        break;
                    };
                    written.remove(&a);
                    hi = hi.max(b);
                }
                written.insert(lo, hi);
            }
        }
        Ok(reads)
    }
    pub fn execute(&self, image: &mut Image) -> Result<(), u32> {
        for row in &self.rows {
            if matches!(row.kind, Kind::Copy) {
                // Row-local snapshot models memcpy only where established by the
                // pinned CRT comparison. Later rows still see earlier writes.
                let mut bytes = zeroed(row.width * 4)?;
                bytes.copy_from_slice(image.read(row.src, row.width * 4)?);
                image.write(row.dst, &bytes)?;
                continue;
            }
            for x in 0..row.width {
                match row.kind {
                    Kind::Packed => {
                        let dst =
                            u32::from_le_bytes(image.read(row.dst + x * 4, 4)?.try_into().unwrap());
                        let src =
                            u32::from_le_bytes(image.read(row.src + x * 4, 4)?.try_into().unwrap());
                        let weight = (src >> 24) + 1;
                        let rb = (((dst & 0x00ff00ff).wrapping_mul(weight) >> 8)
                            .wrapping_add(src & 0x00ff00ff))
                            & 0x00ff00ff;
                        let g = (((dst & 0x0000ff00).wrapping_mul(weight) >> 8)
                            .wrapping_add(src & 0x0000ff00))
                            & 0x0000ff00;
                        image.write(row.dst + x * 4, &(rb | g).to_le_bytes())?;
                    }
                    Kind::Plane(color) => {
                        let dst = u16::from(image.read(row.dst + x, 1)?[0]);
                        let alpha = u16::from(image.read(row.alpha + x, 1)?[0]) + 1;
                        let value = ((dst * alpha) >> 8) as u8;
                        let value = if color {
                            value.wrapping_add(image.read(row.src + x, 1)?[0])
                        } else {
                            value
                        };
                        image.write(row.dst + x, &[value])?;
                    }
                    Kind::Flip => {
                        let mut pixel: [u8; 4] =
                            image.read(row.dst + x * 4, 4)?.try_into().unwrap();
                        pixel[3] ^= 255;
                        image.write(row.dst + x * 4, &pixel)?;
                    }
                    Kind::Solid(planar) => {
                        if planar {
                            let mut value = [image.read(row.dst + x, 1)?[0]];
                            crate::fill::plane_row(
                                &mut value,
                                (row.color >> 24) as u8,
                                row.color as u8,
                            );
                            image.write(row.dst + x, &value)?;
                        } else {
                            let mut pixel: [u8; 4] =
                                image.read(row.dst + x * 4, 4)?.try_into().unwrap();
                            crate::fill::packed_row(&mut pixel, (row.color >> 24) as u8, row.color);
                            image.write(row.dst + x * 4, &pixel)?;
                        }
                    }
                    Kind::Copy => unreachable!(),
                }
            }
        }
        Ok(())
    }
}
pub struct Block {
    pub address: usize,
    pub values: Vec<u8>,
    pub written: Vec<u8>,
    known: Vec<u8>,
}
pub struct Image {
    blocks: Vec<Block>,
}
impl Image {
    pub fn new(spans: &[Span]) -> Result<Self, u32> {
        let mut ranges = Vec::new();
        ranges.try_reserve_exact(spans.len()).map_err(|_| 5u32)?;
        for s in spans {
            ranges.push((s.address, s.address.checked_add(s.len).ok_or(6u32)?));
        }
        ranges.sort_unstable();
        let mut merged: Vec<(usize, usize)> = Vec::new();
        merged.try_reserve_exact(ranges.len()).map_err(|_| 5u32)?;
        for (start, end) in ranges {
            if let Some(last) = merged.last_mut().filter(|v| start < v.1) {
                last.1 = last.1.max(end);
            } else {
                merged.push((start, end));
            }
        }
        let mut blocks = Vec::new();
        blocks.try_reserve_exact(merged.len()).map_err(|_| 5u32)?;
        for (start, end) in merged {
            let len = end - start;
            blocks.push(Block {
                address: start,
                values: zeroed(len)?,
                known: zeroed(len)?,
                written: zeroed(len)?,
            });
        }
        Ok(Self { blocks })
    }
    fn location(&self, address: usize, len: usize) -> Result<(usize, usize), u32> {
        let i = self
            .blocks
            .partition_point(|b| b.address <= address)
            .checked_sub(1)
            .ok_or(6u32)?;
        let at = address - self.blocks[i].address;
        if at
            .checked_add(len)
            .filter(|&end| end <= self.blocks[i].values.len())
            .is_none()
        {
            return Err(6);
        }
        Ok((i, at))
    }
    pub fn import(&mut self, address: usize, bytes: &[u8]) -> Result<(), u32> {
        let (i, at) = self.location(address, bytes.len())?;
        let b = &mut self.blocks[i];
        b.values[at..at + bytes.len()].copy_from_slice(bytes);
        b.known[at..at + bytes.len()].fill(1);
        Ok(())
    }
    fn read(&self, address: usize, len: usize) -> Result<&[u8], u32> {
        let (i, at) = self.location(address, len)?;
        let b = &self.blocks[i];
        if b.known[at..at + len].contains(&0) {
            return Err(6);
        }
        Ok(&b.values[at..at + len])
    }
    fn write(&mut self, address: usize, bytes: &[u8]) -> Result<(), u32> {
        self.import(address, bytes)?;
        let (i, at) = self.location(address, bytes.len())?;
        self.blocks[i].written[at..at + bytes.len()].fill(1);
        Ok(())
    }
    pub fn blocks(&self) -> &[Block] {
        &self.blocks
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn p(mode: i32) -> Parameters {
        Parameters {
            mode,
            destination: [8, 2, 32, 8],
            position: [0, 0],
            size: [2, 2],
            source_pitch: 8,
            color: 0x80402010,
            planar: false,
        }
    }
    fn run(plan: &Plan, initial: &[u8]) -> Vec<u8> {
        let mut image = Image::new(plan.spans()).unwrap();
        for s in plan.initial_reads().unwrap() {
            image
                .import(
                    s.address,
                    &initial[s.address - 100..s.address - 100 + s.len],
                )
                .unwrap();
        }
        plan.execute(&mut image).unwrap();
        let mut output = initial.to_vec();
        for block in image.blocks() {
            for (i, &written) in block.written.iter().enumerate() {
                if written != 0 {
                    output[block.address - 100 + i] = block.values[i];
                }
            }
        }
        output
    }
    #[test]
    fn later_copy_rows_read_earlier_writes_without_importing_uninitialized_destination() {
        let plan = Plan::new(p(2), [108, 100, 0, 0, 0]).unwrap();
        let reads = plan.initial_reads().unwrap();
        assert_eq!(reads.len(), 1);
        assert_eq!(
            (reads[0].address, reads[0].len, reads[0].anchor),
            (100, 8, 1)
        );
        let output = run(
            &plan,
            &[
                1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            ],
        );
        assert_eq!(
            output,
            [1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6, 7, 8]
        );
    }
    #[test]
    fn packed_aliases_observe_prior_pixel_and_drop_destination_alpha() {
        let mut p = p(0);
        p.size = [3, 1];
        p.destination[3] = 12;
        p.source_pitch = 12;
        let plan = Plan::new(p, [104, 100, 0, 0, 0]).unwrap();
        assert_eq!(
            run(
                &plan,
                &[10, 20, 30, 255, 40, 50, 60, 128, 70, 80, 90, 128, 100, 110, 120, 128]
            ),
            [10, 20, 30, 255, 50, 70, 90, 0, 50, 70, 90, 0, 50, 70, 90, 0]
        );
    }
    #[test]
    fn planar_self_alias_uses_updated_alpha_and_wraps_added_color() {
        let mut p = p(1);
        p.destination = [1, 1, 8, 1];
        p.size = [1, 1];
        p.source_pitch = 1;
        let plan = Plan::new(p, [100, 100, 101, 102, 103]).unwrap();
        assert_eq!(run(&plan, &[128, 200, 220, 240]), [64, 250, 19, 44]);
    }
    #[test]
    fn overlapping_flip_rows_and_solid_planes_keep_operation_order() {
        let mut p = p(3);
        p.destination[3] = 4;
        let plan = Plan::new(p, [100, 0, 0, 0, 0]).unwrap();
        assert_eq!(
            run(&plan, &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]),
            [1, 2, 3, 251, 5, 6, 7, 8, 9, 10, 11, 243]
        );
        p.mode = 4;
        p.planar = true;
        p.destination = [2, 0, 8, 4];
        p.size = [2, 1];
        assert_eq!(
            run(&Plan::new(p, [100, 0, 0, 0, 0]).unwrap(), &[100, 100]),
            [18, 18]
        );
    }
    #[test]
    fn negative_copy_extent_is_unresolved_but_empty_pixel_loops_are_empty() {
        let mut p = p(2);
        p.position = [20, 0];
        assert!(matches!(Plan::new(p, [100, 100, 0, 0, 0]), Err(6)));
        p.mode = 0;
        assert!(Plan::new(p, [0; 5]).unwrap().spans().is_empty());
        p.mode = 3;
        p.size = [0, 10];
        assert!(Plan::new(p, [0; 5]).unwrap().spans().is_empty());
        p.mode = 4;
        p.position[1] = i32::MIN;
        p.size = [1, -1];
        assert!(matches!(Plan::new(p, [100, 0, 0, 0, 0]), Err(6)));
        p.mode = 2;
        p.position = [0, 0];
        p.destination[0] = i32::MAX;
        p.size = [i32::MAX, 1];
        assert!(matches!(Plan::new(p, [100, 100, 0, 0, 0]), Err(6)));
    }
}
