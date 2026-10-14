// SPDX-License-Identifier: GPL-3.0-or-later
//! Owned RealText line tags and formatting carried across clear operations.
use alloc::vec::Vec;

#[derive(Clone)]
pub struct StoredTag {
    pub name: Vec<u16>,
    pub flags: u32,
    pub attributes: Vec<(Vec<u16>, Vec<u16>)>,
}
#[derive(Default)]
pub struct Lists {
    pub tags: Vec<StoredTag>,
    pub open: Vec<StoredTag>,
    pub current: Option<StoredTag>,
}
impl Lists {
    pub fn begin(&mut self, name: &[u16], flags: u32) {
        self.current = Some(StoredTag {
            name: name.to_vec(),
            flags,
            attributes: Vec::new(),
        });
    }
    /// The native ordered map supplies distinct attributes in its original order.
    pub fn attribute(&mut self, name: &[u16], value: &[u16]) {
        if let Some(tag) = self.current.as_mut() {
            tag.attributes.push((name.to_vec(), value.to_vec()));
        }
    }
    pub fn open(&mut self) {
        if let Some(tag) = self.current.as_ref() {
            self.open.push(tag.clone());
        }
    }
    pub fn close(&mut self) {
        if let Some(tag) = self.current.as_ref() {
            if let Some(index) = self.open.iter().rposition(|open| open.name == tag.name) {
                self.open.remove(index);
            }
        }
    }
    pub fn append(&mut self) {
        if let Some(tag) = self.current.take() {
            self.tags.push(tag);
        }
    }
    pub fn clear(&mut self) {
        self.tags.clear();
        self.tags.extend(self.open.iter().cloned());
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn close_removes_only_the_last_matching_name_and_clear_keeps_order() {
        let mut lists = Lists::default();
        lists.begin(&[105], 1);
        lists.attribute(&[97], &[49]);
        lists.open();
        lists.append();
        lists.begin(&[98], 1);
        lists.open();
        lists.append();
        lists.begin(&[105], 1);
        lists.attribute(&[97], &[50]);
        lists.open();
        lists.append();
        lists.begin(&[105], 2);
        lists.close();
        lists.append();
        lists.clear();
        assert_eq!(lists.tags.len(), 2);
        assert_eq!(lists.tags[0].name, [105]);
        assert_eq!(lists.tags[0].attributes[0].1, [49]);
        assert_eq!(lists.tags[1].name, [98]);
    }
}
