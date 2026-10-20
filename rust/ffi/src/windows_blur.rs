// SPDX-License-Identifier: GPL-2.0-or-later
//! Linked cleanup Blur predicates and argument conversion. Fixed scalar locals
//! preserve comparisons, x86 extended registers and Windows floating status.
use xy_vsfilter_compat::blur::Numeric as Backend;
#[repr(align(16))]
pub(crate) struct Numeric {
    data: [u64; 16],
}
impl Numeric {
    pub(crate) fn new(be: u32, gaussian: u64, sx: u64, sy: u64) -> Self {
        let mut data = [0; 16];
        data[..5].copy_from_slice(&[u64::from(be), gaussian, sx, sy, 0x3fd55553e0000000]);
        Self { data }
    }
    pub(crate) fn active(&mut self) -> bool {
        self.data[5] = 1;
        #[cfg(target_arch = "x86")]
        // SAFETY: fixed initialized scalars, balanced x87 comparison stack.
        unsafe {
            core::arch::asm!(r#"
            fldz
            fcomp dword ptr [{p}]
            fnstsw ax
            test ah,1
            jne 2f
            fld qword ptr [{p}+32]
            fcomp qword ptr [{p}+8]
            fnstsw ax
            test ah,1
            jne 2f
            mov dword ptr [{p}+40],0
            2:
        "#,p=in(reg)self.data.as_mut_ptr(),out("eax")_,options(nostack));
        }
        #[cfg(target_arch = "x86_64")]
        // SAFETY: fixed initialized operands and all SIMD/integer clobbers.
        unsafe {
            core::arch::asm!(r#"
            xorps xmm2,xmm2
            comiss xmm2,[{p}]
            jb 2f
            movsd xmm0,[{p}+32]
            comisd xmm0,[{p}+8]
            setb byte ptr [{p}+40]
            2:
        "#,p=in(reg)self.data.as_mut_ptr(),out("xmm0")_,out("xmm2")_,options(nostack));
        }
        self.data[5] != 0
    }
}
impl Backend for Numeric {
    type Be = crate::windows_be_blur_numeric::Numeric;
    fn begin(&mut self) -> bool {
        #[cfg(target_arch = "x86_64")]
        {
            self.active()
        }
        #[cfg(target_arch = "x86")]
        {
            self.data[5] = 1;
            // SAFETY: fixed initialized slots; retained numeric values spill
            // to 80-bit locals and the hardware stack is empty on every return.
            unsafe {
                core::arch::asm!(r#"
                fldz
                fld dword ptr [{p}]
                fcom st(1)
                fnstsw ax
                fld qword ptr [{p}+8]
                test ah,0x41
                jp 2f
                fld qword ptr [{p}+32]
                fcomp st(1)
                fnstsw ax
                test ah,1
                jne 2f
                mov dword ptr [{p}+40],0
                2: fstp tbyte ptr [{p}+64]
                fstp tbyte ptr [{p}+80]
                fstp st(0)
            "#,p=in(reg)self.data.as_mut_ptr(),out("eax")_,options(nostack));
            }
            self.data[5] != 0
        }
    }
    fn effects(&mut self) -> [bool; 2] {
        self.data[5] = 0;
        #[cfg(target_arch = "x86")]
        // SAFETY: initialized retained 80-bit scalars, balanced x87 stack.
        unsafe {
            core::arch::asm!(r#"
            fldz
            fld tbyte ptr [{p}+80]
            fld tbyte ptr [{p}+64]
            fldz
            fcompp
            fnstsw ax
            fucompp
            test ah,5
            fnstsw ax
            jp 2f
            mov dword ptr [{p}+40],1
            2: test ah,0x44
            jnp 3f
            or dword ptr [{p}+40],2
            3:
        "#,p=in(reg)self.data.as_mut_ptr(),out("eax")_,options(nostack));
        }
        #[cfg(target_arch = "x86_64")]
        // SAFETY: fixed scalar slots and declared SIMD clobbers only.
        unsafe {
            core::arch::asm!(r#"
            xorps xmm0,xmm0
            movsd xmm1,[{p}+8]
            comisd xmm1,xmm0
            jbe 2f
            mov dword ptr [{p}+40],1
            2: movss xmm1,[{p}]
            ucomiss xmm1,xmm0
            jp 3f
            je 4f
            3: or dword ptr [{p}+40],2
            4:
        "#,p=in(reg)self.data.as_mut_ptr(),out("xmm0")_,out("xmm1")_,options(nostack));
        }
        [self.data[5] & 1 != 0, self.data[5] & 2 != 0]
    }
    fn gaussian_args(&mut self) -> [u64; 3] {
        #[cfg(target_arch = "x86")]
        // SAFETY: fixed initialized slots. Native argument copies load/spill
        // y, x, then Gaussian strength; preserve quieting and denormal status.
        unsafe {
            core::arch::asm!(
            "fld qword ptr [{p}+24]","fstp qword ptr [{p}+112]",
            "fld qword ptr [{p}+16]","fstp qword ptr [{p}+104]",
            "fld qword ptr [{p}+8]","fstp qword ptr [{p}+96]",
            p=in(reg)self.data.as_mut_ptr(),options(nostack));
        }
        #[cfg(target_arch = "x86_64")]
        self.data.copy_within(1..4, 12);
        [self.data[12], self.data[13], self.data[14]]
    }
    fn be(&mut self) -> Self::Be {
        #[cfg(target_arch = "x86")]
        // SAFETY: fixed initialized scalar slots, balanced x87 load/spill stack.
        unsafe {
            core::arch::asm!(r#"
            fld qword ptr [{p}+24]
            fstp dword ptr [{p}+112]
            fld qword ptr [{p}+16]
            fstp dword ptr [{p}+104]
            fld dword ptr [{p}+112]
            fstp dword ptr [{p}+112]
            fld dword ptr [{p}+104]
            fstp dword ptr [{p}+104]
            fld dword ptr [{p}]
            fstp dword ptr [{p}+96]
        "#,p=in(reg)self.data.as_mut_ptr(),options(nostack));
        }
        #[cfg(target_arch = "x86_64")]
        {
            self.data[12] = self.data[0];
            // SAFETY: fixed local slots; native converts y before x. movsd
            // clears the second packed lane before cvtpd2ps, as in cleanup.
            unsafe {
                core::arch::asm!(
                "movsd xmm3,[{p}+24]","cvtpd2ps xmm3,xmm3","movss [{p}+112],xmm3",
                "xorps xmm2,xmm2","cvtsd2ss xmm2,qword ptr [{p}+16]","movss [{p}+104],xmm2",
                p=in(reg)self.data.as_mut_ptr(),out("xmm2")_,out("xmm3")_,options(nostack));
            }
        }
        Self::Be::new(
            self.data[12] as u32,
            self.data[13] as u32,
            self.data[14] as u32,
        )
    }
}
