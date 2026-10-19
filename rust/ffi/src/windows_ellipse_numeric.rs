// SPDX-License-Identifier: GPL-2.0-or-later
//! Cleanup XyEllipse arithmetic, MSVC 14.44.35207. All memory operands address
//! fixed initialized scalar slots; safe Rust owns every arc/table and index.
use xy_vsfilter_compat::ellipse::Numeric as Backend;
#[repr(align(16))]
pub(crate) struct Numeric {
    data: [u64; 64],
}
impl Backend for Numeric {
    fn new(rx: i32, ry: i32) -> Self {
        let mut out = Self { data: [0; 64] };
        out.data[0] = u64::from(rx as u32) | (u64::from(ry as u32) << 32);
        out.data[40] = 0x3fe0000000000000; // f64 half
        out.data[41] = 0x3f0000003f800000; // f32 one, half
        out.data[44] = 0x7fffffff7fffffff;
        out.data[45] = 0x7fffffff7fffffff;
        out.prepare();
        out
    }
    fn table_setup(&mut self, ry: i32) {
        self.data[36] = u64::from(ry.wrapping_mul(ry) as u32);
        self.setup();
    }
    fn table_sample(&mut self, y: i32, dy: i32) -> i32 {
        self.data[32] = u64::from(y as u32) << 32;
        self.data[33] = u64::from(dy as u32);
        self.sample();
        self.data[32] as u32 as i32
    }
    fn analytic(&mut self, dx: i32, dy: i32) -> i32 {
        self.data[33] = u64::from(dy as u32) | (u64::from(dx as u32) << 32);
        self.intersect();
        self.data[32] as u32 as i32
    }
}
#[cfg(target_arch = "x86")]
extern "C" {
    fn ceil(value: f64) -> f64;
    fn _ftol2_sse() -> i32;
}
#[cfg(target_arch = "x86")]
impl Numeric {
    fn prepare(&mut self) {
        // SAFETY: the only address is this initialized aligned 512-byte local.
        // Fixed offsets only; no foreign pointer, table index or allocation.
        // x87 stacks balance on every branch; register clobbers are explicit.
        // x86 ceil/ftol calls use scalar arguments and balanced temporary stack.
        unsafe {
            core::arch::asm!(r#"
            fild dword ptr [edi]
            fstp dword ptr [edi + 128]
            fld dword ptr [edi + 128]
            fstp dword ptr [edi + 128]
            fld dword ptr [edi + 128]
            fst dword ptr [edi+8]
            fild dword ptr [edi + 4]
            fstp dword ptr [edi + 128]
            fld dword ptr [edi + 128]
            fstp dword ptr [edi + 128]
            fld dword ptr [edi + 128]
            fst dword ptr [edi+0x0C]
            fld qword ptr [edi + 320]
            cmp dword ptr [edi],0
            jle 2f
            fld st(0)
            fdivrp st(3),st(0)
            jmp 3f
            2: fstp st(2)
            fldz
            fxch st(2)
            3: fxch st(2)
            fstp qword ptr [edi+0x10]
            fdivp st(1),st(0)
            fstp qword ptr [edi+0x18]
        "#,in("edi") self.data.as_mut_ptr(),out("eax") _,out("edx") _,options(nostack));
        }
    }
    fn setup(&mut self) {
        // SAFETY: the only address is this initialized aligned 512-byte local.
        // Fixed offsets only; no foreign pointer, table index or allocation.
        // x87 stacks balance on every branch; register clobbers are explicit.
        // x86 ceil/ftol calls use scalar arguments and balanced temporary stack.
        unsafe {
            core::arch::asm!(r#"
            fld dword ptr [edi + 8]
            fdiv dword ptr [edi + 12]
            fstp dword ptr [edi + 128]
            fld dword ptr [edi + 128]
            fstp dword ptr [edi + 280]
            fild dword ptr [edi + 288]
            fstp dword ptr [edi + 128]
            fld dword ptr [edi + 128]
            fstp dword ptr [edi + 284]
        "#,in("edi") self.data.as_mut_ptr(),out("eax") _,out("edx") _,options(nostack));
        }
    }
    fn sample(&mut self) {
        // SAFETY: the only address is this initialized aligned 512-byte local.
        // Fixed offsets only; no foreign pointer, table index or allocation.
        // x87 stacks balance on every branch; register clobbers are explicit.
        // x86 ceil/ftol calls use scalar arguments and balanced temporary stack.
        unsafe {
            core::arch::asm!(r#"
            mov eax,dword ptr [edi + 260]
            mov dword ptr [edi + 160],eax
            sub eax,dword ptr [edi + 264]
            mov dword ptr [edi + 148],eax
            fld dword ptr [edi + 280]
            fstp qword ptr [edi + 184]
            fldz
            fld dword ptr [edi + 284]
            fst qword ptr [edi + 192]
            fild dword ptr [edi + 148]
            fstp dword ptr [edi + 148]
            fld dword ptr [edi + 148]
            fmul st(0),st(0)
            fsubr st(0),st(1)
            fstp dword ptr [edi + 148]
            fld dword ptr [edi + 148]
            fcom st(2)
            fnstsw ax
            test ah,5
            jp 2f
            fstp st(0)
            fxch st(1)
            fst dword ptr [edi + 148]
            fld dword ptr [edi + 148]
            fxch st(1)
            fxch st(2)
            fxch st(1)
            2: fild dword ptr [edi + 160]
            fstp dword ptr [edi + 160]
            fld dword ptr [edi + 160]
            fmul st(0),st(0)
            fsubp st(2),st(0)
            fxch st(1)
            fstp dword ptr [edi + 148]
            fld dword ptr [edi + 148]
            fcomp st(2)
            fnstsw ax
            test ah,5
            jp 3f
            fxch st(1)
            fstp dword ptr [edi + 148]
            jmp 4f
            3: fstp st(1)
            4:
            fstp dword ptr [edi + 480]
            fld dword ptr [edi + 480]
            fsqrt
            fstp dword ptr [edi + 480]
            fld dword ptr [edi + 480]
            fstp qword ptr [edi + 160]
            fld dword ptr [edi + 148]
            fstp dword ptr [edi + 480]
            fld dword ptr [edi + 480]
            fsqrt
            fstp dword ptr [edi + 480]
            fld dword ptr [edi + 480]
            fsubr qword ptr [edi + 160]
            fmul qword ptr [edi + 184]
            fstp dword ptr [edi + 160]
            fld dword ptr [edi + 160]
            sub esp,8
            fstp qword ptr [esp]
            call {ceil}
            add esp,8
            fstp dword ptr [edi + 160]
            fld dword ptr [edi + 160]
            call {convert}
            mov dword ptr [edi + 256],eax
        "#,in("edi") self.data.as_mut_ptr(),ceil=sym ceil,convert=sym _ftol2_sse,clobber_abi("C"));
        }
    }
    fn intersect(&mut self) {
        // SAFETY: the only address is this initialized aligned 512-byte local.
        // Fixed offsets only; no foreign pointer, table index or allocation.
        // x87 stacks balance on every branch; register clobbers are explicit.
        // x86 ceil/ftol calls use scalar arguments and balanced temporary stack.
        unsafe {
            core::arch::asm!(r#"
            mov edx,dword ptr [edi + 268]
            mov dword ptr [edi + 168],edx
            mov eax,dword ptr [edi + 264]
            mov dword ptr [edi + 172],eax
            fild dword ptr [edi + 168]
            fstp qword ptr [edi + 140]
            fld qword ptr [edi + 140]
            fmul qword ptr [edi + 16]
            fstp dword ptr [edi + 136]
            fld dword ptr [edi + 136]
            fst qword ptr [edi + 156]
            fild dword ptr [edi + 172]
            fstp qword ptr [edi + 140]
            fld qword ptr [edi + 140]
            fmul qword ptr [edi + 24]
            fstp dword ptr [edi + 140]
            fld dword ptr [edi + 140]
            fst qword ptr [edi + 148]
            fmul st(0),st(0)
            fld st(1)
            fmulp st(2),st(0)
            faddp st(1),st(0)
            fld1
            fld st(0)
            fdivrp st(2),st(0)
            fsubp st(1),st(0)
            fstp dword ptr [edi + 140]
            fldz
            fld dword ptr [edi + 140]
            fcom st(1)
            fnstsw ax
            fstp st(1)
            test ah,1
            jne 3f
            fstp dword ptr [edi + 480]
            fld dword ptr [edi + 480]
            fsqrt
            fstp dword ptr [edi + 480]
            fld dword ptr [edi + 480]
            fld dword ptr [edi + 136]
            fabs
            fld st(1)
            fld qword ptr [edi + 148]
            fmul st(1),st(0)
            fxch st(2)
            faddp st(1),st(0)
            fcomp qword ptr [edi + 344]
            fnstsw ax
            test ah,0x41
            jp 2f
            fld qword ptr [edi + 156]
            fmulp st(2),st(0)
            fsubrp st(1),st(0)
            fstp dword ptr [edi + 140]
            fld dword ptr [edi + 140]
            fmul dword ptr [edi + 12]
            fstp dword ptr [edi + 140]
            fld dword ptr [edi + 140]
            sub esp,8
            fstp qword ptr [esp]
            call {ceil}
            add esp,8
            fstp dword ptr [edi + 140]
            fld dword ptr [edi + 140]
            call {convert}
            jmp 4f
            2: fstp st(0)
            3: xor eax,eax
            fstp st(0)
            test edx,edx
            setg al
            add eax,0x7FFFFFFF
            4: mov dword ptr [edi + 256],eax
        "#,in("edi") self.data.as_mut_ptr(),ceil=sym ceil,convert=sym _ftol2_sse,clobber_abi("C"));
        }
    }
}
#[cfg(target_arch = "x86_64")]
impl Numeric {
    fn prepare(&mut self) {
        // SAFETY: the only address is this initialized aligned 512-byte local.
        // Fixed offsets only; no foreign pointer, table index or allocation.
        // x87 stacks balance on every branch; register clobbers are explicit.
        // x86 ceil/ftol calls use scalar arguments and balanced temporary stack.
        unsafe {
            core::arch::asm!(r#"
            mov eax,dword ptr [rdi]
            mov edx,dword ptr [rdi + 4]
            movd xmm2,eax
            cvtdq2ps xmm2,xmm2
            movss dword ptr [rdi+8],xmm2
            movd xmm3,edx
            cvtdq2ps xmm3,xmm3
            movss dword ptr [rdi+0x0C],xmm3
            movss xmm1,dword ptr [rdi + 332]
            test eax,eax
            jle 2f
            movaps xmm0,xmm1
            divss xmm0,xmm2
            jmp 3f
            2: xorps xmm0,xmm0
            3: cvtps2pd xmm0,xmm0
            movsd qword ptr [rdi+0x10],xmm0
            divss xmm1,xmm3
            cvtps2pd xmm0,xmm1
            movsd qword ptr [rdi+0x18],xmm0
        "#,in("rdi") self.data.as_mut_ptr(),out("eax") _,out("ecx") _,out("edx") _,out("r8") _,out("r10") _,out("xmm0") _,out("xmm1") _,out("xmm2") _,out("xmm3") _,out("xmm4") _,out("xmm5") _,out("xmm6") _,out("xmm7") _,options(nostack));
        }
    }
    fn setup(&mut self) {
        // SAFETY: the only address is this initialized aligned 512-byte local.
        // Fixed offsets only; no foreign pointer, table index or allocation.
        // x87 stacks balance on every branch; register clobbers are explicit.
        // x86 ceil/ftol calls use scalar arguments and balanced temporary stack.
        unsafe {
            core::arch::asm!(r#"
            movss xmm7,dword ptr [rdi + 8]
            divss xmm7,dword ptr [rdi + 12]
            movss dword ptr [rdi + 280],xmm7
            movd xmm6,dword ptr [rdi + 288]
            cvtdq2ps xmm6,xmm6
            movss dword ptr [rdi + 284],xmm6
        "#,in("rdi") self.data.as_mut_ptr(),out("eax") _,out("ecx") _,out("edx") _,out("r8") _,out("r10") _,out("xmm0") _,out("xmm1") _,out("xmm2") _,out("xmm3") _,out("xmm4") _,out("xmm5") _,out("xmm6") _,out("xmm7") _,options(nostack));
        }
    }
    fn sample(&mut self) {
        // SAFETY: the only address is this initialized aligned 512-byte local.
        // Fixed offsets only; no foreign pointer, table index or allocation.
        // x87 stacks balance on every branch; register clobbers are explicit.
        // x86 ceil/ftol calls use scalar arguments and balanced temporary stack.
        unsafe {
            core::arch::asm!(r#"
            mov r8d,dword ptr [rdi + 260]
            mov edx,r8d
            sub edx,dword ptr [rdi + 264]
            movss xmm6,dword ptr [rdi + 284]
            movss xmm7,dword ptr [rdi + 280]
            xorps xmm0,xmm0
            xorps xmm3,xmm3
            cvtsi2ss xmm0,edx
            xorps xmm2,xmm2
            movaps xmm1,xmm6
            mulss xmm0,xmm0
            cvtsi2ss xmm2,r8d
            subss xmm1,xmm0
            xorps xmm0,xmm0
            mulss xmm2,xmm2
            maxss xmm0,xmm1
            movaps xmm1,xmm6
            subss xmm1,xmm2
            sqrtss xmm3,xmm0
            xorps xmm0,xmm0
            maxss xmm0,xmm1
            sqrtss xmm0,xmm0
            subss xmm3,xmm0
            mulss xmm3,xmm7
            cvttss2si ecx,xmm3
            cmp ecx,0x80000000
            je 2f
            movd xmm0,ecx
            cvtdq2ps xmm0,xmm0
            ucomiss xmm0,xmm3
            je 2f
            unpcklps xmm3,xmm3
            movmskps eax,xmm3
            and eax,1
            xor eax,1
            add ecx,eax
            movd xmm3,ecx
            cvtdq2ps xmm3,xmm3
            2: cvttss2si eax,xmm3
            mov dword ptr [rdi + 256],eax
        "#,in("rdi") self.data.as_mut_ptr(),out("eax") _,out("ecx") _,out("edx") _,out("r8") _,out("r10") _,out("xmm0") _,out("xmm1") _,out("xmm2") _,out("xmm3") _,out("xmm4") _,out("xmm5") _,out("xmm6") _,out("xmm7") _,options(nostack));
        }
    }
    fn intersect(&mut self) {
        // SAFETY: the only address is this initialized aligned 512-byte local.
        // Fixed offsets only; no foreign pointer, table index or allocation.
        // x87 stacks balance on every branch; register clobbers are explicit.
        // x86 ceil/ftol calls use scalar arguments and balanced temporary stack.
        unsafe {
            core::arch::asm!(r#"
            mov edx,dword ptr [rdi + 268]
            mov r10d,dword ptr [rdi + 264]
            xorps xmm0,xmm0
            xorps xmm1,xmm1
            xorps xmm6,xmm6
            cvtsi2sd xmm0,r10d
            cvtsi2sd xmm1,edx
            mulsd xmm0,qword ptr [rdi + 24]
            mulsd xmm1,qword ptr [rdi + 16]
            cvtpd2ps xmm3,xmm0
            cvtpd2ps xmm4,xmm1
            movss xmm1,dword ptr [rdi + 328]
            movaps xmm0,xmm3
            movaps xmm2,xmm4
            mulss xmm0,xmm3
            mulss xmm2,xmm4
            movaps xmm5,xmm1
            addss xmm2,xmm0
            divss xmm5,xmm2
            subss xmm5,xmm1
            comiss xmm5,xmm6
            jb 3f
            xorps xmm2,xmm2
            movaps xmm1,xmm4
            andps xmm1,xmmword ptr [rdi + 352]
            sqrtss xmm2,xmm5
            movaps xmm0,xmm2
            mulss xmm0,xmm3
            addss xmm1,xmm0
            comiss xmm6,xmm1
            jb 3f
            mulss xmm2,xmm4
            subss xmm3,xmm2
            mulss xmm3,dword ptr [rdi + 12]
            cvttss2si ecx,xmm3
            cmp ecx,0x80000000
            je 2f
            movd xmm0,ecx
            cvtdq2ps xmm0,xmm0
            ucomiss xmm0,xmm3
            je 2f
            unpcklps xmm3,xmm3
            movmskps eax,xmm3
            and eax,1
            xor eax,1
            add ecx,eax
            movd xmm3,ecx
            cvtdq2ps xmm3,xmm3
            2: cvttss2si eax,xmm3
            jmp 4f
            3: test edx,edx
            mov eax,0x7FFFFFFF
            mov ecx,0x80000000
            cmovg eax,ecx
            4: mov dword ptr [rdi + 256],eax
        "#,in("rdi") self.data.as_mut_ptr(),out("eax") _,out("ecx") _,out("edx") _,out("r8") _,out("r10") _,out("xmm0") _,out("xmm1") _,out("xmm2") _,out("xmm3") _,out("xmm4") _,out("xmm5") _,out("xmm6") _,out("xmm7") _,options(nostack));
        }
    }
}
