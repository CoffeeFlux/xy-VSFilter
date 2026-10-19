// SPDX-License-Identifier: GPL-2.0-or-later
//! Fixed-size arithmetic for cleanup CWord::Transform (67da07cd), compiled with
//! MSVC 14.44.35207 Release; Win32 /arch:IA32, x64 SSE2. Keep instruction order,
//! spills, CRT conversions, NaN operand order and first-point x87 precision.
//! No foreign pointer, path buffer, allocation or variable index enters assembly.
use xy_vsfilter_compat::{
    path_align::Point,
    word_transform::{Backend, Parameters},
};
pub(crate) struct Numeric {
    data: [u64; 96],
}
impl Numeric {
    pub(crate) fn new(p: &Parameters) -> Self {
        let mut result = Self { data: [0; 96] };
        result.data[64..73].copy_from_slice(&p.values);
        result.data[73] = u64::from(p.origin.x as u32) | (u64::from(p.origin.y as u32) << 32);
        result.data[80..86].copy_from_slice(&[
            100.0f64.to_bits(),
            0x3f91df46a2529d39,
            20000.0f64.to_bits(),
            0.5f64.to_bits(),
            1000.0f64.to_bits(),
            1.0f64.to_bits(),
        ]);
        result.prepare();
        result
    }
}
impl Backend for Numeric {
    fn point(&mut self, p: Point, first: bool) -> Point {
        self.data[52] = u64::from(p.x as u32) | (u64::from(p.y as u32) << 32);
        self.convert(first);
        Point {
            x: self.data[53] as u32 as i32,
            y: (self.data[53] >> 32) as u32 as i32,
        }
    }
    fn needs_rounding(&self) -> bool {
        !equal_one(self.data[71]) || !equal_one(self.data[72])
    }
}
#[cfg(target_arch = "x86")]
extern "C" {
    fn _ftol2_sse() -> i32;
}
#[cfg(target_arch = "x86_64")]
extern "C" {
    fn cos(x: f64) -> f64;
    fn sin(x: f64) -> f64;
}

