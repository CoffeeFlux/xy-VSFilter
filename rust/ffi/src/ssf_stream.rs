// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::ssf_stream::Lookahead;
#[no_mangle]
pub extern "C" fn xy_vsfilter_ssf_queue_create() -> *mut Lookahead {
    Box::into_raw(Box::new(Lookahead::default()))
}
/// # Safety
/// owner is a unique live queue. Raw scalar values retain no native references.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssf_queue_push(owner: *mut Lookahead, value: i32) {
    // SAFETY: native RAII lends its unique queue only for this operation.
    unsafe { &mut *owner }.push(value);
}
/// # Safety
/// owner is a unique live queue. Native callers check nonempty before removing.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssf_queue_pop(owner: *mut Lookahead) -> i32 {
    // SAFETY: the exclusive borrow ends before native state updates resume.
    unsafe { &mut *owner }.pop().unwrap_or(0)
}
/// # Safety
/// owner is live and immutable for the query; no reference escapes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssf_queue_count(owner: *const Lookahead) -> usize {
    // SAFETY: immutable scalar query of the live owner.
    unsafe { &*owner }.len()
}
/// # Safety
/// owner is live and immutable. Native callers request ends only when nonempty.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssf_queue_end(owner: *const Lookahead, last: u32) -> i32 {
    // SAFETY: immutable scalar query of the live owner.
    let queue = unsafe { &*owner };
    if last == 0 {
        queue.first()
    } else {
        queue.last()
    }
    .unwrap_or(0)
}
/// # Safety
/// owner is null or the unique create result, released once after all operations.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssf_queue_release(owner: *mut Lookahead) {
    if !owner.is_null() {
        // SAFETY: native RAII ends all operations before returning ownership.
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn queries_and_mutations_keep_one_owner_without_exporting_references() {
        let owner = xy_vsfilter_ssf_queue_create();
        assert!(!owner.is_null());
        // SAFETY: one live unique queue; all accesses are sequential scalar
        // operations, followed by one release after the last query.
        unsafe {
            for value in 0..1024 {
                xy_vsfilter_ssf_queue_push(owner, value);
            }
            xy_vsfilter_ssf_queue_push(owner, -1);
            xy_vsfilter_ssf_queue_push(owner, -1);
            assert_eq!(xy_vsfilter_ssf_queue_count(owner), 1026);
            assert_eq!(xy_vsfilter_ssf_queue_end(owner, 0), 0);
            assert_eq!(xy_vsfilter_ssf_queue_end(owner, 1), -1);
            for value in 0..1024 {
                assert_eq!(xy_vsfilter_ssf_queue_pop(owner), value);
            }
            assert_eq!(xy_vsfilter_ssf_queue_pop(owner), -1);
            assert_eq!(xy_vsfilter_ssf_queue_pop(owner), -1);
            assert_eq!(xy_vsfilter_ssf_queue_count(owner), 0);
            xy_vsfilter_ssf_queue_release(owner);
        }
    }
}
