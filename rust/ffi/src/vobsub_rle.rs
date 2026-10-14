// SPDX-License-Identifier: GPL-3.0-or-later
use xy_vsfilter_compat::vobsub_rle;

/// Private value-only ABI. Bit 8 requests native offset advance and alignment.
#[no_mangle]
pub extern "C" fn xy_vsfilter_vobsub_nibble(byte: u8, aligned: u32) -> u32 {
    vobsub_rle::nibble(byte, aligned != 0)
}

/// Private value-only ABI. Each byte read completes before the next step, and
/// native draws and exceptions occur outside Rust. No native buffer is borrowed.
#[no_mangle]
pub extern "C" fn xy_vsfilter_vobsub_code_step(code: u32, nibble: u8, stage: u32) -> u64 {
    vobsub_rle::code_step(code, nibble, stage)
}
