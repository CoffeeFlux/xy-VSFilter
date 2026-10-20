// SPDX-License-Identifier: GPL-2.0-or-later
//! Linked cleanup GaussianCoefficients arithmetic, MSVC 14.44. Fixed local
//! scalar slots only; x86 recurrence slots retain all 80 bits between steps.
use xy_vsfilter_compat::gaussian_coefficients::Numeric as Backend;
#[repr(align(16))]
pub(crate) struct Numeric {
    data: [u64; 32],
}
impl Numeric {
    pub(crate) fn new(sigma: u64) -> Self {
        let mut out = Self { data: [0; 32] };
        out.data[0] = sigma;
        out.data[24] = (-1.0f64).to_bits();
        out.data[25] = 3.0f64.to_bits();
        out.data[26] = 1.0f64.to_bits();
        out
    }
}
impl Backend for Numeric {
    fn begin(&mut self) -> Option<i32> {
        self.prepare();
        ((self.data[2] >> 32) != 0).then_some(self.data[2] as u32 as i32)
    }
    fn start(&mut self) {
        self.initialize();
    }
    fn next(&mut self) -> u32 {
        self.step();
        self.data[4] as u32
    }
    fn normalize(&mut self, bits: u32) -> u32 {
        self.data[4] = u64::from(bits);
        self.divide();
        self.data[4] as u32
    }
}
#[cfg(target_arch = "x86")]
extern "C" {
    fn ceil(v: f64) -> f64;
    fn _ftol2_sse() -> i32;
}
#[cfg(target_arch = "x86_64")]
extern "C" {
    fn exp(v: f64) -> f64;
}

