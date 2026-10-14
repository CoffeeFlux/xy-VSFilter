// SPDX-License-Identifier: GPL-2.0-or-later
//! Closed-caption cursor and initialized cell updates.

use alloc::vec::Vec;

/// Build exactly the text consumed by native subtitle insertion. A native
/// string scan may cross a row boundary; preserve it when its NUL terminator
/// lies in the initialized backing. Otherwise decline without native changes.
pub fn display(cells: &[u16]) -> Option<Vec<u16>> {
    let cells = cells.get(..16 * 33)?;
    let mut output = Vec::new();
    for row in 0..16 {
        let mut nonempty = false;
        let mut column = 0;
        while column < 32 {
            let start = row * 33 + column;
            if cells[start] != 0 {
                let length = cells[start..].iter().position(|&unit| unit == 0)?;
                if nonempty {
                    output.push(32);
                }
                output.extend_from_slice(&cells[start..start + length]);
                column += length;
                nonempty = true;
            }
            column += 1;
        }
        if nonempty {
            output.push(10);
        }
    }
    Some(output)
}

pub fn move_cursor(mut x: i32, mut y: i32) -> Option<[i32; 2]> {
    x = x.max(0);
    y = y.max(0);
    if x >= 32 {
        x = 0;
        y = y.checked_add(1)?;
    }
    if y >= 16 {
        y = 0;
    }
    Some([x, y])
}

/// The 33rd cell of every native row is retained as its string terminator.
/// Invalid coordinates or a short backing are declined without any write.
pub fn put(buffer: &mut [u16], x: i32, y: i32, value: u16) -> bool {
    if buffer.len() < 16 * 33 || !(0..32).contains(&x) || !(0..16).contains(&y) {
        return false;
    }
    buffer[y as usize * 33 + x as usize] = value;
    true
}
const SPECIAL: [u16; 16] = [
    0xae, 0xb0, 0xbd, 0xbf, 0x2122, 0xa2, 0xa3, 0x266a, 0xe0, 0xff, 0xe8, 0xe2, 0xea, 0xee, 0xf4,
    0xfb,
];
const EXTENDED: [u16; 32] = [
    0xc0, 0xc9, 0xd3, 0xda, 0xdc, 0xfc, 0x2018, 0xa1, 0x2a, 0x2019, 0x2d, 0xa9, 0x2120, 0xb7,
    0x201c, 0x201d, 0xc1, 0xc2, 0xc7, 0xc8, 0xca, 0xcb, 0xeb, 0xce, 0xcf, 0xef, 0xd4, 0xd9, 0xf9,
    0xdb, 0xab, 0xbb,
];
const MORE_EXTENDED: [u16; 32] = [
    0xc3, 0xe3, 0xcd, 0xcc, 0xec, 0xd2, 0xf2, 0xd5, 0xf5, 0x7b, 0x7d, 0x5c, 0x5e, 0x5f, 0xa6, 0x7e,
    0xc4, 0xe4, 0xd6, 0xf6, 0xdf, 0xa5, 0xa4, 0x7c, 0xc5, 0xe5, 0xd8, 0xf8, 0x250c, 0x2510, 0x2514,
    0x2518,
];
fn basic(unit: u8) -> u16 {
    match unit {
        0x2a => 0xe1,
        0x5c => 0xe9,
        0x5e => 0xed,
        0x5f => 0xf3,
        0x60 => 0xfa,
        0x7b => 0xe7,
        0x7c => 0xf7,
        0x7d => 0x4e,
        0x7e => 0x6e,
        0x7f => 0x3f,
        _ => u16::from(unit),
    }
}

