// SPDX-License-Identifier: GPL-2.0-or-later
//! Pinned MSVC named-map arithmetic, including the floating exception domain.
//! The safe map decides WHEN arithmetic occurs. These scalar-only callbacks
//! preserve target conversions; no native map or foreign pointer is retained.
use xy_vsfilter_compat::document_style_map::Arithmetic;

pub(crate) static ARITHMETIC: Arithmetic = Arithmetic::new(thresholds, estimate);

#[cfg(target_arch = "x86")]
extern "C" {
    // Compiler helper: consumes ST(0), returns signed i64 in EDX:EAX. This is
    // __ftol2, not the font-height __ftol2_sse signed-i32 conversion.
    fn _ftol2() -> i64;
}

#[cfg(target_arch = "x86")]
fn thresholds(bins: u32) -> (usize, usize) {
    let mut data = [
        bins,
        (4294967296.0f32).to_bits(),
        0,
        (2.25f32).to_bits(),
        (0.25f32).to_bits(),
        0,
        0,
    ];
    // SAFETY: EDI addresses this live, initialized seven-word local throughout
    // both calls and is callee-preserved. Signed load plus unsigned correction,
    // f32 spill, extended products and __ftol2 match the reference. Each call
    // consumes one x87 value; the second consumes the remaining bins value.
    // Declare C volatile registers, flags, stack use and memory writes.
    unsafe {
        core::arch::asm!(
            "fild dword ptr [edi]",
            "cmp dword ptr [edi], 0",
            "jns 2f",
            "fadd dword ptr [edi + 4]",
            "2:",
            "fstp dword ptr [edi + 8]",
            "fld dword ptr [edi + 8]",
            "fld dword ptr [edi + 12]",
            "fmul st(0), st(1)",
            "call {convert}",
            "mov dword ptr [edi + 20], eax",
            "fmul dword ptr [edi + 16]",
            "call {convert}",
            "mov dword ptr [edi + 24], eax",
            in("edi") data.as_mut_ptr(),
            convert = sym _ftol2,
            clobber_abi("C"),
        );
    }
    (data[5] as usize, data[6] as usize)
}

#[cfg(target_arch = "x86")]
fn estimate(count: usize) -> usize {
    let mut data = [
        count as u32,
        (4294967296.0f32).to_bits(),
        0,
        (0.75f32).to_bits(),
    ];
    let result: usize;
    // SAFETY: live four-word local; all pointer uses precede the helper call.
    // The f32 spill precedes division. Do not round the extended quotient to
    // f32 before __ftol2. The native size_t keeps only EAX. Balanced x87 stack,
    // declared C clobbers, memory writes and stack use; no pointer escapes.
    unsafe {
        core::arch::asm!(
            "fild dword ptr [{data}]",
            "cmp dword ptr [{data}], 0",
            "jns 2f",
            "fadd dword ptr [{data} + 4]",
            "2:",
            "fstp dword ptr [{data} + 8]",
            "fld dword ptr [{data} + 8]",
            "fdiv dword ptr [{data} + 12]",
            "call {convert}",
            data = in(reg) data.as_mut_ptr(),
            convert = sym _ftol2,
            lateout("eax") result,
            clobber_abi("C"),
        );
    }
    result
}

#[cfg(target_arch = "x86_64")]
fn thresholds(bins: u32) -> (usize, usize) {
    let high: usize;
    let low: usize;
    let factors = [2.25f32, 0.25f32];
    // SAFETY: SSE2 is required on x64. The input is zero-extended u32; both
    // products are finite, positive and below i64::MAX, so native unsigned
    // conversion's large-value branch is unreachable for these fixed loads.
    // Preserve scalar f32 products, truncating conversions and SSE status.
    // Initialized factors stay live; all register and flag effects declared.
    unsafe {
        core::arch::asm!(
            "xorps xmm0, xmm0",
            "cvtsi2ss xmm0, {bins}",
            "movaps xmm1, xmm0",
            "mulss xmm1, dword ptr [{factors}]",
            "mulss xmm0, dword ptr [{factors} + 4]",
            "cvttss2si {high}, xmm1",
            "cvttss2si {low}, xmm0",
            bins = in(reg) u64::from(bins),
            factors = in(reg) factors.as_ptr(),
            high = lateout(reg) high,
            low = lateout(reg) low,
            out("xmm0") _, out("xmm1") _,
            options(readonly, nostack),
        );
    }
    (high, low)
}

#[cfg(target_arch = "x86_64")]
fn estimate(count: usize) -> usize {
    let factors = [0.75f32, 9223372036854775808.0f32];
    let result: usize;
    // SAFETY: scalar SSE2 with initialized local constants. Match the native
    // unsigned size_t -> f32 -> quotient -> size_t conversion, including its
    // high-half branches. No saturating Rust float cast or folded reciprocal.
    // Memory is read-only; all scratch registers and flags are declared.
    unsafe {
        core::arch::asm!(
            "xorps xmm0, xmm0",
            "test {count}, {count}",
            "js 2f",
            "cvtsi2ss xmm0, {count}",
            "jmp 3f",
            "2:",
            "mov rax, {count}",
            "and {count}, 1",
            "shr rax, 1",
            "or rax, {count}",
            "cvtsi2ss xmm0, rax",
            "addss xmm0, xmm0",
            "3:",
            "divss xmm0, dword ptr [{factors}]",
            "xor eax, eax",
            "movss xmm1, dword ptr [{factors} + 4]",
            "comiss xmm0, xmm1",
            "jb 4f",
            "subss xmm0, xmm1",
            "comiss xmm0, xmm1",
            "jae 4f",
            "mov rax, 0x8000000000000000",
            "4:",
            "cvttss2si {result}, xmm0",
            "add {result}, rax",
            count = inout(reg) count => _,
            factors = in(reg) factors.as_ptr(),
            result = lateout(reg) result,
            out("rax") _, out("xmm0") _, out("xmm1") _,
            options(readonly, nostack),
        );
    }
    result
}
