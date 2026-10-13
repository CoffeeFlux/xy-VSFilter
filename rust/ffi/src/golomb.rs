// SPDX-License-Identifier: GPL-3.0-or-later
use xy_vsfilter_compat::golomb::{read_step, Action};

/// Private by-value ABI; there are no native pointers or borrowed buffers.
#[repr(C)]
pub struct ReadStep {
    buffer: u64,
    value: u64,
    length: i32,
    action: u32,
}

#[no_mangle]
pub extern "C" fn xy_vsfilter_golomb_step(
    buffer: u64,
    length: i32,
    position: i32,
    size: i32,
    bits: i32,
    peek: u32,
) -> ReadStep {
    let step = read_step(buffer, length, position, size, bits, peek != 0);
    ReadStep {
        buffer: step.buffer,
        value: step.value,
        length: step.length,
        action: match step.action {
            Action::Done => 0,
            Action::Byte => 1,
            Action::Native => 2,
        },
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn abi_step_uses_only_scalar_values() {
        let s = xy_vsfilter_golomb_step(0xabc, 12, 1, 1, 8, 0);
        assert_eq!((s.buffer, s.value, s.length, s.action), (0xc, 0xab, 4, 0));
        let s = xy_vsfilter_golomb_step(0, 0, 0, 1, 32, 1);
        assert_eq!((s.buffer, s.value, s.length, s.action), (0, 0, 0, 1));
    }
}
