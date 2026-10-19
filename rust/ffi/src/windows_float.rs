// SPDX-License-Identifier: GPL-2.0-or-later
//! Target floating-domain comparisons from the pinned MSVC Windows renderer.
#[cfg(target_arch = "x86_64")]
pub(crate) fn equal_f64(a: f64, b: f64) -> bool {
    a == b
}
#[cfg(target_arch = "x86")]
pub(crate) fn equal_f64(a: f64, b: f64) -> bool {
    let equal: u8;
    // SAFETY: both initialized f64 inputs remain live; two x87 loads and
    // FUCOMPP balance the x87 stack. AX and condition flags are declared
    // clobbered. This is the pinned STSStyleBase/STSStyle::operator== instruction order,
    // including denormal/signaling-NaN load effects in x87, not SSE.
    unsafe {
        core::arch::asm!(
            "fld qword ptr [{a}]",
            "fld qword ptr [{b}]",
            "fucompp",
            "fnstsw ax",
            "test ah, 0x44",
            "setnp {equal}",
            a=in(reg) &a,
            b=in(reg) &b,
            equal=lateout(reg_byte) equal,
            lateout("ax") _,
            options(readonly,nostack),
        );
    }
    equal != 0
}