pub struct Decoder {
    length: usize,
    position: usize,
    pub action_position: usize,
    pub value: u16,
    pub x: i32,
    pub y: i32,
}
impl Decoder {
    pub fn new(length: i32) -> Self {
        Self {
            length: length.max(0) as usize,
            position: 0,
            action_position: 0,
            value: 0,
            x: 0,
            y: 0,
        }
    }
    /// 0 done, 1 put character, 2 offset cursor, 3 clear nondisplayed memory,
    /// 4 end caption, 5 erase display, 6 move cursor. Input is loaned afresh
    /// after each native operation, preserving source/backing aliases.
    pub fn next(&mut self, input: &[u8]) -> u32 {
        if input.len() < self.length {
            return 0;
        }
        while self.position < self.length {
            let mut index = self.position;
            let unit = input[index] & 0x7f;
            if unit >= 0x20 {
                self.action_position = index;
                self.position = index + 1;
                self.value = basic(unit);
                return 1;
            }
            if input[index] == 0x80 || index + 1 >= self.length {
                self.position = index + 1;
                continue;
            }
            if index + 3 < self.length
                && input[index] == input[index + 2]
                && input[index + 1] == input[index + 3]
            {
                index += 2;
            }
            let first = input[index];
            let second = input[index + 1];
            let unit = second & 0x7f;
            self.action_position = index;
            self.position = index + 2;
            if first == 0x91 && (0x20..0x30).contains(&unit) {
                continue;
            }
            if first == 0x91 && unit == 0x39 {
                self.x = 1;
                self.y = 0;
                return 2;
            }
            if first == 0x91 && (0x30..0x40).contains(&unit) {
                self.value = SPECIAL[usize::from(unit - 0x30)];
                return 1;
            }
            if first == 0x92 && (0x20..0x40).contains(&unit) {
                self.value = EXTENDED[usize::from(unit - 0x20)];
                return 1;
            }
            if first == 0x13 && (0x20..0x40).contains(&unit) {
                self.value = MORE_EXTENDED[usize::from(unit - 0x20)];
                return 1;
            }
            if first == 0x94 && (second == 0xae || second == 0x20) {
                return 3;
            }
            if first == 0x94 && second == 0x2f {
                return 4;
            }
            if first == 0x94 && second == 0x2c {
                return 5;
            }
            if first == 0x97 && [0xa1, 0xa2, 0x23].contains(&second) {
                self.x = i32::from(second & 3);
                self.y = 0;
                return 2;
            }
            let row = match first {
                0x91 => 0,
                0x92 => 2,
                0x15 => 4,
                0x16 => 6,
                0x97 => 8,
                0x10 => 10,
                0x13 => 12,
                0x94 => 14,
                _ => continue,
            };
            self.y = row + i32::from(second & 0x20 != 0);
            self.x = if second & 0x10 != 0 {
                i32::from((second & 0xe) << 1)
            } else {
                0
            };
            return 6;
        }
        0
    }
}

