// SPDX-License-Identifier: GPL-3.0-or-later
//! DVD control blocks, with patches only for fields the original assigns.
//! Warning/assertion paths decline so native observation order is retained.
#[derive(Default)]
#[repr(C)]
pub struct Patch {
    pub flags: u32,
    pub current: i32,
    pub forced: u32,
    pub delay: i32,
    pub left: i32,
    pub top: i32,
    pub right: i32,
    pub bottom: i32,
    pub offsets: [usize; 2],
    pub palette: u32,
    pub transparency: u32,
    pub animated: u32,
}
pub const FORCED: u32 = 1;
pub const DELAY: u32 = 2;
pub const RECT: u32 = 4;
pub const OFFSETS: u32 = 8;

struct Reader<F> {
    len: usize,
    read: F,
}
impl<F: FnMut(usize) -> u8> Reader<F> {
    fn byte(&mut self, index: usize) -> Option<u8> {
        if index >= self.len {
            None
        } else {
            Some((self.read)(index))
        }
    }
    fn word(&mut self, index: usize) -> Option<u16> {
        Some((u16::from(self.byte(index)?) << 8) | u16::from(self.byte(index.checked_add(1)?)?))
    }
}

/// The reader is invoked only for individual bytes actually consumed. No
/// unused packet prefix, tail or gap needs to be initialized or copied.
pub fn parse(
    len: usize,
    data_size: usize,
    time: i32,
    read: impl FnMut(usize) -> u8,
) -> Option<Patch> {
    let mut reader = Reader { len, read };
    let mut patch = Patch::default();
    let mut next = data_size;
    let mut palettes = 0u16;
    let mut transparencies = 0u16;
    loop {
        let mut i = next;
        patch.current = i32::from(reader.word(i)?) * 1024 / 90;
        i = i.checked_add(2)?;
        next = usize::from(reader.word(i)?);
        i = i.checked_add(2)?;
        // Preserve the native assertion and any earlier state observations by
        // declining the entire transaction before applying a patch.
        if next > len || next < data_size {
            return None;
        }
        if patch.current > time {
            break;
        }
        loop {
            let command = reader.byte(i)?;
            let extra = match command {
                3 | 4 => 2,
                5 => 6,
                6 => 4,
                _ => 0,
            };
            if i.checked_add(extra)? >= len {
                return None;
            }
            i += 1;
            match command {
                0 => {
                    patch.flags |= FORCED;
                    patch.forced = 1;
                }
                1 => {
                    patch.flags |= FORCED;
                    patch.forced = 0;
                }
                2 => {
                    patch.flags |= DELAY;
                    patch.delay = patch.current;
                }
                3 => {
                    patch.palette = u32::from(reader.word(i)?);
                    i += 2;
                    palettes = palettes.wrapping_add(1);
                }
                4 => {
                    patch.transparency = u32::from(reader.word(i)?);
                    i += 2;
                    transparencies = transparencies.wrapping_add(1);
                }
                5 => {
                    let mut value = [0i32; 6];
                    for (index, slot) in value.iter_mut().enumerate() {
                        *slot = i32::from(reader.byte(i + index)?);
                    }
                    patch.flags |= RECT;
                    patch.left = (value[0] << 4) + (value[1] >> 4);
                    patch.top = (value[3] << 4) + (value[4] >> 4);
                    patch.right = ((value[1] & 15) << 8) + value[2] + 1;
                    patch.bottom = ((value[4] & 15) << 8) + value[5] + 1;
                    i += 6;
                }
                6 => {
                    patch.flags |= OFFSETS;
                    patch.offsets = [
                        usize::from(reader.word(i)?),
                        usize::from(reader.word(i + 2)?),
                    ];
                    i += 4;
                }
                _ => break,
            }
        }
        // End markers and unknown commands both advance past their byte. A
        // forward target is followed only after the original inclusive test.
        if i > next || i >= len {
            break;
        }
    }
    patch.animated = u32::from(palettes > 1 || transparencies > 1);
    Some(patch)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn future_block_updates_time_and_resets_palette_without_touching_other_fields() {
        let bytes = [0, 90, 0, 0, 0xff];
        let patch = parse(bytes.len(), 0, 1023, |index| bytes[index]).unwrap();
        assert_eq!(patch.current, 1024);
        assert_eq!(patch.flags, 0);
        assert_eq!(
            (patch.palette, patch.transparency, patch.animated),
            (0, 0, 0)
        );
    }
    #[test]
    fn unknown_command_still_allows_a_forward_control_block() {
        let bytes = [0, 0, 0, 5, 0x77, 0, 90, 0, 5, 2, 0xff];
        let patch = parse(bytes.len(), 0, i32::MAX, |index| bytes[index]).unwrap();
        assert_eq!(patch.current, 1024);
        assert_eq!(patch.flags, DELAY);
        assert_eq!(patch.delay, 1024);
    }
}
