// SPDX-License-Identifier: GPL-2.0-or-later
//! A single borrowed-host-buffer boundary. Planning, alias tracking, byte
//! ownership and all arithmetic live in the safe portable core.
use xy_vsfilter_compat::surface::{Image, Parameters, Plan};

/// # Safety
/// parameters lends 48 initialized scalar bytes. The caller lends every range
/// returned by the checked plan: initial-read fragments are initialized and
/// readable; destination spans are writable. Pixel spans may alias each other,
/// but not the parameter packet or private storage allocated by this call. No other thread or
/// callback accesses these spans until return. Each range stays within its source/destination allocation;
/// original pointer provenance is retained; no Rust reference to caller storage is constructed or retained.
/// Metadata aliases, invalid backing addresses and allocation failure are not
/// admitted reference outcomes. Unused pointers on empty plans need not be valid.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_surface_v1(
    parameters: *const u8,
    destination: *mut u8,
    source0: *const u8,
    source1: *const u8,
    source2: *const u8,
    source3: *const u8,
) -> u32 {
    if parameters.is_null() {
        return 3;
    }
    let mut bytes = [0u8; 48];
    // SAFETY: fixed initialized metadata packet, detached before any host write.
    unsafe {
        core::ptr::copy_nonoverlapping(parameters, bytes.as_mut_ptr(), 48);
    }
    let f: [i32; 12] =
        core::array::from_fn(|i| i32::from_le_bytes(bytes[i * 4..i * 4 + 4].try_into().unwrap()));
    if !(0..=1).contains(&f[11]) {
        return 6;
    }
    let p = Parameters {
        mode: f[0],
        destination: [f[1], f[2], f[3], f[4]],
        position: [f[5], f[6]],
        size: [f[7], f[8]],
        source_pitch: f[9],
        color: f[10] as u32,
        planar: f[11] != 0,
    };
    let run = || -> Result<(), u32> {
        let pointers = [destination.cast_const(), source0, source1, source2, source3];
        let plan = Plan::new(p, pointers.map(|ptr| ptr.addr()))?;
        let mut image = Image::new(plan.spans())?;
        for span in plan.initial_reads()? {
            let mut input = alloc::vec::Vec::<u8>::new();
            input.try_reserve_exact(span.len).map_err(|_| 5u32)?;
            input.resize(span.len, 0);
            // SAFETY: the checked plan's initial-read fragment is an initialized
            // caller range. Each fragment retains the corresponding input-pointer provenance.
            // Destination is independent private storage; no source reference exists.
            unsafe {
                core::ptr::copy_nonoverlapping(
                    pointers[span.anchor].with_addr(span.address),
                    input.as_mut_ptr(),
                    span.len,
                );
            }
            image.import(span.address, &input)?;
        }
        plan.execute(&mut image)?;
        for block in image.blocks() {
            let mut at = 0;
            while at < block.values.len() {
                if block.written[at] == 0 {
                    at += 1;
                    continue;
                }
                let mut end = at + 1;
                while end < block.values.len() && block.written[end] != 0 {
                    end += 1;
                }
                // SAFETY: only actual written spans are exported to caller-writable
                // addresses. Values are detached and initialized; padding is untouched.
                unsafe {
                    core::ptr::copy_nonoverlapping(
                        block.values.as_ptr().add(at),
                        destination.with_addr(block.address + at),
                        end - at,
                    );
                }
                at = end;
            }
        }
        Ok(())
    };
    run().err().unwrap_or(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn copy_initializes_later_source_rows_and_preserves_provenance() {
        let mut parameters = [0u8; 49];
        for (i, v) in [2i32, 8, 2, 32, 8, 0, 0, 2, 2, 8, 0, 0]
            .into_iter()
            .enumerate()
        {
            parameters[1 + i * 4..1 + i * 4 + 4].copy_from_slice(&v.to_le_bytes());
        }
        let mut output = core::mem::MaybeUninit::<[u8; 24]>::uninit();
        let ptr = output.as_mut_ptr().cast::<u8>();
        // SAFETY: only the first eight bytes are initially read. Each row writes
        // the next eight before the following row uses it as input. All 24 bytes
        // are initialized on success; the single allocation supplies provenance.
        unsafe {
            core::ptr::copy_nonoverlapping([1u8, 2, 3, 4, 5, 6, 7, 8].as_ptr(), ptr, 8);
            assert_eq!(
                xy_vsfilter_surface_v1(
                    parameters.as_ptr().add(1),
                    ptr.add(8),
                    ptr,
                    core::ptr::null(),
                    core::ptr::null(),
                    core::ptr::null()
                ),
                0
            );
            assert_eq!(
                output.assume_init(),
                [1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6, 7, 8]
            );
        }
    }
    #[test]
    fn null_metadata_and_noop_geometry_do_not_touch_host_memory() {
        let mut fields = [0u8; 48];
        fields[..4].copy_from_slice(&3i32.to_le_bytes());
        // SAFETY: null metadata is rejected; zero width consumes no host pointer.
        unsafe {
            assert_eq!(
                xy_vsfilter_surface_v1(
                    core::ptr::null(),
                    core::ptr::null_mut(),
                    core::ptr::null(),
                    core::ptr::null(),
                    core::ptr::null(),
                    core::ptr::null()
                ),
                3
            );
            assert_eq!(
                xy_vsfilter_surface_v1(
                    fields.as_ptr(),
                    core::ptr::null_mut(),
                    core::ptr::null(),
                    core::ptr::null(),
                    core::ptr::null(),
                    core::ptr::null()
                ),
                0
            );
        }
    }
}
