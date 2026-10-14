// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::{boxed::Box, vec::Vec};
use xy_vsfilter_compat::span_merge::{self, Span};
/// # Safety
/// Nonempty a/b each span count initialized aligned pairs of u64 values and
/// remain immutable for this call. Zero counts accept null. Inputs may alias.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_span_merge(
    a: *const u64,
    na: usize,
    b: *const u64,
    nb: usize,
    offset1: u64,
    offset2: u64,
) -> *mut Vec<Span> {
    if na > isize::MAX as usize / core::mem::size_of::<Span>()
        || nb > isize::MAX as usize / core::mem::size_of::<Span>()
        || (na != 0 && a.is_null())
        || (nb != 0 && b.is_null())
    {
        return core::ptr::null_mut();
    }
    // SAFETY: immutable aligned native copies, length/shape checked above.
    let a = if na == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(a.cast::<Span>(), na) }
    };
    // SAFETY: same immutable input contract; shared input aliases are allowed.
    let b = if nb == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(b.cast::<Span>(), nb) }
    };
    span_merge::merge(a, b, offset1, offset2).map_or(core::ptr::null_mut(), |output| {
        Box::into_raw(Box::new(output))
    })
}
/// # Safety
/// owner is live and immutable during query. field 0=count, 1=start, 2=end.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_span_field(
    owner: *const Vec<Span>,
    field: u32,
    index: usize,
) -> u64 {
    // SAFETY: noncopyable native owner stays live; no callback spans this borrow.
    let state = unsafe { &*owner };
    if field == 0 {
        return state.len() as u64;
    }
    state
        .get(index)
        .and_then(|span| match field {
            1 => Some(span[0]),
            2 => Some(span[1]),
            _ => None,
        })
        .unwrap_or(0)
}
/// # Safety
/// Release the unique returned owner once, after all queries. Null is accepted.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_span_release(owner: *mut Vec<Span>) {
    if !owner.is_null() {
        // SAFETY: the native RAII object transfers unique ownership back.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn result_owns_its_records_and_accepts_shared_input_aliases() {
        let mut input = [1u64, 3, 4, 5];
        // SAFETY: initialized immutable shared aliases, sequential queries, one release.
        unsafe {
            let owner = xy_vsfilter_span_merge(input.as_ptr(), 2, input.as_ptr(), 2, 0, 0);
            assert!(!owner.is_null());
            input.fill(99);
            assert_eq!(xy_vsfilter_span_field(owner, 0, 0), 2);
            assert_eq!(xy_vsfilter_span_field(owner, 1, 0), 1);
            assert_eq!(xy_vsfilter_span_field(owner, 2, 1), 5);
            assert_eq!(xy_vsfilter_span_field(owner, 1, usize::MAX), 0);
            xy_vsfilter_span_release(owner);
            assert!(
                xy_vsfilter_span_merge(input.as_ptr(), 2, core::ptr::null(), 0, 0, 0).is_null()
            );
        }
    }
}