#[cfg(target_arch = "x86")]
impl Numeric {
    fn prepare(&mut self) {
        // SAFETY: fixed initialized slots, balanced x87/temporary stack, scalar
        // CRT ceil/conversion only. EDI is preserved and C clobbers are explicit.
        unsafe {
            core::arch::asm!(r#"
            fld qword ptr [edi]
            fld st(0)
            fmul st(0),st(1)
            fadd st(0),st(0)
            fdivr qword ptr [edi + 192]
            fldl2e
            fmulp st(1),st(0)
            fld st(0)
            frndint
            fxch st(1)
            fsub st(0),st(1)
            f2xm1
            fld1
            faddp st(1),st(0)
            fscale
            fstp st(1)
            fstp qword ptr [edi + 8]
            fldz
            fucomp st(1)
            fnstsw ax
            test ah,0x44
            jnp 2f
            fmul qword ptr [edi + 200]
            sub esp,8
            fstp qword ptr [esp]
            call {ceil}
            add esp,8
            call {convert}
            mov dword ptr [edi + 16],eax
            mov dword ptr [edi + 20],1
            jmp 3f
            2: fstp st(0)
            3:
        "#,in("edi")self.data.as_mut_ptr(),ceil=sym ceil,convert=sym _ftol2_sse,clobber_abi("C"));
        }
    }
    fn initialize(&mut self) {
        // SAFETY: initialized fixed 80-bit slots; the stack is empty on return.
        unsafe {
            core::arch::asm!(r#"
            fld1
            fld qword ptr [edi + 8]
            fld st(0)
            fmul st(0),st(1)
            fld st(2)
            fstp tbyte ptr [edi + 112]
            fstp tbyte ptr [edi + 96]
            fstp tbyte ptr [edi + 80]
            fstp tbyte ptr [edi + 64]
        "#,in("edi")self.data.as_mut_ptr(),options(nostack));
        }
    }
    fn step(&mut self) {
        // SAFETY: fixed scalar slots only. Extended intermediates are stored
        // without narrowing; coefficient spills follow the captured f32 order.
        unsafe {
            core::arch::asm!(r#"
            fld tbyte ptr [edi + 64]
            fld tbyte ptr [edi + 80]
            fld tbyte ptr [edi + 96]
            fld tbyte ptr [edi + 112]
            fld st(2)
            fmulp st(4),st(0)
            fld st(1)
            fmulp st(3),st(0)
            fxch st(3)
            fst dword ptr [edi + 32]
            fld dword ptr [edi + 32]
            fst dword ptr [edi + 32]
            fstp dword ptr [edi + 32]
            fld st(0)
            faddp st(4),st(0)
            fadd st(3),st(0)
            fstp tbyte ptr [edi + 64]
            fstp tbyte ptr [edi + 96]
            fstp tbyte ptr [edi + 80]
            fstp tbyte ptr [edi + 112]
        "#,in("edi")self.data.as_mut_ptr(),options(nostack));
        }
    }
    fn divide(&mut self) {
        // SAFETY: fixed scalar operands; both x87 operands are popped.
        unsafe {
            core::arch::asm!(r#"
            fld tbyte ptr [edi + 112]
            fld dword ptr [edi + 32]
            fdiv st(0),st(1)
            fstp dword ptr [edi + 32]
            fstp st(0)
        "#,in("edi")self.data.as_mut_ptr(),options(nostack));
        }
    }
}
#[cfg(target_arch = "x86_64")]
impl Numeric {
    fn prepare(&mut self) {
        // SAFETY: fixed initialized scalar operands and explicit clobbers.
        unsafe {
            core::arch::asm!(r#"
            movsd xmm0,qword ptr [r10 + 192]
            movsd xmm2,qword ptr [r10]
            mulsd xmm2,xmm2
            addsd xmm2,xmm2
            divsd xmm0,xmm2
            movsd qword ptr [r10 + 8],xmm0
        "#,in("r10")self.data.as_mut_ptr(),out("xmm0")_,out("xmm2")_,options(nostack));
        }
        // SAFETY: CRT exp consumes and returns scalar doubles, with no memory
        // loan. This is the same CRT routine linked by the cleanup x64 binary.
        self.data[1] = unsafe { exp(f64::from_bits(self.data[1])) }.to_bits();
        // SAFETY: fixed initialized scalar operands; no variable addressing.
        unsafe {
            core::arch::asm!(r#"
            xorps xmm1,xmm1
            movsd xmm2,qword ptr [r10]
            ucomisd xmm1,xmm2
            jp 2f
            je 5f
            2: mulsd xmm2,qword ptr [r10 + 200]
            mov rax,0x8000000000000000
            xorps xmm0,xmm0
            cvttsd2si rcx,xmm2
            cmp rcx,rax
            je 4f
            xorps xmm0,xmm0
            cvtsi2sd xmm0,rcx
            ucomisd xmm0,xmm2
            je 4f
            unpcklpd xmm2,xmm2
            movmskpd eax,xmm2
            xorps xmm2,xmm2
            and eax,1
            xor eax,1
            add rcx,rax
            cvtsi2sd xmm2,rcx
            4: cvttsd2si ecx,xmm2
            mov dword ptr [r10 + 16],ecx
            mov dword ptr [r10 + 20],1
            5:
        "#,in("r10")self.data.as_mut_ptr(),out("rax")_,out("rcx")_,out("xmm0")_,out("xmm1")_,out("xmm2")_,options(nostack));
        }
    }
    fn initialize(&mut self) {
        // SAFETY: fixed local scalar slots only; all clobbers explicit.
        unsafe {
            core::arch::asm!(r#"
            movsd xmm0,qword ptr [r10 + 8]
            movsd qword ptr [r10 + 80],xmm0
            mulsd xmm0,xmm0
            movsd qword ptr [r10 + 96],xmm0
            movsd xmm0,qword ptr [r10 + 208]
            movsd qword ptr [r10 + 64],xmm0
            movsd qword ptr [r10 + 112],xmm0
        "#,in("r10")self.data.as_mut_ptr(),out("xmm0")_,options(nostack));
        }
    }
    fn step(&mut self) {
        // SAFETY: fixed initialized scalar slots only.
        unsafe {
            core::arch::asm!(r#"
            movsd xmm2,qword ptr [r10 + 64]
            movsd xmm1,qword ptr [r10 + 80]
            movsd xmm3,qword ptr [r10 + 112]
            mulsd xmm2,xmm1
            xorps xmm0,xmm0
            mulsd xmm1,qword ptr [r10 + 96]
            cvtsd2ss xmm0,xmm2
            addsd xmm3,xmm2
            movss dword ptr [r10 + 32],xmm0
            addsd xmm3,xmm2
            movsd qword ptr [r10 + 64],xmm2
            movsd qword ptr [r10 + 80],xmm1
            movsd qword ptr [r10 + 112],xmm3
        "#,in("r10")self.data.as_mut_ptr(),out("xmm0")_,out("xmm1")_,out("xmm2")_,out("xmm3")_,options(nostack));
        }
    }
    fn divide(&mut self) {
        // SAFETY: fixed local scalar slots only.
        unsafe {
            core::arch::asm!(r#"
            movss xmm0,dword ptr [r10 + 32]
            cvtps2pd xmm0,xmm0
            divsd xmm0,qword ptr [r10 + 112]
            cvtpd2ps xmm0,xmm0
            movss dword ptr [r10 + 32],xmm0
        "#,in("r10")self.data.as_mut_ptr(),out("xmm0")_,options(nostack));
        }
    }
}
