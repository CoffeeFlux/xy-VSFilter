// SPDX-License-Identifier: GPL-3.0-or-later
//! Exact byte-to-binary32 conversion without floating-point operations.
pub fn row(input: &[u8], output: &mut [u32]) -> bool {
    if output.len() < input.len() {
        return false;
    }
    for (&byte, word) in input.iter().zip(output.iter_mut()) {
        let value = u32::from(byte);
        *word = if value == 0 {
            0
        } else {
            let top = 31 - value.leading_zeros();
            ((top + 127) << 23) | ((value << (23 - top)) & 0x7f_ffff)
        };
    }
    output[input.len()..].fill(0);
    true
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn exponents_and_positive_zero_extension() {
        let mut output = [0x8000_0000; 7];
        assert!(row(&[0, 1, 127, 128, 255], &mut output));
        assert_eq!(
            output,
            [0, 0x3f80_0000, 0x42fe_0000, 0x4300_0000, 0x437f_0000, 0, 0]
        );
    }
}
