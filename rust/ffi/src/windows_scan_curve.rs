// SPDX-License-Identifier: GPL-2.0-or-later
//! Cleanup ScanLineData::_EvaluateBezier target arithmetic, MSVC 14.44.35207.
//! Fixed initialized local storage only; no path pointer or dynamic address.
//! Preserve first/subsequent x87 intermediates, spills, Y-before-X sample casts,
//! step comparisons and conditional X-before-Y initial origin casts.
use xy_vsfilter_compat::{path_align::Point, scan_convert::Curve};
#[repr(align(16))]
pub(crate) struct Numeric {
    data: [u64; 96],
}
impl Curve for Numeric {
    fn new(points: [Point; 4], spline: bool, first_set: bool) -> Self {
        let mut value = Self { data: [0; 96] };
        for (word, p) in value.data[64..68].iter_mut().zip(points) {
            *word = u64::from(p.x as u32) | (u64::from(p.y as u32) << 32);
        }
        value.data[69] = u64::from(spline) | (u64::from(first_set) << 32);
        value.data[80] = 0x3fc5555555555555;
        value.data[81] = 0x4008000000000000;
        value.data[82] = 0x4010000000000000;
        value.data[83] = 0x4018000000000000;
        value.data[84] = 0x4020000000000000;
        value.data[85] = 0x3ff0000000000000;
        value.data[88] = 0x7fffffffffffffff;
        value.data[89] = 0x7fffffffffffffff;
        value.prepare();
        value
    }
    fn origin(&self) -> Point {
        Point {
            x: self.data[54] as u32 as i32,
            y: (self.data[54] >> 32) as u32 as i32,
        }
    }
    fn point(&mut self) -> Point {
        self.sample();
        self.result()
    }
    fn advance(&mut self) -> bool {
        self.step();
        self.data[56] != 0
    }
    fn endpoint(&mut self) -> Point {
        self.finish();
        self.result()
    }
}
impl Numeric {
    fn result(&self) -> Point {
        Point {
            x: self.data[55] as u32 as i32,
            y: (self.data[55] >> 32) as u32 as i32,
        }
    }
}
#[cfg(target_arch = "x86")]
extern "C" {
    fn _ftol2_sse() -> i32;
}
#[cfg(target_arch = "x86")]
impl Numeric {
    fn prepare(&mut self) {
        // SAFETY: EDI addresses 768 initialized bytes. Fixed offsets only; the
        // x87 stack is balanced on every branch. C clobbers cover conversion
        // calls, which consume one x87 value and preserve EDI. No pointer escapes.
        unsafe {
            core::arch::asm!(r#"
                cmp dword ptr [edi + 552], 0
                fild dword ptr [edi + 512]
                fst qword ptr [edi + 40]
                fild dword ptr [edi + 520]
                fst qword ptr [edi + 104]
                fild dword ptr [edi + 528]
                fst qword ptr [edi + 112]
                fild dword ptr [edi + 516]
                fst qword ptr [edi + 32]
                fild dword ptr [edi + 524]
                fstp qword ptr [edi + 48]
                fild dword ptr [edi + 532]
                fstp qword ptr [edi + 88]
                fld st(2)
                fld qword ptr [edi + 648]
                fmul st(1),st(0)
                fxch st(1)
                fst qword ptr [edi + 24]
                fxch st(3)
                fmul st(0),st(1)
                fst qword ptr [edi + 96]
                fxch st(3)
                fsub st(0),st(5)
                fsub st(0),st(3)
                fild dword ptr [edi + 536]
                fstp qword ptr [edi + 80]
                fadd qword ptr [edi + 80]
                fld st(5)
                fmul st(0),st(2)
                fst qword ptr [edi + 56]
                fld qword ptr [edi + 664]
                fmulp st(6),st(0)
                fsubrp st(5),st(0)
                fxch st(4)
                faddp st(3),st(0)
                fld qword ptr [edi + 48]
                fmul st(0),st(1)
                fst qword ptr [edi + 16]
                fld qword ptr [edi + 88]
                fmul st(0),st(2)
                fst qword ptr [edi + 120]
                fxch st(1)
                fsub st(0),st(3)
                fsub st(0),st(1)
                fild dword ptr [edi + 540]
                fstp qword ptr [edi + 80]
                fadd qword ptr [edi + 80]
                fstp qword ptr [edi + 8]
                fld st(2)
                fmulp st(2),st(0)
                fxch st(1)
                fstp qword ptr [edi + 80]
                fld qword ptr [edi + 48]
                fmul qword ptr [edi + 664]
                fld qword ptr [edi + 80]
                fld st(0)
                fsubrp st(2),st(0)
                fxch st(1)
                faddp st(2),st(0)
                je 2f
                fstp st(0)
                fld qword ptr [edi + 640]
                fmul st(4),st(0)
                fxch st(4)
                fst qword ptr [edi + 72]
                fxch st(3)
                fmul st(0),st(4)
                fst qword ptr [edi + 64]
                fld qword ptr [edi + 96]
                fsub qword ptr [edi + 56]
                fmul st(0),st(5)
                fstp qword ptr [edi + 24]
                fld qword ptr [edi + 656]
                fld qword ptr [edi + 104]
                fmul st(0),st(1)
                faddp st(7),st(0)
                fld qword ptr [edi + 112]
                faddp st(7),st(0)
                fxch st(6)
                fmul st(0),st(5)
                fstp qword ptr [edi + 40]
                fld qword ptr [edi + 8]
                fmul st(0),st(5)
                fstp qword ptr [edi + 8]
                fxch st(1)
                fmul st(0),st(4)
                fst qword ptr [edi + 56]
                fld qword ptr [edi + 120]
                fsub qword ptr [edi + 80]
                fmul st(0),st(5)
                fstp qword ptr [edi + 16]
                fld qword ptr [edi + 48]
                fmulp st(6),st(0)
                fxch st(5)
                faddp st(2),st(0)
                fld qword ptr [edi + 88]
                faddp st(2),st(0)
                fxch st(1)
                fmulp st(3),st(0)
                fxch st(2)
                fstp qword ptr [edi + 32]
                fld qword ptr [edi + 40]
                jmp 3f
                2: fstp st(2)
                fxch st(3)
                fst qword ptr [edi + 72]
                fxch st(2)
                fst qword ptr [edi + 64]
                fld qword ptr [edi + 24]
                fsub qword ptr [edi + 56]
                fstp qword ptr [edi + 24]
                fld qword ptr [edi + 8]
                fstp qword ptr [edi + 8]
                fxch st(3)
                fst qword ptr [edi + 56]
                fld qword ptr [edi + 16]
                fsubrp st(2),st(0)
                fxch st(1)
                fstp qword ptr [edi + 16]
                fxch st(3)
                3: fld st(3)
                fadd st(0),st(4)
                fabs
                fld qword ptr [edi + 8]
                fmul qword ptr [edi + 664]
                fabs
                faddp st(1),st(0)
                fld st(3)
                fadd st(0),st(4)
                fabs
                fld st(3)
                fmul qword ptr [edi + 664]
                fabs
                faddp st(1),st(0)
                fcom st(1)
                fnstsw ax
                test ah,5
                jp 4f
                fstp st(0)
                jmp 5f
                4: fstp st(1)
                5: fld1
                fstp qword ptr [edi + 88]
                fld qword ptr [edi + 672]
                fcom st(1)
                fnstsw ax
                fld qword ptr [edi + 32]
                test ah,5
                jp 6f
                fxch st(1)
                fdivrp st(2),st(0)
                fxch st(1)
                fsqrt
                fstp qword ptr [edi + 88]
                jmp 7f
                6: fstp st(1)
                fstp st(1)
                7: cmp dword ptr [edi + 556], 0
                jne 8f
                fld st(1)
                call {convert}
                mov dword ptr [edi + 432], eax
                fld st(0)
                call {convert}
                mov dword ptr [edi + 436], eax
                8: fldz
                fst qword ptr [edi + 80]
                fstp tbyte ptr [edi + 256]
                fstp tbyte ptr [edi + 272]
                fstp tbyte ptr [edi + 288]
                fstp tbyte ptr [edi + 304]
                fstp tbyte ptr [edi + 320]
                fstp tbyte ptr [edi + 336]
            "#,in("edi") self.data.as_mut_ptr(),convert=sym _ftol2_sse,clobber_abi("C"));
        }
    }
    fn sample(&mut self) {
        // SAFETY: EDI addresses 768 initialized bytes. Fixed offsets only; the
        // x87 stack is balanced on every branch. C clobbers cover conversion
        // calls, which consume one x87 value and preserve EDI. No pointer escapes.
        unsafe {
            core::arch::asm!(r#"
                fld tbyte ptr [edi + 336]
                fld tbyte ptr [edi + 320]
                fld tbyte ptr [edi + 304]
                fld tbyte ptr [edi + 288]
                fld tbyte ptr [edi + 272]
                fld tbyte ptr [edi + 256]
                fld st(0)
                fmul qword ptr [edi + 8]
                faddp st(6),st(0)
                fmul st(5),st(0)
                fld qword ptr [edi + 16]
                faddp st(6),st(0)
                fmul st(5),st(0)
                fxch st(5)
                faddp st(1),st(0)
                call {convert}
                fld st(3)
                fmulp st(2),st(0)
                fxch st(1)
                mov dword ptr [edi + 444], eax
                faddp st(2),st(0)
                fxch st(1)
                fmul st(0),st(2)
                fadd qword ptr [edi + 24]
                fmulp st(2),st(0)
                faddp st(1),st(0)
                call {convert}
                mov dword ptr [edi + 440], eax
            "#,in("edi") self.data.as_mut_ptr(),convert=sym _ftol2_sse,clobber_abi("C"));
        }
    }
    fn step(&mut self) {
        // SAFETY: EDI addresses 768 initialized bytes. Fixed offsets only; the
        // x87 stack is balanced on every branch. C clobbers cover conversion
        // calls, which consume one x87 value and preserve EDI. No pointer escapes.
        unsafe {
            core::arch::asm!(r#"
                fld qword ptr [edi + 80]
                fadd qword ptr [edi + 88]
                fst qword ptr [edi + 80]
                fld1
                fcomp st(1)
                fnstsw ax
                fld qword ptr [edi + 64]
                fld qword ptr [edi + 56]
                fld qword ptr [edi + 40]
                fld qword ptr [edi + 32]
                fld qword ptr [edi + 72]
                test ah,0x41
                jz 2f
                mov dword ptr [edi + 448], 0
                fstp st(5)
                fstp st(3)
                fstp st(1)
                fstp st(0)
                fstp st(0)
                fstp st(0)
                jmp 3f
                2: fxch st(3)
                fxch st(5)
                fstp tbyte ptr [edi + 256]
                fstp tbyte ptr [edi + 272]
                fstp tbyte ptr [edi + 288]
                fstp tbyte ptr [edi + 304]
                fstp tbyte ptr [edi + 320]
                fstp tbyte ptr [edi + 336]
                mov dword ptr [edi + 448], 1
                3:
            "#,in("edi") self.data.as_mut_ptr(),out("eax") _,options(nostack));
        }
    }
    fn finish(&mut self) {
        // SAFETY: EDI addresses 768 initialized bytes. Fixed offsets only; the
        // x87 stack is balanced on every branch. C clobbers cover conversion
        // calls, which consume one x87 value and preserve EDI. No pointer escapes.
        unsafe {
            core::arch::asm!(r#"
                fld qword ptr [edi + 32]
                fadd qword ptr [edi + 16]
                fadd qword ptr [edi + 56]
                fadd qword ptr [edi + 8]
                call {convert}
                fld qword ptr [edi + 40]
                fadd qword ptr [edi + 24]
                mov dword ptr [edi + 444], eax
                fadd qword ptr [edi + 64]
                fadd qword ptr [edi + 72]
                call {convert}
                mov dword ptr [edi + 440], eax
            "#,in("edi") self.data.as_mut_ptr(),convert=sym _ftol2_sse,clobber_abi("C"));
        }
    }
}
#[cfg(target_arch = "x86_64")]
impl Numeric {
    fn prepare(&mut self) {
        // SAFETY: RDI addresses the 16-byte-aligned initialized 768-byte local.
        // All offsets are fixed/in bounds; MOVDQA's mask is aligned. No calls,
        // stack effects, path address or pointer escape; all clobbers declared.
        unsafe {
            core::arch::asm!(r#"
                xorps xmm0,xmm0
                movsd xmm1,qword ptr [rdi + 648]
                xorps xmm3,xmm3
                movsd xmm4,qword ptr [rdi + 664]
                xorps xmm5,xmm5
                xorps xmm6,xmm6
                xorps xmm7,xmm7
                xorps xmm8,xmm8
                cvtsi2sd xmm0,dword ptr [rdi + 528]
                cvtsi2sd xmm3,dword ptr [rdi + 532]
                movsd qword ptr [rdi + 240],xmm0
                movaps xmm9,xmm0
                mulsd xmm9,xmm1
                xorps xmm0,xmm0
                movsd qword ptr [rdi + 248],xmm3
                movaps xmm10,xmm3
                mulsd xmm10,xmm1
                cvtsi2sd xmm0,dword ptr [rdi + 536]
                cvtsi2sd xmm7,dword ptr [rdi + 520]
                cvtsi2sd xmm5,dword ptr [rdi + 512]
                movaps xmm15,xmm7
                mulsd xmm15,xmm1
                movaps xmm2,xmm5
                mulsd xmm2,xmm1
                cvtsi2sd xmm8,dword ptr [rdi + 524]
                movaps xmm13,xmm15
                movaps xmm11,xmm2
                subsd xmm13,xmm5
                cvtsi2sd xmm6,dword ptr [rdi + 516]
                subsd xmm13,xmm9
                movaps xmm3,xmm6
                mulsd xmm3,xmm1
                addsd xmm13,xmm0
                movaps xmm0,xmm7
                mulsd xmm0,xmm4
                movaps xmm12,xmm3
                subsd xmm11,xmm0
                movaps xmm0,xmm8
                mulsd xmm0,xmm1
                addsd xmm11,xmm9
                movsd qword ptr [rdi + 48],xmm0
                movaps xmm14,xmm0
                subsd xmm14,xmm6
                xorps xmm0,xmm0
                cvtsi2sd xmm0,dword ptr [rdi + 540]
                subsd xmm14,xmm10
                addsd xmm14,xmm0
                movaps xmm0,xmm8
                mulsd xmm0,xmm4
                subsd xmm12,xmm0
                addsd xmm12,xmm10
                cmp dword ptr [rdi + 552], 0
                je 2f
                movsd xmm1,qword ptr [rdi + 640]
                subsd xmm9,xmm2
                mulsd xmm7,qword ptr [rdi + 656]
                subsd xmm10,xmm3
                mulsd xmm8,qword ptr [rdi + 656]
                mulsd xmm13,xmm1
                addsd xmm7,xmm5
                mulsd xmm11,xmm1
                addsd xmm8,xmm6
                mulsd xmm9,xmm1
                mulsd xmm14,xmm1
                addsd xmm7,qword ptr [rdi + 240]
                addsd xmm8,qword ptr [rdi + 248]
                mulsd xmm12,xmm1
                mulsd xmm10,xmm1
                mulsd xmm7,xmm1
                mulsd xmm8,xmm1
                jmp 3f
                2: movsd xmm10,qword ptr [rdi + 48]
                movaps xmm9,xmm15
                subsd xmm9,xmm2
                movaps xmm7,xmm5
                subsd xmm10,xmm3
                movaps xmm8,xmm6
                3: movdqa xmm2,xmmword ptr [rdi + 704]
                movaps xmm3,xmm12
                movsd xmm15,qword ptr [rdi + 680]
                addsd xmm3,xmm12
                movaps xmm0,xmm14
                movaps xmm1,xmm11
                mulsd xmm0,xmm4
                addsd xmm1,xmm11
                andps xmm3,xmm2
                andps xmm0,xmm2
                andps xmm1,xmm2
                addsd xmm3,xmm0
                movaps xmm0,xmm13
                mulsd xmm0,xmm4
                andps xmm0,xmm2
                addsd xmm1,xmm0
                movsd xmm0,qword ptr [rdi + 672]
                maxsd xmm3,xmm1
                comisd xmm3,xmm0
                jbe 4f
                divsd xmm0,xmm3
                sqrtpd xmm15,xmm0
                4: cmp dword ptr [rdi + 556], 0
                jne 5f
                cvttsd2si eax, xmm7
                mov dword ptr [rdi + 432], eax
                cvttsd2si eax, xmm8
                mov dword ptr [rdi + 436], eax
                5: xorps xmm6,xmm6
                movsd qword ptr [rdi + 256], xmm6
                movsd qword ptr [rdi + 264], xmm7
                movsd qword ptr [rdi + 272], xmm8
                movsd qword ptr [rdi + 280], xmm9
                movsd qword ptr [rdi + 288], xmm10
                movsd qword ptr [rdi + 296], xmm11
                movsd qword ptr [rdi + 304], xmm12
                movsd qword ptr [rdi + 312], xmm13
                movsd qword ptr [rdi + 320], xmm14
                movsd qword ptr [rdi + 328], xmm15
            "#,in("rdi") self.data.as_mut_ptr(),out("eax") _,out("r9") _,out("xmm0") _, out("xmm1") _, out("xmm2") _, out("xmm3") _, out("xmm4") _, out("xmm5") _, out("xmm6") _, out("xmm7") _, out("xmm8") _, out("xmm9") _, out("xmm10") _, out("xmm11") _, out("xmm12") _, out("xmm13") _, out("xmm14") _, out("xmm15") _,options(nostack));
        }
    }
    fn sample(&mut self) {
        // SAFETY: RDI addresses the 16-byte-aligned initialized 768-byte local.
        // All offsets are fixed/in bounds; MOVDQA's mask is aligned. No calls,
        // stack effects, path address or pointer escape; all clobbers declared.
        unsafe {
            core::arch::asm!(r#"
                movsd xmm6, qword ptr [rdi + 256]
                movsd xmm7, qword ptr [rdi + 264]
                movsd xmm8, qword ptr [rdi + 272]
                movsd xmm9, qword ptr [rdi + 280]
                movsd xmm10, qword ptr [rdi + 288]
                movsd xmm11, qword ptr [rdi + 296]
                movsd xmm12, qword ptr [rdi + 304]
                movsd xmm13, qword ptr [rdi + 312]
                movsd xmm14, qword ptr [rdi + 320]
                movsd xmm15, qword ptr [rdi + 328]
                movaps xmm0,xmm6
                movaps xmm1,xmm6
                mulsd xmm0,xmm14
                mulsd xmm1,xmm13
                addsd xmm0,xmm12
                addsd xmm1,xmm11
                mulsd xmm0,xmm6
                mulsd xmm1,xmm6
                addsd xmm0,xmm10
                addsd xmm1,xmm9
                mulsd xmm0,xmm6
                mulsd xmm1,xmm6
                addsd xmm0,xmm8
                addsd xmm1,xmm7
                cvttsd2si eax,xmm0
                cvttsd2si r9d,xmm1
                mov dword ptr [rdi + 444], eax
                mov dword ptr [rdi + 440], r9d
            "#,in("rdi") self.data.as_mut_ptr(),out("eax") _,out("r9") _,out("xmm0") _, out("xmm1") _, out("xmm2") _, out("xmm3") _, out("xmm4") _, out("xmm5") _, out("xmm6") _, out("xmm7") _, out("xmm8") _, out("xmm9") _, out("xmm10") _, out("xmm11") _, out("xmm12") _, out("xmm13") _, out("xmm14") _, out("xmm15") _,options(nostack));
        }
    }
    fn step(&mut self) {
        // SAFETY: RDI addresses the 16-byte-aligned initialized 768-byte local.
        // All offsets are fixed/in bounds; MOVDQA's mask is aligned. No calls,
        // stack effects, path address or pointer escape; all clobbers declared.
        unsafe {
            core::arch::asm!(r#"
                movsd xmm6, qword ptr [rdi + 256]
                movsd xmm7, qword ptr [rdi + 264]
                movsd xmm8, qword ptr [rdi + 272]
                movsd xmm9, qword ptr [rdi + 280]
                movsd xmm10, qword ptr [rdi + 288]
                movsd xmm11, qword ptr [rdi + 296]
                movsd xmm12, qword ptr [rdi + 304]
                movsd xmm13, qword ptr [rdi + 312]
                movsd xmm14, qword ptr [rdi + 320]
                movsd xmm15, qword ptr [rdi + 328]
                movsd xmm0, qword ptr [rdi + 680]
                addsd xmm6,xmm15
                comisd xmm0,xmm6
                seta al
                movzx eax,al
                mov dword ptr [rdi + 448],eax
                movsd qword ptr [rdi + 256],xmm6
            "#,in("rdi") self.data.as_mut_ptr(),out("eax") _,out("r9") _,out("xmm0") _, out("xmm1") _, out("xmm2") _, out("xmm3") _, out("xmm4") _, out("xmm5") _, out("xmm6") _, out("xmm7") _, out("xmm8") _, out("xmm9") _, out("xmm10") _, out("xmm11") _, out("xmm12") _, out("xmm13") _, out("xmm14") _, out("xmm15") _,options(nostack));
        }
    }
    fn finish(&mut self) {
        // SAFETY: RDI addresses the 16-byte-aligned initialized 768-byte local.
        // All offsets are fixed/in bounds; MOVDQA's mask is aligned. No calls,
        // stack effects, path address or pointer escape; all clobbers declared.
        unsafe {
            core::arch::asm!(r#"
                movsd xmm6, qword ptr [rdi + 256]
                movsd xmm7, qword ptr [rdi + 264]
                movsd xmm8, qword ptr [rdi + 272]
                movsd xmm9, qword ptr [rdi + 280]
                movsd xmm10, qword ptr [rdi + 288]
                movsd xmm11, qword ptr [rdi + 296]
                movsd xmm12, qword ptr [rdi + 304]
                movsd xmm13, qword ptr [rdi + 312]
                movsd xmm14, qword ptr [rdi + 320]
                movsd xmm15, qword ptr [rdi + 328]
                addsd xmm8,xmm10
                addsd xmm7,xmm9
                addsd xmm8,xmm12
                addsd xmm7,xmm11
                addsd xmm8,xmm14
                addsd xmm7,xmm13
                cvttsd2si eax,xmm8
                cvttsd2si r9d,xmm7
                mov dword ptr [rdi + 444], eax
                mov dword ptr [rdi + 440], r9d
            "#,in("rdi") self.data.as_mut_ptr(),out("eax") _,out("r9") _,out("xmm0") _, out("xmm1") _, out("xmm2") _, out("xmm3") _, out("xmm4") _, out("xmm5") _, out("xmm6") _, out("xmm7") _, out("xmm8") _, out("xmm9") _, out("xmm10") _, out("xmm11") _, out("xmm12") _, out("xmm13") _, out("xmm14") _, out("xmm15") _,options(nostack));
        }
    }
}
