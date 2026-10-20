// SPDX-License-Identifier: GPL-2.0-or-later
//! Linked cleanup CClipper signed-int scale conversion; fixed scalar locals.
pub(crate) struct Numeric;
#[cfg(target_arch = "x86")]
extern "C" {
    fn _ftol2_sse() -> i32;
}
impl xy_vsfilter_compat::clipper::Numeric for Numeric {
    fn scale(&mut self, value: i32, scale: u64) -> i32 {
        let mut data = [u64::from(value as u32), scale, 0, 0];
        #[cfg(target_arch = "x86")]
        // SAFETY: EDI points to initialized fixed locals across the C helper;
        // all volatile registers are declared and the helper pops the x87 value.
        unsafe {
            core::arch::asm!(r#"
            fild dword ptr [edi]
            fstp qword ptr [edi+16]
            fld qword ptr [edi+16]
            fmul qword ptr [edi+8]
            call {convert}
            mov dword ptr [edi+24],eax
        "#,in("edi") data.as_mut_ptr(),convert=sym _ftol2_sse,clobber_abi("C"));
        }
        #[cfg(target_arch = "x86_64")]
        // SAFETY: only fixed initialized scalars, with every clobber declared.
        unsafe {
            core::arch::asm!(r#"
            movd xmm0,dword ptr [{p}]
            cvtdq2pd xmm0,xmm0
            mulsd xmm0,qword ptr [{p}+8]
            cvttsd2si eax,xmm0
            mov dword ptr [{p}+24],eax
        "#,p=in(reg)data.as_mut_ptr(),out("eax")_,out("xmm0")_,options(nostack));
        }
        data[3] as u32 as i32
    }
}
