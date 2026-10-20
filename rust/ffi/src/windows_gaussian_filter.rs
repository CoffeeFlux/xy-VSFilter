// SPDX-License-Identifier: GPL-2.0-or-later
//! Fixed-size arithmetic only. Image indexing, filters and scratch live in the
//! safe core. Inline assembly retains MXCSR/x87 rounding and sticky status.
use xy_vsfilter_compat::gaussian_filter::Numeric as Backend;

#[repr(align(16))]
pub(crate) struct Numeric {
    data: [u32; 16],
}
impl Numeric {
    pub(crate) fn new() -> Self {
        Self { data: [0; 16] }
    }
}

impl Backend for Numeric {
    fn mul(&mut self, left: [u32; 4], right: [u32; 4]) -> [u32; 4] {
        self.data[..4].copy_from_slice(&left);
        self.data[4..8].copy_from_slice(&right);
        // SAFETY: three fixed 16-byte accesses within the initialized local;
        // no native image address or dimension enters this boundary.
        unsafe {
            core::arch::asm!(
            "movups xmm0, [{p}]", "mulps xmm0, [{p} + 16]", "movups [{p}], xmm0",
            p=in(reg) self.data.as_mut_ptr(), out("xmm0") _, options(nostack));
        }
        self.data[..4].try_into().unwrap()
    }
    fn add(&mut self, left: [u32; 4], right: [u32; 4]) -> [u32; 4] {
        self.data[..4].copy_from_slice(&left);
        self.data[4..8].copy_from_slice(&right);
        // SAFETY: fixed initialized local slots only, no stack or calls.
        unsafe {
            core::arch::asm!(
            "movups xmm0, [{p}]", "addps xmm0, [{p} + 16]", "movups [{p}], xmm0",
            p=in(reg) self.data.as_mut_ptr(), out("xmm0") _, options(nostack));
        }
        self.data[..4].try_into().unwrap()
    }
    fn probe(&mut self, value: u32) -> u32 {
        self.data[0] = value;
        #[cfg(target_arch = "x86")]
        // SAFETY: fixed four-byte local slot; x87 stack is balanced.
        unsafe {
            core::arch::asm!("fld dword ptr [{p}]", "fstp dword ptr [{p}]",
            p=in(reg) self.data.as_mut_ptr(), options(nostack));
        }
        self.data[0]
    }
    fn equal(&mut self, left: u32, right: u32) -> bool {
        self.data[..2].copy_from_slice(&[left, right]);
        #[cfg(target_arch = "x86")]
        // SAFETY: initialized scalar slots, balanced x87 stack and all clobbers.
        unsafe {
            core::arch::asm!(
            "fld dword ptr [{p}]", "fld dword ptr [{p} + 4]", "fucompp",
            "fnstsw ax", "test ah, 0x44", "setnp byte ptr [{p} + 8]",
            p=in(reg) self.data.as_mut_ptr(), out("eax") _, options(nostack));
        }
        #[cfg(target_arch = "x86_64")]
        // SAFETY: fixed initialized scalar slots and declared scalar clobbers.
        unsafe {
            core::arch::asm!(
            "movss xmm0, [{p}]", "ucomiss xmm0, [{p} + 4]", "sete al", "setnp dl",
            "and al, dl", "mov byte ptr [{p} + 8], al",
            p=in(reg) self.data.as_mut_ptr(), out("xmm0") _, out("eax") _, out("edx") _, options(nostack));
        }
        self.data[2] & 255 != 0
    }
    fn bytes(&mut self, values: [u32; 16]) -> [u8; 16] {
        self.data = values;
        // SAFETY: four initialized 16-byte local vectors. Conversion and packs
        // match cleanup's SIMD blocks, including signed/unsigned saturation.
        unsafe {
            core::arch::asm!(
            "cvtps2dq xmm0, [{p}]", "cvtps2dq xmm1, [{p} + 16]",
            "cvtps2dq xmm2, [{p} + 32]", "cvtps2dq xmm3, [{p} + 48]",
            "packssdw xmm0, xmm1", "packssdw xmm2, xmm3", "packuswb xmm0, xmm2",
            "movups [{p}], xmm0", p=in(reg) self.data.as_mut_ptr(),
            out("xmm0") _, out("xmm1") _, out("xmm2") _, out("xmm3") _, options(nostack));
        }
        core::array::from_fn(|i| (self.data[i / 4] >> (8 * (i % 4))) as u8)
    }
    fn byte(&mut self, value: u32) -> u8 {
        self.data[0] = value;
        self.data[2] = 0;
        self.data[3] = 0x3fe00000; // binary64 0.5
        #[cfg(target_arch = "x86")]
        // SAFETY: fixed initialized slots, balanced x87 stack. EDI is preserved
        // by the scalar CRT conversion; all C ABI clobbers are declared.
        unsafe {
            core::arch::asm!(
            "fld qword ptr [edi + 8]", "fadd dword ptr [edi]", "call {convert}", "mov [edi + 4], eax",
            in("edi") self.data.as_mut_ptr(), convert=sym _ftol2, clobber_abi("C"));
        }
        #[cfg(target_arch = "x86_64")]
        // SAFETY: fixed local scalar slots; cleanup converts to a signed DWORD
        // then keeps AL, unlike the SIMD path's saturated byte conversion.
        unsafe {
            core::arch::asm!(
            "movss xmm0, [{p}]", "cvtss2sd xmm0, xmm0", "addsd xmm0, [{p} + 8]",
            "cvttsd2si eax, xmm0", "mov [{p} + 4], eax",
            p=in(reg) self.data.as_mut_ptr(), out("xmm0") _, out("eax") _, options(nostack));
        }
        self.data[1] as u8
    }
}
#[cfg(target_arch = "x86")]
extern "C" {
    fn _ftol2() -> i64;
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn simd_saturation_and_scalar_rounding_are_distinct() {
        let mut n = Numeric::new();
        let values = [0.5f32, 1.5, 2.5, 255.5, 256.5, -0.6];
        let mut input = [0; 16];
        for (i, v) in values.iter().enumerate() {
            input[i] = v.to_bits();
        }
        assert_eq!(&n.bytes(input)[..6], &[0, 2, 2, 255, 255, 0]);
        assert_eq!(values.map(|v| n.byte(v.to_bits())), [1, 2, 3, 0, 1, 0]);
    }
}
