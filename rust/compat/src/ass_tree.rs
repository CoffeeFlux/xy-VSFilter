// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned renderer ASS command trees, derived from cleanup 67da07cd RTS.cpp.
//! Flat node storage avoids recursive parsing and destruction. Child-list IDs
//! are checked indices, never pointers; parameters remain raw UTF-16 code units.
use alloc::vec::Vec;
use core::ops::Range;

pub const UNKNOWN: u32 = 54;
const NAMES: [&[u8]; 54] = [
    b"1c", b"2c", b"3c", b"4c", b"1a", b"2a", b"3a", b"4a", b"alpha", b"an", b"a", b"blur",
    b"bord", b"be", b"b", b"clip", b"iclip", b"c", b"fade", b"fad", b"fax", b"fay", b"fe", b"fn",
    b"frx", b"fry", b"frz", b"fr", b"fscx", b"fscy", b"fsc", b"fsp", b"fs", b"i", b"kt", b"kf",
    b"K", b"ko", b"k", b"move", b"org", b"pbo", b"pos", b"p", b"q", b"r", b"shad", b"s", b"t",
    b"u", b"xbord", b"xshad", b"ybord", b"yshad",
];
pub fn position_level(command: u32) -> i32 {
    match command {
        15 | 16 | 39..=43 => 2,
        20 | 21 | 24..=32 | 34..=38 => 1,
        _ => 0,
    }
}
fn command(input: &[u16]) -> (u32, usize) {
    for len in (1..=input.len().min(5)).rev() {
        if let Some(index) = NAMES.iter().position(|name| {
            name.len() == len && name.iter().zip(input).all(|(a, b)| u16::from(*a) == *b)
        }) {
            return (index as u32, len);
        }
    }
    (UNKNOWN, 0)
}
#[derive(Clone, Debug, PartialEq, Eq)]
struct Node {
    command: u32,
    params: Vec<Vec<u16>>,
    children: Vec<usize>,
}
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Tree {
    nodes: Vec<Node>,
    roots: Vec<usize>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Shape,
    Index,
    Overflow,
}
impl Tree {
    fn list(&self, list: u32) -> Result<&[usize], Error> {
        if list == 0 {
            Ok(&self.roots)
        } else {
            self.nodes
                .get(list as usize - 1)
                .map(|node| node.children.as_slice())
                .ok_or(Error::Index)
        }
    }
    fn list_mut(&mut self, list: u32) -> Result<&mut Vec<usize>, Error> {
        if list == 0 {
            Ok(&mut self.roots)
        } else {
            self.nodes
                .get_mut(list as usize - 1)
                .map(|node| &mut node.children)
                .ok_or(Error::Index)
        }
    }
    pub fn count(&self, list: u32) -> Result<u32, Error> {
        u32::try_from(self.list(list)?.len()).map_err(|_| Error::Overflow)
    }
    /// Append a separately prepared tree. Existing node/list IDs stay valid;
    /// source ownership transfers only after all shape/index checks succeed.
    pub fn append(&mut self, list: u32, mut incoming: Self) -> Result<(), Error> {
        self.list(list)?;
        let offset = self.nodes.len();
        let total = offset
            .checked_add(incoming.nodes.len())
            .ok_or(Error::Overflow)?;
        if total > u32::MAX as usize {
            return Err(Error::Overflow);
        }
        self.nodes.reserve(incoming.nodes.len());
        self.list_mut(list)?.reserve(incoming.roots.len());
        for node in &mut incoming.nodes {
            for child in &mut node.children {
                *child += offset;
            }
        }
        for root in &mut incoming.roots {
            *root += offset;
        }
        self.nodes.append(&mut incoming.nodes);
        self.list_mut(list)?.append(&mut incoming.roots);
        Ok(())
    }
    /// Detached byte snapshot: command:u32,child-list:u32,param-count:u32,
    /// then unit-count:u32 and UTF16LE units per parameter. No native layouts.
    pub fn packet(&self, list: u32, index: u32) -> Result<Vec<u8>, Error> {
        fn word(out: &mut Vec<u8>, value: usize) -> Result<(), Error> {
            out.extend_from_slice(
                &u32::try_from(value)
                    .map_err(|_| Error::Overflow)?
                    .to_le_bytes(),
            );
            Ok(())
        }
        let id = *self.list(list)?.get(index as usize).ok_or(Error::Index)?;
        let node = &self.nodes[id];
        let mut out = Vec::new();
        word(&mut out, node.command as usize)?;
        word(&mut out, id + 1)?;
        word(&mut out, node.params.len())?;
        for param in &node.params {
            word(&mut out, param.len())?;
            for unit in param {
                out.extend_from_slice(&unit.to_le_bytes());
            }
        }
        Ok(out)
    }
}

