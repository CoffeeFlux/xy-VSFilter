// SPDX-License-Identifier: GPL-2.0-or-later
//! Scalar arithmetic from the linked cleanup BeBlur/xy_be_blur, MSVC 14.44.
//! All assembly addresses are fixed slots in an initialized 256-byte local.
//! No image pointer, dimension, allocation or traversal enters this boundary.
use xy_vsfilter_compat::be_blur::Numeric as Backend;

#[repr(align(16))]
pub(crate) struct Numeric {
    data: [u32; 64],
}
impl Numeric {
    pub(crate) fn new(be: u32, sx: u32, sy: u32) -> Self {
        let mut out = Self { data: [0; 64] };
        out.data[..3].copy_from_slice(&[be, sx, sy]);
        for (i, value) in [0.25f64, 0.5, 1.0, 3.0, 256.0].into_iter().enumerate() {
            let bits = value.to_bits();
            out.data[32 + i * 2] = bits as u32;
            out.data[33 + i * 2] = (bits >> 32) as u32;
        }
        for (i, value) in [0.25f32, 0.5, 1.0, 3.0, 256.0].into_iter().enumerate() {
            out.data[42 + i] = value.to_bits();
        }
        out
    }
}
impl Backend for Numeric {
    fn geometry(&mut self) -> [i32; 2] {
        self.prepare();
        [self.data[3] as i32, self.data[4] as i32]
    }
    fn passes(&mut self) -> Option<i32> {
        self.scale();
        (self.data[12] != 0).then_some(self.data[6] as i32)
    }
    fn fraction(&mut self) -> Option<u32> {
        self.tail();
        (self.data[12] != 0).then_some(self.data[7])
    }
    fn axis_passes(&mut self, pass: u32) -> i32 {
        self.data[8] = pass;
        self.convert_axis();
        self.data[9] as i32
    }
    fn axis_filter(&mut self) -> Option<[u32; 2]> {
        self.filter();
        if self.data[12] == 0 {
            return None;
        }
        let side = self.data[10];
        let middle = 256u32.wrapping_sub(side.wrapping_mul(2));
        Some(if side.wrapping_mul(2) > middle {
            [64, 128]
        } else {
            [side, middle]
        })
    }
}

#[cfg(target_arch = "x86")]
extern "C" {
    fn _ftol2_sse() -> i32;
    fn _ftol2() -> i64;
}

