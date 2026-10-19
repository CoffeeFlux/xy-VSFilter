// SPDX-License-Identifier: GPL-2.0-or-later
//! Shared style equality for canonical ownership and document AddStyle.
use xy_vsfilter_compat::document_style::StyleValue;
pub(crate) fn equal(a: &StyleValue, b: &StyleValue) -> bool {
    #[cfg(windows)]
    {
        a.native_compatible_eq_with(b, crate::windows_float::equal_f64)
    }
    #[cfg(not(windows))]
    {
        a.native_compatible_eq(b)
    }
}
