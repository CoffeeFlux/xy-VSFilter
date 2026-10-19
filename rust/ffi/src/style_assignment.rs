// SPDX-License-Identifier: GPL-2.0-or-later
//! Explicit style assignment, distinct from bit-preserving packets and Clone.
pub(crate) fn copy_bits(bits: u64) -> u64 {
    #[cfg(windows)]
    {
        crate::windows_float::xy_vsfilter_style_assign_double_v1(bits)
    }
    #[cfg(not(windows))]
    {
        bits
    }
}