pub fn parse(input: &[u16], whitespace: &[u8]) -> Result<Tree, Error> {
    if input.len() != whitespace.len() {
        return Err(Error::Shape);
    }
    let mut tree = Tree::default();
    let mut work: Vec<(Range<usize>, u32)> = alloc::vec![(0..input.len(), 0)];
    while let Some((range, parent)) = work.pop() {
        let raw = &input[range.clone()];
        let white = &whitespace[range.clone()];
        let tags = crate::ass_tags::tokenize(raw, white);
        for tag in tags {
            let cmd = &raw[tag.command];
            let (kind, len) = command(cmd);
            let mut params: Vec<Vec<u16>> = tag
                .parameters
                .iter()
                .map(|span| raw[span.clone()].to_vec())
                .collect();
            match kind {
                0..=8 | 17 => {
                    let mut suffix = &cmd[len..];
                    while matches!(suffix.first(), Some(38 | 72)) {
                        suffix = &suffix[1..];
                    }
                    while matches!(suffix.last(), Some(38 | 72)) {
                        suffix = &suffix[..suffix.len() - 1];
                    }
                    params.push(suffix.to_vec());
                }
                15 | 16 | 18 | 19 | 39 | 40 | 42 | 48 | UNKNOWN => {}
                _ => params.push(cmd[len..].to_vec()),
            }
            if tree.nodes.len() >= u32::MAX as usize {
                return Err(Error::Overflow);
            }
            let id = tree.nodes.len();
            tree.nodes.push(Node {
                command: kind,
                params,
                children: Vec::new(),
            });
            tree.list_mut(parent)?.push(id);
            if kind == 48 && !tag.parameters.is_empty() && tag.parameters.len() <= 4 {
                let span = tag.parameters.last().ok_or(Error::Shape)?;
                work.push((
                    range.start + span.start..range.start + span.end,
                    (id + 1) as u32,
                ));
            }
        }
    }
    Ok(tree)
}

#[cfg(test)]
mod tests {
    use super::*;
    fn parsed(s: &str) -> Tree {
        let input: Vec<u16> = s.encode_utf16().collect();
        let white: Vec<u8> = input
            .iter()
            .map(|u| u8::from(matches!(u, 9..=13 | 32)))
            .collect();
        parse(&input, &white).unwrap()
    }
    #[test]
    fn dispatch_longest_prefix_case_and_parameter_append() {
        for (i, name) in NAMES.iter().enumerate() {
            let mut text: Vec<u16> = name.iter().map(|u| u16::from(*u)).collect();
            text.extend([49, 50]);
            assert_eq!(command(&text), (i as u32, name.len()));
        }
        let tree = parsed("\\K1\\k2\\FS20\\fs(20)40\\1c(H&12&H)H&FF&H\\");
        assert_eq!(tree.roots.len(), 6);
        assert_eq!(tree.nodes[0].command, 36);
        assert_eq!(tree.nodes[1].command, 38);
        assert_eq!(tree.nodes[2].command, UNKNOWN);
        assert_eq!(
            tree.nodes[3].params,
            alloc::vec![alloc::vec![50, 48], Vec::new()]
        );
        assert_eq!(
            tree.nodes[4].params,
            alloc::vec![alloc::vec![72, 38, 49, 50, 38, 72], Vec::new()]
        );
        assert!(tree.nodes[5].params.is_empty());
    }
    #[test]
    fn nested_transform_and_unknown_parameter_ownership() {
        let tree = parsed("\\t(0,100,\\b1\\i2)\\zunknown(1,2)\\t(1,2,3,4,\\b9)");
        assert_eq!(tree.count(0), Ok(3));
        assert_eq!(tree.count(1), Ok(2));
        assert_eq!(tree.nodes[tree.nodes[0].children[0]].command, 14);
        assert_eq!(
            tree.nodes[1].params,
            alloc::vec![alloc::vec![49], alloc::vec![50]]
        );
        assert!(tree.nodes[2].children.is_empty());
    }
    #[test]
    fn append_preserves_existing_child_ids_and_detached_packet() {
        let mut tree = parsed("\\t(\\b1)");
        let before = tree.packet(0, 0).unwrap();
        let child = u32::from_le_bytes(before[4..8].try_into().unwrap());
        tree.append(0, parsed("\\i1")).unwrap();
        assert_eq!(tree.packet(0, 0).unwrap(), before);
        assert_eq!(tree.count(child), Ok(1));
        tree.append(child, parsed("\\u1")).unwrap();
        assert_eq!(tree.count(child), Ok(2));
        assert_eq!(tree.count(0), Ok(2));
        let prior = tree.clone();
        assert_eq!(tree.append(u32::MAX, parsed("\\b2")), Err(Error::Index));
        assert_eq!(tree, prior);
        drop(tree);
        assert_eq!(before.len(), 22);
    }
    #[test]
    fn raw_units_nul_and_whitespace_are_supplied() {
        let input = [92, 98, 0xd800, 0, 92, 105, 49];
        let tree = parse(&input, &[0; 7]).unwrap();
        assert_eq!(tree.count(0), Ok(1));
        assert_eq!(tree.nodes[0].params[0], [0xd800]);
        assert_eq!(parse(&input, &[]), Err(Error::Shape));
        let input = [92, 0x2003, 98, 49];
        let a = parse(&input, &[0; 4]).unwrap();
        let b = parse(&input, &[0, 1, 0, 0]).unwrap();
        assert_eq!(a.nodes[0].command, UNKNOWN);
        assert_eq!(b.nodes[0].command, 14);
    }
}
