// SPDX-License-Identifier: GPL-2.0-or-later
#[no_mangle]
pub extern "C" fn xy_vsfilter_mask_scalar(
    source: u8,
    body: u8,
    mask: u8,
    color_alpha: i32,
    flags: u32,
) -> u8 {
    xy_vsfilter_compat::mask::scalar(source, body, mask, color_alpha, flags)
}
