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
/// # Safety
/// state points to an initialized, exclusive repr(C) Peek record. Queue facts
/// and the previous callback result are copied scalars; no borrow escapes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssf_peek_step(
    state: *mut xy_vsfilter_compat::ssf_stream::Peek,
    count: usize,
    head: i32,
    tail: i32,
    last: i32,
) -> u32 {
    // SAFETY: native stack state is initialized once and borrowed only here.
    unsafe { &mut *state }.step(count, head, tail, last) as u32
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn peek_stack_state_ends_each_borrow_before_character_callbacks() {
        let owner = xy_vsfilter_ssf_queue_create();
        let input = [47, 42, 65, 42, 47, 47, 47, 66, 10, 0xd800];
        let mut cursor = 0;
        let mut last = 0;
        let mut state = xy_vsfilter_compat::ssf_stream::Peek::default();
        let mut done = false;
        // SAFETY: stack state and queue owner are disjoint. Each scalar step
        // and queue mutation ends before the next simulated native callback.
        unsafe {
            for _ in 0..100 {
                let count = xy_vsfilter_ssf_queue_count(owner);
                let head = if count != 0 {
                    xy_vsfilter_ssf_queue_end(owner, 0)
                } else {
                    0
                };
                let tail = if count != 0 {
                    xy_vsfilter_ssf_queue_end(owner, 1)
                } else {
                    0
                };
                match xy_vsfilter_ssf_peek_step(&mut state, count, head, tail, last) {
                    0 => {
                        last = input.get(cursor).copied().unwrap_or(-1);
                        cursor += 1;
                        xy_vsfilter_ssf_queue_push(owner, last);
                    }
                    1 => {
                        last = xy_vsfilter_ssf_queue_pop(owner);
                    }
                    2 => {
                        done = true;
                        break;
                    }
                    3 => assert_eq!(count, 2),
                    _ => panic!("unexpected peek action"),
                }
            }
            assert!(done);
            assert_eq!(state.value(), 0xd800);
            assert_eq!(xy_vsfilter_ssf_queue_end(owner, 0), 0xd800);
            assert_eq!(xy_vsfilter_ssf_queue_end(owner, 1), -1);
            xy_vsfilter_ssf_queue_release(owner);
        }
    }
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
