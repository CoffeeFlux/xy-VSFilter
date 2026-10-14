// SPDX-License-Identifier: GPL-2.0-or-later
#[repr(C)]
pub struct BlurStep {
    pub pixels: u64,
    pub state: u32,
    pub reserved: u32,
}

/// # Safety
/// For counts 1 through 8, column_pixels and column_sums each lend count
/// initialized, aligned, exclusive u16 values in disjoint native allocations.
/// Source bytes were snapshotted before this call; no native row is borrowed.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_be_blur_group(
    source: u64,
    count: u32,
    state: u32,
    column_pixels: *mut u16,
    column_sums: *mut u16,
    phase: u32,
    vector: u32,
) -> BlurStep {
    if count == 0 || count > 8 {
        return BlurStep {
            pixels: 0,
            state,
            reserved: 0,
        };
    }
    // SAFETY: each separately allocated column buffer lends this bounded group;
    // both borrows end before native output writes and the next source read.
    let pixels = unsafe { core::slice::from_raw_parts_mut(column_pixels, count as usize) };
    let sums = unsafe { core::slice::from_raw_parts_mut(column_sums, count as usize) };
    let step = xy_vsfilter_compat::be_blur::group(source, state, pixels, sums, phase, vector != 0);
    BlurStep {
        pixels: step.pixels,
        state: step.state,
        reserved: 0,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn group_borrows_end_before_history_is_reused() {
        let mut pixels = [0; 8];
        let mut sums = [0; 8];
        let seed = 16 | (32 << 16);
        // SAFETY: the two live arrays are disjoint and each call's borrows end
        // before the following phase reuses the same history.
        unsafe {
            for phase in 0..3 {
                let step = xy_vsfilter_be_blur_group(
                    0x1010101010101010,
                    8,
                    seed,
                    pixels.as_mut_ptr(),
                    sums.as_mut_ptr(),
                    phase,
                    1,
                );
                if phase == 2 {
                    assert_eq!(step.pixels, 0x1010101010101010);
                }
                assert_eq!(step.reserved, 0);
            }
        }
    }
}
