// SPDX-License-Identifier: GPL-2.0-or-later
//! Scalar key copies, comparisons, table-growth arithmetic and Gaussian sigma
//! products from linked cleanup. All addresses are fixed initialized local slots.
use xy_vsfilter_compat::gaussian_cache::Numeric as Backend;
#[repr(align(16))]
pub(crate) struct Numeric {
    data: [u64; 8],
}
impl Numeric {
    pub(crate) fn new() -> Self {
        Self { data: [0; 8] }
    }
    pub(crate) fn products(&mut self, sigma: u64, sx: u64, sy: u64) -> [u64; 2] {
        self.data[..3].copy_from_slice(&[sigma, sx, sy]);
        #[cfg(target_arch = "x86")]
        // SAFETY: fixed initialized scalar slots and balanced x87 stack.
        unsafe {
            core::arch::asm!(
            "fld qword ptr [{p}]","fld qword ptr [{p}+8]","fmul st(0),st(1)",
            "fstp qword ptr [{p}+24]","fmul qword ptr [{p}+16]","fstp qword ptr [{p}+32]",
            p=in(reg)self.data.as_mut_ptr(),options(nostack));
        }
        #[cfg(target_arch = "x86_64")]
        // SAFETY: fixed initialized scalar slots, declared SIMD clobbers.
        unsafe {
            core::arch::asm!(
            "movsd xmm0,[{p}]","movapd xmm1,xmm0","mulsd xmm0,[{p}+8]",
            "movsd [{p}+24],xmm0","mulsd xmm1,[{p}+16]","movsd [{p}+32],xmm1",
            p=in(reg)self.data.as_mut_ptr(),out("xmm0")_,out("xmm1")_,options(nostack));
        }
        [self.data[3], self.data[4]]
    }
}
impl Backend for Numeric {
    fn products(&mut self, sigma: u64, sx: u64, sy: u64) -> [u64; 2] {
        self.products(sigma, sx, sy)
    }
    fn copy(&mut self, key: u64) -> u64 {
        self.data[0] = key;
        #[cfg(target_arch = "x86")]
        // SAFETY: fixed initialized qword; balanced x87 load/spill preserves
        // signaling-NaN quieting and denormal status at the original copy sites.
        unsafe {
            core::arch::asm!("fld qword ptr [{p}]","fstp qword ptr [{p}]",p=in(reg)self.data.as_mut_ptr(),options(nostack));
        }
        self.data[0]
    }
    fn equal(&mut self, lookup: u64, stored: u64) -> bool {
        self.data[..3].copy_from_slice(&[lookup, stored, 0]);
        #[cfg(target_arch = "x86")]
        // SAFETY: two initialized qwords, one result byte, balanced x87 stack.
        unsafe {
            core::arch::asm!("fld qword ptr [{p}]","fld qword ptr [{p}+8]","fucompp",
            "fnstsw ax","test ah,0x44","setnp byte ptr [{p}+16]",p=in(reg)self.data.as_mut_ptr(),out("eax")_,options(nostack));
        }
        #[cfg(target_arch = "x86_64")]
        // SAFETY: fixed local operands, explicit integer/SIMD clobbers.
        unsafe {
            core::arch::asm!("movsd xmm0,[{p}]","ucomisd xmm0,[{p}+8]","sete al","setnp dl","and al,dl","mov [{p}+16],al",
            p=in(reg)self.data.as_mut_ptr(),out("xmm0")_,out("eax")_,out("edx")_,options(nostack));
        }
        self.data[2] != 0
    }
    fn build(
        &mut self,
        key: u64,
    ) -> Result<xy_vsfilter_compat::gaussian_coefficients::Coefficients, u32> {
        xy_vsfilter_compat::gaussian_coefficients::Coefficients::build(
            &mut crate::windows_gaussian_numeric::Numeric::new(key),
        )
    }
    fn growth(&mut self, size: usize, buckets: usize) -> Result<usize, u32> {
        // Actual owned table lengths cannot exceed this signed conversion range.
        if size > isize::MAX as usize || buckets > isize::MAX as usize {
            return Err(5);
        }
        self.data = [
            size as u64,
            buckets as u64,
            0,
            0,
            1f32.to_bits() as u64,
            0,
            0,
            0,
        ];
        #[cfg(target_arch = "x86")]
        // SAFETY: fixed initialized slots and balanced x87/temporary stack;
        // scalar CRT ceil and _ftol2 only, with all C ABI clobbers declared.
        unsafe {
            core::arch::asm!(r#"
            fld dword ptr [edi+32]
            fild dword ptr [edi]
            fstp dword ptr [edi+24]
            fld dword ptr [edi+24]
            fild dword ptr [edi+8]
            fstp dword ptr [edi+24]
            fld dword ptr [edi+24]
            fdivr st(0),st(1)
            fcomp st(2)
            fnstsw ax
            test ah,0x41
            jne 2f
            fdivrp st(1),st(0)
            fstp dword ptr [edi+24]
            fld dword ptr [edi+24]
            fstp dword ptr [edi+24]
            fld dword ptr [edi+24]
            sub esp,8
            fstp qword ptr [esp]
            call {ceil}
            add esp,8
            fstp dword ptr [edi+24]
            fld dword ptr [edi+24]
            call {convert}
            mov [edi+16],eax
            jmp 3f
            2: fstp st(0)
            fstp st(0)
            3:
        "#,in("edi")self.data.as_mut_ptr(),ceil=sym ceil,convert=sym _ftol2,clobber_abi("C"));
        }
        #[cfg(target_arch = "x86_64")]
        // SAFETY: fixed initialized locals; copied SSE ratio, ceil and signed
        // conversion for representable owned-table counts, no image/cache address.
        unsafe {
            core::arch::asm!(r#"
            movss xmm3,[{p}+32]
            xorps xmm1,xmm1
            cvtsi2ss xmm1,qword ptr [{p}]
            xorps xmm2,xmm2
            cvtsi2ss xmm2,qword ptr [{p}+8]
            movaps xmm0,xmm1
            divss xmm0,xmm2
            comiss xmm0,xmm3
            jbe 3f
            divss xmm1,xmm3
            cvttss2si ecx,xmm1
            cmp ecx,0x80000000
            je 2f
            movd xmm0,ecx
            cvtdq2ps xmm0,xmm0
            ucomiss xmm0,xmm1
            je 2f
            unpcklps xmm1,xmm1
            movmskps eax,xmm1
            and eax,1
            xor eax,1
            add ecx,eax
            movd xmm1,ecx
            cvtdq2ps xmm1,xmm1
            2: cvttss2si rax,xmm1
            mov [{p}+16],rax
            3:
        "#,p=in(reg)self.data.as_mut_ptr(),out("eax")_,out("ecx")_,out("xmm0")_,out("xmm1")_,out("xmm2")_,out("xmm3")_,options(nostack));
        }
        Ok(self.data[2] as usize)
    }
}
#[cfg(target_arch = "x86")]
extern "C" {
    fn ceil(value: f64) -> f64;
    fn _ftol2() -> i64;
}
