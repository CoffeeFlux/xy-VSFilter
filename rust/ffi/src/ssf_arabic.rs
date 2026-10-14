// SPDX-License-Identifier: GPL-2.0-or-later
// Scalar-only private native bridges; no references cross the boundary.
#[no_mangle]
pub extern "C" fn xy_vsfilter_ssf_is_arabic(c: u16) -> u32 {
    u32::from(xy_vsfilter_compat::ssf_arabic::is_arabic(c))
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_ssf_arabic_form(c: u16, form: u32) -> u32 {
    xy_vsfilter_compat::ssf_arabic::replace(c, form)
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_ssf_arabic_neighbors(c: u16, previous: u16, next: u16) -> u32 {
    xy_vsfilter_compat::ssf_arabic::neighbors(c, previous, next)
}
