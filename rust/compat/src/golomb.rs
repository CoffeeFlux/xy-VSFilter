// SPDX-License-Identifier: GPL-3.0-or-later
//! Scalar transition for the bitmap reader's 0..=32-bit path. Storage access
//! stays native. Wider requests and whole 64-bit cached words retain their
//! original path until they have separate platform-specific evidence.

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Action {
    Done,
    Byte,
    Native,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Step {
    pub buffer: u64,
    pub value: u64,
    pub length: i32,
    pub action: Action,
}

pub fn read_step(
    buffer: u64,
    length: i32,
    position: i32,
    size: i32,
    bits: i32,
    peek: bool,
) -> Step {
    let mut step = Step {
        buffer,
        value: 0,
        length,
        action: Action::Native,
    };
    if !(0..=32).contains(&bits) || !(0..64).contains(&length) {
        return step;
    }
    if length < bits {
        // The reference shifts before checking EOF, even on a failed refill.
        step.buffer = buffer.wrapping_shl(8);
        step.action = if position >= size {
            Action::Done
        } else {
            Action::Byte
        };
    } else {
        let remaining = length - bits;
        step.value = (buffer >> remaining) & ((1_u64 << bits) - 1);
        if !peek {
            step.buffer &= (1_u64 << remaining) - 1;
            step.length = remaining;
        }
        step.action = Action::Done;
    }
    step
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn eof_changes_buffer_without_changing_cached_length() {
        let step = read_step(0x12, 3, 8, 8, 8, false);
        assert_eq!(
            step,
            Step {
                buffer: 0x1200,
                value: 0,
                length: 3,
                action: Action::Done
            }
        );
    }
    #[test]
    fn peek_refill_is_followed_by_nonconsuming_extraction() {
        let step = read_step(0x5, 3, 0, 1, 8, true);
        assert_eq!(step.action, Action::Byte);
        let filled = step.buffer | 0xab;
        let step = read_step(filled, step.length + 8, 1, 1, 8, true);
        assert_eq!(step.value, 0xb5);
        assert_eq!((step.buffer, step.length), (0x5ab, 11));
    }
    #[test]
    fn unsupported_requests_leave_the_native_state_untouched() {
        for (bits, length) in [(33, 0), (64, 7), (-1, 0), (0, 64), (8, -1)] {
            assert_eq!(
                read_step(u64::MAX, length, -1, 0, bits, false),
                Step {
                    buffer: u64::MAX,
                    value: 0,
                    length,
                    action: Action::Native
                }
            );
        }
    }
}
