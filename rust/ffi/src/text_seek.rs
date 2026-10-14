// SPDX-License-Identifier: GPL-2.0-or-later
/// # Safety
/// output is aligned, exclusively writable for one i64 value throughout call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_seek_target(
    position: u64,
    length: u64,
    offset: i64,
    origin: u32,
    bom: i32,
    output: *mut i64,
) -> u32 {
    if let Some(target) =
        xy_vsfilter_compat::text_seek::target(position, length, offset, origin, bom)
    {
        // SAFETY: native temporary is disjoint and exclusively writable.
        unsafe { output.write(target) };
        1
    } else {
        0
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn declined_physical_offset_leaves_native_output_untouched() {
        let mut result = 37i64;
        // SAFETY: initialized aligned stack output, exclusive during each call.
        unsafe {
            assert_eq!(
                xy_vsfilter_seek_target(0, u64::MAX, i64::MAX, 0, 3, &mut result),
                0
            );
            assert_eq!(result, 37);
            assert_eq!(xy_vsfilter_seek_target(0, 10, -1, 2, 3, &mut result), 1);
            assert_eq!(result, 13);
        }
    }
}