/// Snapshot the first caption packet before either field is decoded. The
/// supplied backing can be larger than the native logical scan length.
pub struct Packet {
    pub position: i32,
    pub capacity: usize,
    pub fields: [[u8; 64]; 2],
    pub lengths: [usize; 2],
}
pub fn extract_packet(input: &[u8], length: i32) -> Option<Packet> {
    let scan_end = length.checked_sub(9)?;
    let mut result = Packet {
        position: length.max(0),
        capacity: 0,
        fields: [[0; 64]; 2],
        lengths: [0; 2],
    };
    for position in 0..scan_end.max(0) as usize {
        if input.get(position..position + 8)? != [0, 0, 1, 0xb2, 0x43, 0x43, 1, 0xf8] {
            continue;
        }
        result.position = position as i32;
        let count = usize::from(*input.get(position + 8)? & 63);
        result.capacity = (count + 1) & !1;
        let mut cursor = position + 9;
        let mut field_count = 0;
        while field_count < result.capacity && cursor < 0x800 {
            for field in 0..2 {
                let present = *input.get(cursor)? == 255;
                cursor += 1;
                if present {
                    let bytes = input.get(cursor..cursor + 2)?;
                    let end = result.lengths[field] + 2;
                    result.fields[field]
                        .get_mut(result.lengths[field]..end)?
                        .copy_from_slice(bytes);
                    result.lengths[field] = end;
                }
                cursor += 2;
                field_count += 1;
                if field_count >= result.capacity {
                    break;
                }
            }
        }
        break;
    }
    Some(result)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn packet_first_marker_rounding_and_scan_end_are_preserved() {
        let mut input = [0x23; 64];
        input[1..9].copy_from_slice(&[0, 0, 1, 0xb2, 0x43, 0x43, 1, 0xf8]);
        input[9..16].copy_from_slice(&[1, 255, 0x80, 0, 255, 0xfe, 0xff]);
        let packet = extract_packet(&input, 64).unwrap();
        assert_eq!(
            (packet.position, packet.capacity, packet.lengths),
            (1, 2, [2, 2])
        );
        assert_eq!(&packet.fields[0][..2], &[0x80, 0]);
        assert_eq!(&packet.fields[1][..2], &[0xfe, 0xff]);
        assert_eq!(extract_packet(&input, 10).unwrap().position, 10);
        assert_eq!(extract_packet(&input, 11).unwrap().position, 1);
        input[9] = 0;
        input[20..28].copy_from_slice(&[0, 0, 1, 0xb2, 0x43, 0x43, 1, 0xf8]);
        input[28] = 63;
        assert_eq!(extract_packet(&input, 64).unwrap().capacity, 0);
        assert!(extract_packet(&[], i32::MIN).is_none());
        assert_eq!(extract_packet(&[], -1).unwrap().position, 0);
    }

    #[test]
    fn packet_cutoff_is_checked_before_each_pair_and_declines_are_bounded() {
        let mut input = [0x23; 2054];
        input[2038..2046].copy_from_slice(&[0, 0, 1, 0xb2, 0x43, 0x43, 1, 0xf8]);
        input[2046..2053].copy_from_slice(&[63, 255, 1, 2, 255, 3, 4]);
        let packet = extract_packet(&input, 2048).unwrap();
        assert_eq!(packet.lengths, [2, 2]);
        assert!(extract_packet(&input[..2048], 2048).is_none());
        input[2047] = 0;
        input[2050] = 0;
        assert!(extract_packet(&input[..2051], 2051).is_some());
        assert!(extract_packet(&input[..2050], 2050).is_none());
    }

    #[test]
    fn display_preserves_row_gaps_and_bounded_cross_row_strings() {
        let mut cells = [0u16; 528];
        cells[0] = 65;
        cells[2] = 66;
        cells[33] = 67;
        assert_eq!(display(&cells).unwrap(), [65, 32, 66, 10, 67, 10]);
        cells.fill(65);
        cells[527] = 0;
        let text = display(&cells).unwrap();
        assert_eq!(text.len(), 4488);
        cells[527] = 65;
        assert!(display(&cells).is_none());
        cells.fill(0);
        cells[527] = 65;
        assert!(display(&cells).unwrap().is_empty());
    }

    #[test]
    fn raw_parity_and_duplicate_consumption_are_not_normalized() {
        let mut decoder = Decoder::new(4);
        assert_eq!(decoder.next(&[0x94, 0x2f, 0x94, 0x2f]), 4);
        assert_eq!(decoder.action_position, 2);
        assert_eq!(decoder.next(&[0x94, 0x2f, 0x94, 0x2f]), 0);
        let mut decoder = Decoder::new(2);
        assert_eq!(decoder.next(&[0x94, 0xaf]), 6);
        assert_eq!((decoder.x, decoder.y), (0, 15));
        let mut decoder = Decoder::new(3);
        assert_eq!(decoder.next(&[0x80, 0x91, 0x39]), 2);
        assert_eq!((decoder.x, decoder.y), (1, 0));
    }

    #[test]
    fn wrapping_is_single_step_and_row_terminators_are_preserved() {
        assert_eq!(move_cursor(200, 14), Some([0, 15]));
        assert_eq!(move_cursor(32, 15), Some([0, 0]));
        assert_eq!(move_cursor(-1, -1), Some([0, 0]));
        assert_eq!(move_cursor(31, i32::MAX), Some([31, 0]));
        assert_eq!(move_cursor(32, i32::MAX), None);
        let mut data = [7u16; 528];
        assert!(put(&mut data, 31, 15, 0xd800));
        assert_eq!(data[526], 0xd800);
        assert_eq!(data[527], 7);
        assert!(!put(&mut data, 32, 15, 0));
        assert!(!put(&mut data[..527], 0, 0, 0));
    }
}
