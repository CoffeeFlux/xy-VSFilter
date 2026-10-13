// SPDX-License-Identifier: GPL-2.0-or-later
//! Private ownership boundary for the ASS lexer. See include/xy_rust_ass.h.
use alloc::{boxed::Box, vec::Vec};
use xy_vsfilter_compat::ass_tags::{tokenize, Tag};

pub struct Tags(Vec<Tag>);

/// # Safety
/// For nonzero length, both pointers must describe readable, properly aligned,
/// nonoverlapping arrays of `length` elements, immutable for this call. Length
/// must fit the platform slice size limit. The bridge owns those arrays. Nothing
/// borrows their storage after this function returns.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_scan(
    units: *const u16,
    whitespace: *const u8,
    length: u32,
) -> *mut Tags {
    if length == 0 {
        return Box::into_raw(Box::new(Tags(Vec::new())));
    }
    if units.is_null() || whitespace.is_null() || length as u64 > isize::MAX as u64 / 2 {
        return core::ptr::null_mut();
    }
    // SAFETY: the caller provides the live immutable arrays described above.
    let units = unsafe { core::slice::from_raw_parts(units, length as usize) };
    let whitespace = unsafe { core::slice::from_raw_parts(whitespace, length as usize) };
    Box::into_raw(Box::new(Tags(tokenize(units, whitespace))))
}

/// Query fields by value: 0=tag count, 1=command span, 2=parameter count,
/// 3=parameter span. Spans pack start in low 32 bits and length in high 32.
/// # Safety
/// `tags` is null or an unreleased pointer from ass_scan; it must not be released
/// concurrently with a query. Invalid indices return zero without dereferencing
/// an element. No pointer into the allocation is returned.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_field(
    tags: *const Tags,
    tag: u32,
    field: u32,
    parameter: u32,
) -> u64 {
    if tags.is_null() {
        return 0;
    }
    // SAFETY: caller retains the unique owner for the duration of this query.
    let tags = &unsafe { &*tags }.0;
    if field == 0 {
        return tags.len() as u64;
    }
    let Some(tag) = tags.get(tag as usize) else {
        return 0;
    };
    let span = match field {
        1 => &tag.command,
        2 => return tag.parameters.len() as u64,
        3 => match tag.parameters.get(parameter as usize) {
            Some(span) => span,
            None => return 0,
        },
        _ => return 0,
    };
    span.start as u64 | ((span.len() as u64) << 32)
}

/// # Safety
/// Pass null or the unique pointer returned by ass_scan exactly once, after all
/// queries finish. The C++ RAII owner disables copying and never shares it.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_release(tags: *mut Tags) {
    if !tags.is_null() {
        // SAFETY: ownership is transferred back exactly once by the caller.
        drop(unsafe { Box::from_raw(tags) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn owner_outlives_input_and_queries_are_bounded() {
        let owner = {
            let input: Vec<u16> = "\\pos(1,2)".encode_utf16().collect();
            let white = vec![0; input.len()];
            // SAFETY: live correctly sized arrays, borrowed only during scan.
            unsafe { xy_vsfilter_ass_scan(input.as_ptr(), white.as_ptr(), input.len() as u32) }
        };
        assert!(!owner.is_null());
        // SAFETY: owner is live for all queries, then released exactly once.
        unsafe {
            assert_eq!(xy_vsfilter_ass_field(owner, 0, 0, 0), 1);
            assert_eq!(xy_vsfilter_ass_field(owner, 0, 1, 0), 1 | (3 << 32));
            assert_eq!(xy_vsfilter_ass_field(owner, 0, 2, 0), 2);
            assert_eq!(xy_vsfilter_ass_field(owner, 0, 3, 1), 7 | (1 << 32));
            assert_eq!(xy_vsfilter_ass_field(owner, u32::MAX, 1, 0), 0);
            assert_eq!(xy_vsfilter_ass_field(owner, 0, 3, u32::MAX), 0);
            assert_eq!(xy_vsfilter_ass_field(owner, 0, u32::MAX, 0), 0);
            xy_vsfilter_ass_release(owner);
            xy_vsfilter_ass_release(std::ptr::null_mut());
        }
    }
}
