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

/// Private scalar ABI for the cleanup x86 implicit STSStyle assignment.
/// Bits enter and leave in integer registers, avoiding an extra floating copy.
#[no_mangle]
pub extern "C" fn xy_vsfilter_style_assign_double_v1(bits: u64) -> u64 {
    #[cfg(target_arch = "x86")]
    {
        let mut out = 0u64;
        // SAFETY: initialized, live, disjoint eight-byte input/output objects.
        // The reference generated assignment uses FLD/FSTP for each double.
        // Balance the x87 stack, retain NaN quieting and floating status, and
        // declare output memory writes. No Rust or native pointer escapes.
        unsafe {
            core::arch::asm!(
                "fld qword ptr [{input}]",
                "fstp qword ptr [{output}]",
                input=in(reg) &bits,
                output=in(reg) &mut out,
                options(nostack),
            );
        }
        out
    }
    #[cfg(not(target_arch = "x86"))]
    {
        bits
    }
}