#[cfg(target_arch = "x86")]
impl Numeric {
    fn prepare(&mut self) {
        // SAFETY: EDI addresses 768 initialized bytes for the whole block. All
        // fixed accesses are within that buffer, with no aliases or calls. The
        // native x87 stack is balanced by storing its last value as raw 80 bits.
        unsafe {
            core::arch::asm!(r#"
                mov eax, dword ptr [edi + 584]
                mov dword ptr [edi + 28], eax
                neg eax
                mov dword ptr [edi + 16], eax
                fld qword ptr [edi + 512]
                fld qword ptr [edi + 640]
                fdiv st(1),st(0)
                fxch st(1)
                fstp qword ptr [edi + 72]
                fdivr qword ptr [edi + 520]
                fld qword ptr [edi + 528]
                fld qword ptr [edi + 648]
                fmul st(1),st(0)
                fld st(1)
                fcos
                fst qword ptr [edi + 48]
                fxch st(2)
                fsin
                fst qword ptr [edi + 64]
                fld qword ptr [edi + 536]
                fmul st(0),st(2)
                fld st(0)
                fcos
                fstp qword ptr [edi + 128]
                fsin
                fstp qword ptr [edi + 80]
                fld qword ptr [edi + 544]
                fmulp st(2),st(0)
                fld st(1)
                fcos
                fstp qword ptr [edi + 56]
                fxch st(1)
                fsin
                fstp qword ptr [edi + 96]
                fld qword ptr [edi + 568]
                fst qword ptr [edi + 104]
                fild dword ptr [edi + 16]
                fstp qword ptr [edi + 32]
                fdivr qword ptr [edi + 32]
                mov eax,dword ptr [edi + 588]
                mov dword ptr [edi + 32],eax
                neg eax
                mov dword ptr [edi + 16],eax
                fld qword ptr [edi + 576]
                fst qword ptr [edi + 120]
                fild dword ptr [edi + 16]
                fstp qword ptr [edi + 16]
                fdivr qword ptr [edi + 16]
                fxch st(2)
                fmul st(0),st(4)
                fstp qword ptr [edi + 16]
                fld qword ptr [edi + 560]
                fld qword ptr [edi + 72]
                fmulp st(4),st(0)
                fxch st(3)
                fstp qword ptr [edi + 88]
                fld qword ptr [edi + 552]
                fld st(2)
                fmul qword ptr [edi + 64]
                fld st(2)
                fmul qword ptr [edi + 48]
                faddp st(1),st(0)
                fstp qword ptr [edi + 112]
                fld qword ptr [edi + 48]
                fmulp st(5),st(0)
                fld qword ptr [edi + 64]
                fld st(0)
                fmul qword ptr [edi + 72]
                fld st(6)
                fmul st(0),st(6)
                fsub st(0),st(1)
                fst qword ptr [edi + 72]
                fxch st(1)
                fmul st(0),st(3)
                fsubp st(7),st(0)
                fxch st(6)
                fst qword ptr [edi + 64]
                fld qword ptr [edi + 48]
                fmulp st(5),st(0)
                fxch st(3)
                fmulp st(1),st(0)
                fsubp st(3),st(0)
                fxch st(2)
                fst qword ptr [edi + 136]
                fld qword ptr [edi + 80]
                fmul st(5),st(0)
                fmul st(2),st(0)
                fmulp st(1),st(0)
                fstp qword ptr [edi + 80]
                fld qword ptr [edi + 16]
                fmul st(3),st(0)
                fld qword ptr [edi + 88]
                fadd st(4),st(0)
                fmulp st(3),st(0)
                faddp st(2),st(0)
                fld st(3)
                fld qword ptr [edi + 96]
                fmul st(1),st(0)
                fld st(4)
                fld qword ptr [edi + 56]
                fmul st(1),st(0)
                fxch st(3)
                faddp st(1),st(0)
                fstp qword ptr [edi + 48]
                fmul st(4),st(0)
                fxch st(5)
                fmul st(0),st(1)
                fsubp st(4),st(0)
                fxch st(3)
                fstp qword ptr [edi + 88]
                fld st(1)
                fmul st(0),st(4)
                fld st(1)
                fmulp st(4),st(0)
                fsubrp st(3),st(0)
                fxch st(2)
                fstp qword ptr [edi + 96]
                fld qword ptr [edi + 656]
                fld qword ptr [edi + 104]
                fmul st(0),st(1)
                fld qword ptr [edi + 48]
                fmul st(0),st(1)
                fstp qword ptr [edi + 48]
                fxch st(3)
                fmul st(0),st(4)
                fld qword ptr [edi + 56]
                fmulp st(3),st(0)
                faddp st(2),st(0)
                fxch st(1)
                fmul st(0),st(2)
                fstp qword ptr [edi + 104]
                fld qword ptr [edi + 80]
                fld st(0)
                fmul st(0),st(4)
                fld qword ptr [edi + 112]
                fld st(0)
                fld qword ptr [edi + 56]
                fmul st(1),st(0)
                fxch st(3)
                faddp st(1),st(0)
                fmulp st(5),st(0)
                fxch st(4)
                fstp qword ptr [edi + 112]
                fld qword ptr [edi + 120]
                fmul st(0),st(3)
                fld qword ptr [edi + 72]
                fld qword ptr [edi + 128]
                fmul st(1),st(0)
                fxch st(1)
                fmul st(0),st(2)
                fstp qword ptr [edi + 120]
                fld qword ptr [edi + 64]
                fmul st(0),st(1)
                fmul st(0),st(2)
                fstp qword ptr [edi + 128]
                fmul qword ptr [edi + 136]
                fmulp st(1),st(0)
                fstp qword ptr [edi + 80]
                fxch st(3)
                fmulp st(4),st(0)
                fmulp st(2),st(0)
                fxch st(2)
                fsubrp st(1),st(0)
                faddp st(1),st(0)
                fst qword ptr [edi + 64]
                fild dword ptr [edi + 28]
                fstp qword ptr [edi + 136]
                fld qword ptr [edi + 136]
                fld qword ptr [edi + 664]
                fadd st(1),st(0)
                fxch st(1)
                fstp qword ptr [edi + 72]
                fild dword ptr [edi + 32]
                fstp qword ptr [edi + 136]
                fadd qword ptr [edi + 136]
                fstp qword ptr [edi + 136]
                fstp tbyte ptr [edi + 400]
            "#, in("edi") self.data.as_mut_ptr(), out("eax") _, options(nostack));
        }
    }
    fn convert(&mut self, first: bool) {
        // SAFETY: fixed initialized buffer as above. EDI survives the two CRT
        // conversions, C clobbers are declared, and the x87 stack is balanced.
        // First z22 retains 80 bits; subsequent points reload the native f64 spill.
        unsafe {
            core::arch::asm!(r#"
                test ecx, ecx
                jz 2f
                fld tbyte ptr [edi + 400]
                jmp 3f
            2: fld qword ptr [edi + 64]
            3: fld qword ptr [edi + 672]
                fld1
                fild dword ptr [edi + 416]
                fild dword ptr [edi + 420]
                fld qword ptr [edi + 96]
                fmul st(0),st(1)
                fld qword ptr [edi + 88]
                fmul st(0),st(3)
                faddp st(1),st(0)
                faddp st(5),st(0)
                fld qword ptr [edi + 104]
                fmul st(0),st(1)
                fld qword ptr [edi + 48]
                fmul st(0),st(3)
                faddp st(1),st(0)
                fadd qword ptr [edi + 112]
                fld qword ptr [edi + 128]
                fmulp st(2),st(0)
                fld qword ptr [edi + 120]
                fmulp st(3),st(0)
                fxch st(1)
                faddp st(2),st(0)
                fld qword ptr [edi + 80]
                faddp st(2),st(0)
                fxch st(4)
                fcom st(3)
                fnstsw ax
                test ah,0x41
                je 4f
                fstp st(0)
                fld st(2)
                4: fdiv st(4),st(0)
                fld qword ptr [edi + 72]
                faddp st(5),st(0)
                fxch st(4)
                call {convert}
                fdivrp st(3),st(0)
                mov dword ptr [edi + 424], eax
                fld qword ptr [edi + 136]
                faddp st(3),st(0)
                fxch st(2)
                call {convert}
                mov dword ptr [edi + 428], eax
                fstp st(0)
                fstp st(0)
            "#, in("edi") self.data.as_mut_ptr(), in("ecx") u32::from(first),
                convert=sym _ftol2_sse, clobber_abi("C"));
        }
    }
}
#[cfg(target_arch = "x86")]
fn equal_one(bits: u64) -> bool {
    crate::windows_float::equal_f64(f64::from_bits(bits), 1.0)
}

#[cfg(target_arch = "x86_64")]
impl Numeric {
    fn prepare(&mut self) {
        // SAFETY: RDI addresses the live initialized 768-byte buffer and survives
        // CRT cos/sin; every fixed load/store is in bounds. Calls have 32-byte
        // shadow space and preserve entry alignment. All used registers/C ABI
        // clobbers are declared. No callback or pointer escapes the scalar block.
        unsafe {
            core::arch::asm!(r#"
                sub rsp, 32
                movsd xmm7,qword ptr [rdi + 648]
                movsd xmm1,qword ptr [rdi + 520]
                divsd xmm1,qword ptr [rdi + 640]
                movsd xmm6,qword ptr [rdi + 528]
                movsd xmm11,qword ptr [rdi + 512]
                divsd xmm11,qword ptr [rdi + 640]
                mulsd xmm6,xmm7
                movsd qword ptr [rdi + 272],xmm1
                movaps xmm0,xmm6
                call {cosine}
                movaps xmm8,xmm0
                movaps xmm0,xmm6
                call {sine}
                movsd xmm6,qword ptr [rdi + 536]
                movaps xmm9,xmm0
                mulsd xmm6,xmm7
                movaps xmm0,xmm6
                call {cosine}
                movaps xmm15,xmm0
                movaps xmm0,xmm6
                call {sine}
                movsd xmm6,qword ptr [rdi + 544]
                movaps xmm10,xmm0
                mulsd xmm6,xmm7
                movaps xmm0,xmm6
                call {cosine}
                movaps xmm14,xmm0
                movaps xmm0,xmm6
                call {sine}
                mov edx,dword ptr [rdi + 584]
                xorps xmm4,xmm4
                mov ecx,dword ptr [rdi + 588]
                xorps xmm1,xmm1
                movsd xmm7,qword ptr [rdi + 568]
                movaps xmm13,xmm0
                movsd xmm12,qword ptr [rdi + 576]
                mov eax,edx
                movsd xmm2,qword ptr [rdi + 560]
                neg eax
                movsd xmm3,qword ptr [rdi + 552]
                cvtsi2sd xmm1,eax
                mov eax,ecx
                neg eax
                cvtsi2sd xmm4,eax
                divsd xmm1,xmm7
                divsd xmm4,xmm12
                movaps xmm0,xmm1
                mulsd xmm1,xmm9
                movaps xmm5,xmm4
                mulsd xmm0,xmm8
                mulsd xmm4,xmm8
                mulsd xmm5,xmm9
                subsd xmm4,xmm1
                movsd xmm1,qword ptr [rdi + 272]
                addsd xmm5,xmm0
                movaps xmm0,xmm1
                mulsd xmm0,xmm9
                movaps xmm6,xmm4
                movsd qword ptr [rdi + 288],xmm4
                mulsd xmm6,xmm10
                movsd qword ptr [rdi + 280],xmm5
                movaps xmm4,xmm0
                movaps xmm5,xmm11
                mulsd xmm4,xmm2
                mulsd xmm5,xmm8
                addsd xmm4,xmm5
                mulsd xmm5,xmm3
                mulsd xmm1,xmm8
                addsd xmm5,xmm0
                mulsd xmm11,xmm9
                movaps xmm8,xmm1
                movaps xmm1,xmm4
                mulsd xmm1,xmm13
                movaps xmm9,xmm8
                mulsd xmm9,xmm2
                mulsd xmm4,xmm14
                subsd xmm9,xmm11
                mulsd xmm11,xmm3
                movaps xmm2,xmm9
                subsd xmm8,xmm11
                mulsd xmm2,xmm10
                mulsd xmm9,xmm15
                movaps xmm11,xmm8
                mulsd xmm8,xmm15
                movaps xmm0,xmm2
                mulsd xmm11,xmm10
                mulsd xmm2,xmm13
                mulsd xmm0,xmm14
                addsd xmm2,xmm4
                subsd xmm1,xmm0
                movaps xmm0,xmm11
                mulsd xmm0,xmm14
                mulsd xmm11,xmm13
                movsd qword ptr [rdi + 40],xmm1
                movaps xmm1,xmm5
                mulsd xmm1,xmm13
                mulsd xmm5,xmm14
                subsd xmm1,xmm0
                movaps xmm0,xmm6
                mulsd xmm6,xmm14
                mulsd xmm0,xmm13
                addsd xmm11,xmm5
                movsd qword ptr [rdi + 48],xmm1
                movsd xmm1,qword ptr [rdi + 656]
                mulsd xmm7,xmm1
                mulsd xmm12,xmm1
                mulsd xmm2,xmm7
                mulsd xmm9,xmm12
                mulsd xmm11,xmm7
                movsd qword ptr [rdi + 272],xmm2
                movsd xmm2,qword ptr [rdi + 280]
                movaps xmm3,xmm2
                movsd qword ptr [rdi + 296],xmm9
                movsd xmm9,qword ptr [rdi + 288]
                mulsd xmm2,xmm13
                mulsd xmm3,xmm14
                mulsd xmm9,xmm15
                subsd xmm2,xmm6
                mulsd xmm8,xmm12
                addsd xmm3,xmm0
                mulsd xmm9,xmm12
                addsd xmm2,xmm1
                xorps xmm1,xmm1
                cvtsi2sd xmm1,edx
                xor edx,edx
                mulsd xmm3,xmm7
                addsd xmm1,qword ptr [rdi + 664]
                movsd qword ptr [rdi + 280],xmm2
                movsd qword ptr [rdi + 288],xmm1
                xorps xmm1,xmm1
                cvtsi2sd xmm1,ecx
                addsd xmm1,qword ptr [rdi + 664]
                movsd qword ptr [rdi + 56],xmm1
                movsd qword ptr [rdi + 320], xmm2
                movsd qword ptr [rdi + 328], xmm3
                movsd qword ptr [rdi + 336], xmm8
                movsd qword ptr [rdi + 344], xmm9
                movsd qword ptr [rdi + 352], xmm11
                add rsp, 32
            "#, in("rdi") self.data.as_mut_ptr(), cosine=sym cos, sine=sym sin,
                out("xmm0") _, out("xmm1") _, out("xmm2") _, out("xmm3") _, out("xmm4") _, out("xmm5") _, out("xmm6") _, out("xmm7") _, out("xmm8") _, out("xmm9") _, out("xmm10") _, out("xmm11") _, out("xmm12") _, out("xmm13") _, out("xmm14") _, out("xmm15") _, clobber_abi("C"));
        }
    }
    fn convert(&mut self, _first: bool) {
        // SAFETY: fixed initialized buffer, no calls/stack effects; SSE2 is
        // mandatory on x64. CVTTSD2SI preserves native invalid/overflow results.
        unsafe {
            core::arch::asm!(r#"
                movsd xmm2, qword ptr [rdi + 320]
                movsd xmm13, qword ptr [rdi + 328]
                movsd xmm8, qword ptr [rdi + 336]
                movsd xmm9, qword ptr [rdi + 344]
                movsd xmm11, qword ptr [rdi + 352]
                movsd xmm7, qword ptr [rdi + 672]
                movsd xmm10,qword ptr [rdi + 272]
                movsd xmm12,qword ptr [rdi + 296]
                movsd xmm14,qword ptr [rdi + 40]
                movsd xmm15,qword ptr [rdi + 48]
                xorps xmm4,xmm4
                movaps xmm0,xmm15
                xorps xmm3,xmm3
                movaps xmm1,xmm10
                cvtsi2sd xmm4,dword ptr [rdi + 416]
                cvtsi2sd xmm3,dword ptr [rdi + 420]
                mulsd xmm1,xmm4
                movaps xmm5,xmm4
                mulsd xmm0,xmm3
                mulsd xmm5,xmm14
                addsd xmm5,xmm0
                movaps xmm0,xmm11
                mulsd xmm0,xmm3
                addsd xmm5,xmm2
                movaps xmm2,xmm8
                addsd xmm1,xmm0
                mulsd xmm2,xmm3
                movaps xmm0,xmm12
                mulsd xmm0,xmm4
                addsd xmm1,xmm13
                maxsd xmm5,xmm7
                addsd xmm2,xmm0
                divsd xmm1,xmm5
                addsd xmm2,xmm9
                addsd xmm1,qword ptr [rdi + 288]
                divsd xmm2,xmm5
                cvttsd2si eax,xmm1
                addsd xmm2,qword ptr [rdi + 56]
                mov dword ptr [rdi + 424],eax
                cvttsd2si ecx,xmm2
                mov dword ptr [rdi + 428],ecx
            "#, in("rdi") self.data.as_mut_ptr(), out("eax") _, out("ecx") _,
                out("xmm0") _, out("xmm1") _, out("xmm2") _, out("xmm3") _, out("xmm4") _, out("xmm5") _, out("xmm6") _, out("xmm7") _, out("xmm8") _, out("xmm9") _, out("xmm10") _, out("xmm11") _, out("xmm12") _, out("xmm13") _, out("xmm14") _, out("xmm15") _, options(nostack));
        }
    }
}
#[cfg(target_arch = "x86_64")]
fn equal_one(bits: u64) -> bool {
    let equal: u8;
    let ordered: u8;
    let one = 1.0f64.to_bits();
    // SAFETY: two live initialized scalars; SSE2 comparison, declared clobbers.
    // An ordinary Rust comparison may optimize away denormal/SNaN side effects.
    unsafe {
        core::arch::asm!("movsd xmm0, qword ptr [{bits}]", "ucomisd xmm0, qword ptr [{one}]",
        "sete {equal}", "setnp {ordered}", bits=in(reg) &bits, one=in(reg) &one,
        equal=lateout(reg_byte) equal, ordered=lateout(reg_byte) ordered, out("xmm0") _, options(nostack,readonly));
    }
    equal != 0 && ordered != 0
}
