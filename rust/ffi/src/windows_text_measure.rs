// SPDX-License-Identifier: GPL-2.0-or-later
//! Scalar arithmetic from cleanup CText::GetTextInfo's MSVC x86/x64 code.
//! Preserve instruction order, x87 extended intermediates and CRT conversion.
#[cfg(target_arch = "x86")]
extern "C" {
    fn _ftol2_sse() -> i32;
}

#[cfg(target_arch = "x86")]
pub(crate) fn metrics(scale: u64, ascent: i32, descent: i32) -> [i32; 2] {
    let mut data = [
        scale,
        100.0f64.to_bits(),
        ascent as u32 as u64,
        descent as u32 as u64,
        0,
        0,
        0,
    ];
    // SAFETY: EDI is callee-preserved and addresses seven initialized u64s.
    // Match the native retained 100.0 x87 value across the first conversion,
    // integer-to-double spills, and two __ftol2_sse calls. The x87 stack is
    // balanced; C volatile registers, memory and stack effects are declared.
    unsafe {
        core::arch::asm!(
            "fld qword ptr [edi]", "fld qword ptr [edi + 8]", "fdiv st(1), st(0)",
            "fild dword ptr [edi + 16]", "fstp qword ptr [edi + 32]", "fld qword ptr [edi + 32]",
            "fmulp st(2), st(0)", "fxch st(1)", "call {convert}", "mov dword ptr [edi + 40], eax",
            "fdivr qword ptr [edi]", "fild dword ptr [edi + 24]", "fstp qword ptr [edi + 32]",
            "fmul qword ptr [edi + 32]", "call {convert}", "mov dword ptr [edi + 48], eax",
            in("edi") data.as_mut_ptr(),convert=sym _ftol2_sse,clobber_abi("C"),
        );
    }
    [data[5] as u32 as i32, data[6] as u32 as i32]
}
#[cfg(target_arch = "x86")]
pub(crate) fn width(scale: u64, width: i32) -> i32 {
    let mut data = [
        scale,
        100.0f64.to_bits(),
        width as u32 as u64,
        0,
        4.0f64.to_bits(),
    ];
    let result: i32;
    // SAFETY: initialized five-word local, with all accesses before the helper.
    // Native order is scale/100, integer-to-double spill, multiply, add four,
    // __ftol2_sse, arithmetic shift. One x87 value is pushed and consumed.
    unsafe {
        core::arch::asm!(
            "fld qword ptr [{data}]", "fdiv qword ptr [{data} + 8]", "fild dword ptr [{data} + 16]",
            "fstp qword ptr [{data} + 24]", "fmul qword ptr [{data} + 24]", "fadd qword ptr [{data} + 32]",
            "call {convert}",data=in(reg) data.as_mut_ptr(),convert=sym _ftol2_sse,
            lateout("eax") result,clobber_abi("C"),
        );
    }
    result >> 3
}
#[cfg(target_arch = "x86")]
pub(crate) fn spacing(bits: u64) -> i32 {
    let result: i32;
    // SAFETY: live initialized scalar; __ftol2_sse consumes the one x87 load.
    unsafe {
        core::arch::asm!("fld qword ptr [{bits}]","call {convert}",bits=in(reg) &bits,
        convert=sym _ftol2_sse,lateout("eax") result,clobber_abi("C"));
    }
    result
}

#[cfg(target_arch = "x86_64")]
fn scaled(scale: u64, value: i32, round: bool) -> i32 {
    let constants = [100.0f64, 4.0f64];
    let result: i32;
    // SAFETY: SSE2 is mandatory on x64. Scalar inputs/constants stay live;
    // MOVD/CVTDQ2PD, DIVSD/MULSD and CVTTSD2SI match MSVC, including indefinite
    // overflow results and exception flags. Add four only for final width.
    unsafe {
        core::arch::asm!(
            "movsd xmm1, qword ptr [{scale}]", "divsd xmm1, qword ptr [{constants}]",
            "movd xmm0, {value:e}", "cvtdq2pd xmm0, xmm0", "mulsd xmm1, xmm0",
            "test {round:e}, {round:e}", "jz 2f", "addsd xmm1, qword ptr [{constants} + 8]", "2:",
            "cvttsd2si {result:e}, xmm1",scale=in(reg) &scale,constants=in(reg) constants.as_ptr(),
            value=in(reg) value,round=in(reg) u32::from(round),result=lateout(reg) result,
            out("xmm0") _,out("xmm1") _,options(nostack,readonly),
        );
    }
    result
}
#[cfg(target_arch = "x86_64")]
pub(crate) fn metrics(scale: u64, ascent: i32, descent: i32) -> [i32; 2] {
    [scaled(scale, ascent, false), scaled(scale, descent, false)]
}
#[cfg(target_arch = "x86_64")]
pub(crate) fn width(scale: u64, width: i32) -> i32 {
    scaled(scale, width, true) >> 3
}
#[cfg(target_arch = "x86_64")]
pub(crate) fn spacing(bits: u64) -> i32 {
    let result: i32;
    // SAFETY: initialized live scalar, SSE2 truncating conversion, no pointer
    // escapes or stack effects. Retain indefinite results and SSE status.
    unsafe {
        core::arch::asm!("cvttsd2si {result:e}, qword ptr [{bits}]",bits=in(reg) &bits,
        result=lateout(reg) result,options(nostack,readonly));
    }
    result
}