#[cfg(target_arch = "x86")]
impl Numeric {
    fn prepare(&mut self) {
        // SAFETY: fixed initialized slots, balanced x87 stack, scalar CRT
        // conversions only. EDI is callee-preserved; all C clobbers declared.
        unsafe {
            core::arch::asm!(r#"
            fld dword ptr [edi]
            fst qword ptr [edi + 64]
            fmul qword ptr [edi + 128]
            fstp dword ptr [edi + 52]
            fld dword ptr [edi + 52]
            fstp dword ptr [edi + 52]
            fld dword ptr [edi + 52]
            fsqrt
            fstp dword ptr [edi + 52]
            fld dword ptr [edi + 52]
            fld dword ptr [edi + 4]
            fst qword ptr [edi + 72]
            fmul st(0),st(1)
            fld qword ptr [edi + 136]
            fadd st(1),st(0)
            fxch st(1)
            call {convert}
            mov dword ptr [edi + 12],eax
            fld dword ptr [edi + 8]
            fst qword ptr [edi + 80]
            fmulp st(2),st(0)
            faddp st(1),st(0)
            call {convert}
            mov dword ptr [edi + 16],eax
        "#, in("edi") self.data.as_mut_ptr(), convert=sym _ftol2_sse, clobber_abi("C"));
        }
    }
    fn scale(&mut self) {
        // SAFETY: fixed local slots and scalar CRT conversion; balanced stacks.
        unsafe {
            core::arch::asm!(r#"
            mov dword ptr [edi + 48],0
            fldz
            fcomp dword ptr [edi]
            fnstsw ax
            test ah,1
            je 2f
            fld qword ptr [edi + 80]
            fadd qword ptr [edi + 72]
            fld qword ptr [edi + 64]
            fmul qword ptr [edi + 136]
            fmulp st(1),st(0)
            fstp dword ptr [edi + 20]
            fld dword ptr [edi + 20]
            call {convert}
            mov dword ptr [edi + 24],eax
            mov dword ptr [edi + 48],1
            2:
        "#, in("edi") self.data.as_mut_ptr(), convert=sym _ftol2_sse, clobber_abi("C"));
        }
    }
    fn tail(&mut self) {
        // SAFETY: fixed local slots; every branch pops both comparison operands.
        unsafe {
            core::arch::asm!(r#"
            mov dword ptr [edi + 48],0
            fld dword ptr [edi + 20]
            fild dword ptr [edi + 24]
            fstp dword ptr [edi + 52]
            fld dword ptr [edi + 52]
            fcom st(1)
            fnstsw ax
            test ah,5
            jp 2f
            fsubp st(1),st(0)
            fstp dword ptr [edi + 52]
            fld dword ptr [edi + 52]
            fst dword ptr [edi + 28]
            fstp dword ptr [edi + 28]
            mov dword ptr [edi + 48],1
            jmp 3f
            2: fstp st(0)
            fstp st(0)
            3:
        "#, in("edi") self.data.as_mut_ptr(), out("eax") _, options(nostack));
        }
    }
    fn convert_axis(&mut self) {
        // SAFETY: fixed slots, balanced x87 stack and scalar CRT conversion.
        unsafe {
            core::arch::asm!(r#"
            fld dword ptr [edi + 32]
            fld st(0)
            call {convert}
            mov dword ptr [edi + 36],eax
            fstp st(0)
        "#, in("edi") self.data.as_mut_ptr(), convert=sym _ftol2_sse, clobber_abi("C"));
        }
    }
    fn filter(&mut self) {
        // SAFETY: fixed local slots, scalar CRT conversions, balanced x87 stack
        // on every branch. No caller-owned memory or variable addressing.
        unsafe {
            core::arch::asm!(r#"
            mov dword ptr [edi + 48],0
            fld dword ptr [edi + 32]
            fld st(0)
            fild dword ptr [edi + 36]
            fstp dword ptr [edi + 52]
            fsub dword ptr [edi + 52]
            fldz
            fcompp
            fnstsw ax
            test ah,5
            jp 6f
            fstp dword ptr [edi + 52]
            fld dword ptr [edi + 52]
            fld st(0)
            call {convert}
            mov dword ptr [edi + 52],eax
            test eax,eax
            jne 2f
            fld qword ptr [edi + 160]
            fmul st(0),st(1)
            fxch st(1)
            fmul qword ptr [edi + 152]
            fadd qword ptr [edi + 144]
            jmp 5f
            2: cmp eax,1
            jne 3f
            fld qword ptr [edi + 144]
            fsubr st(0),st(1)
            fmul qword ptr [edi + 160]
            jmp 4f
            3: fild dword ptr [edi + 52]
            add eax,2
            fstp dword ptr [edi + 52]
            fld dword ptr [edi + 52]
            fld st(1)
            mov dword ptr [edi + 52],eax
            fmul qword ptr [edi + 152]
            fadd qword ptr [edi + 144]
            fsub st(0),st(1)
            fxch st(2)
            fsubrp st(1),st(0)
            fmul qword ptr [edi + 160]
            fild dword ptr [edi + 52]
            fstp dword ptr [edi + 52]
            fmul dword ptr [edi + 52]
            4: fxch st(1)
            fadd st(0),st(0)
            5: fdivp st(1),st(0)
            call {unsigned_convert}
            mov dword ptr [edi + 40],eax
            mov dword ptr [edi + 48],1
            jmp 7f
            6: fstp st(0)
            7:
        "#, in("edi") self.data.as_mut_ptr(), convert=sym _ftol2_sse,
            unsigned_convert=sym _ftol2, clobber_abi("C"));
        }
    }
}

#[cfg(target_arch = "x86_64")]
impl Numeric {
    fn prepare(&mut self) {
        // SAFETY: fixed initialized local slots; explicit scalar/SIMD clobbers.
        unsafe {
            core::arch::asm!(r#"
            movss xmm0,dword ptr [r10]
            mulss xmm0,dword ptr [r10 + 168]
            xorps xmm3,xmm3
            sqrtss xmm3,xmm0
            movss xmm0,dword ptr [r10 + 4]
            mulss xmm0,xmm3
            cvtps2pd xmm1,xmm0
            addsd xmm1,qword ptr [r10 + 136]
            cvttsd2si eax,xmm1
            mov dword ptr [r10 + 12],eax
            movss xmm0,dword ptr [r10 + 8]
            mulss xmm0,xmm3
            cvtps2pd xmm1,xmm0
            addsd xmm1,qword ptr [r10 + 136]
            cvttsd2si eax,xmm1
            mov dword ptr [r10 + 16],eax
        "#, in("r10") self.data.as_mut_ptr(), out("rax") _, out("xmm0") _, out("xmm1") _, out("xmm3") _, options(nostack));
        }
    }
    fn scale(&mut self) {
        // SAFETY: fixed local slots only; no calls or stack access.
        unsafe {
            core::arch::asm!(r#"
            mov dword ptr [r10 + 48],0
            xorps xmm0,xmm0
            movss xmm1,dword ptr [r10]
            comiss xmm0,xmm1
            jae 2f
            movss xmm0,dword ptr [r10 + 4]
            addss xmm0,dword ptr [r10 + 8]
            mulss xmm1,dword ptr [r10 + 172]
            mulss xmm0,xmm1
            movss dword ptr [r10 + 20],xmm0
            cvttss2si eax,xmm0
            mov dword ptr [r10 + 24],eax
            mov dword ptr [r10 + 48],1
            2:
        "#, in("r10") self.data.as_mut_ptr(), out("rax") _, out("xmm0") _, out("xmm1") _, options(nostack));
        }
    }
    fn tail(&mut self) {
        // SAFETY: fixed local slots only; scalar/SIMD clobbers explicit.
        unsafe {
            core::arch::asm!(r#"
            mov dword ptr [r10 + 48],0
            movd xmm0,dword ptr [r10 + 24]
            cvtdq2ps xmm0,xmm0
            movss xmm1,dword ptr [r10 + 20]
            comiss xmm1,xmm0
            jbe 2f
            subss xmm1,xmm0
            movss dword ptr [r10 + 28],xmm1
            mov dword ptr [r10 + 48],1
            2:
        "#, in("r10") self.data.as_mut_ptr(), out("xmm0") _, out("xmm1") _, options(nostack));
        }
    }
    fn convert_axis(&mut self) {
        // SAFETY: fixed local slots only.
        unsafe {
            core::arch::asm!(r#"
            cvttss2si eax,dword ptr [r10 + 32]
            mov dword ptr [r10 + 36],eax
        "#, in("r10") self.data.as_mut_ptr(), out("rax") _, options(nostack));
        }
    }
    fn filter(&mut self) {
        // SAFETY: fixed initialized slots only, with all register clobbers.
        unsafe {
            core::arch::asm!(r#"
            mov dword ptr [r10 + 48],0
            movd xmm0,dword ptr [r10 + 36]
            cvtdq2ps xmm0,xmm0
            movss xmm2,dword ptr [r10 + 32]
            movaps xmm1,xmm2
            subss xmm1,xmm0
            xorps xmm3,xmm3
            comiss xmm1,xmm3
            jbe 5f
            mov eax,dword ptr [r10 + 36]
            test eax,eax
            jne 2f
            movaps xmm0,xmm2
            mulss xmm2,dword ptr [r10 + 180]
            mulss xmm0,dword ptr [r10 + 184]
            addss xmm2,dword ptr [r10 + 176]
            divss xmm0,xmm2
            jmp 4f
            2: cmp eax,1
            jne 3f
            movaps xmm0,xmm2
            subss xmm0,dword ptr [r10 + 176]
            mulss xmm0,dword ptr [r10 + 184]
            jmp 6f
            3: mulss xmm2,dword ptr [r10 + 180]
            add eax,2
            mulss xmm1,dword ptr [r10 + 184]
            addss xmm2,dword ptr [r10 + 176]
            subss xmm2,xmm0
            movd xmm0,eax
            cvtdq2ps xmm0,xmm0
            mulss xmm0,xmm1
            6: addss xmm2,xmm2
            divss xmm0,xmm2
            4: cvttss2si rax,xmm0
            mov dword ptr [r10 + 40],eax
            mov dword ptr [r10 + 48],1
            5:
        "#, in("r10") self.data.as_mut_ptr(), out("rax") _, out("xmm0") _, out("xmm1") _, out("xmm2") _, out("xmm3") _, options(nostack));
        }
    }
}
