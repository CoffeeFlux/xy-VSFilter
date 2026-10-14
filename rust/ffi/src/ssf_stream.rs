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

/// Copied scalars only. All output callbacks occur after this function returns.
#[no_mangle]
pub extern "C" fn xy_vsfilter_ssf_output(character: u16, encoding: u32) -> u64 {
    xy_vsfilter_compat::ssf_stream::output(character, encoding)
}

/// # Safety
/// For positive length, input is initialized and readable for length bytes.
/// The copied data never retains references to the caller's input.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssf_memory_create(
    input: *const u8,
    length: i32,
) -> *mut xy_vsfilter_compat::ssf_stream::Memory {
    if length <= 0 {
        return core::ptr::null_mut();
    }
    // SAFETY: the native copy-mode constructor supplies its input span.
    let bytes = unsafe { core::slice::from_raw_parts(input, length as usize) };
    xy_vsfilter_compat::ssf_stream::Memory::copy(bytes).map_or(core::ptr::null_mut(), |memory| {
        Box::into_raw(Box::new(memory))
    })
}
/// # Safety
/// owner is a live immutable copy owner. The returned bytes remain read-only
/// until release. Neither native code nor another Rust call may mutate them.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssf_memory_data(
    owner: *const xy_vsfilter_compat::ssf_stream::Memory,
) -> *const u8 {
    // SAFETY: the native owner remains alive throughout all immutable loans.
    unsafe { &*owner }.data().as_ptr()
}
/// # Safety
/// owner is live and immutable; position is a copied scalar.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssf_memory_read(
    owner: *const xy_vsfilter_compat::ssf_stream::Memory,
    position: i32,
) -> i32 {
    // SAFETY: shared access only; the byte lookup is bounds checked in Rust.
    unsafe { &*owner }.read(position)
}
/// # Safety
/// owner is null or the unique create result; all byte uses have ended.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ssf_memory_release(
    owner: *mut xy_vsfilter_compat::ssf_stream::Memory,
) {
    if !owner.is_null() {
        // SAFETY: the native owner ends every immutable loan before release.
        drop(unsafe { Box::from_raw(owner) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn copied_memory_is_independent_and_shared_queries_preserve_the_read_loan() {
        let mut input = [0, 255, 128, 65];
        // SAFETY: input has four initialized bytes. The owner copies them;
        // source mutations and shared queries cannot invalidate its read loan.
        unsafe {
            let owner = xy_vsfilter_ssf_memory_create(input.as_ptr(), 4);
            assert!(!owner.is_null());
            let data = xy_vsfilter_ssf_memory_data(owner);
            input.fill(9);
            assert_eq!(input, [9; 4]);
            for (i, expected) in [0, 255, 128, 65].into_iter().enumerate() {
                assert_eq!(xy_vsfilter_ssf_memory_read(owner, i as i32), expected);
                assert_eq!(*data.add(i), expected as u8);
            }
            assert_eq!(xy_vsfilter_ssf_memory_read(owner, -1), -1);
            assert_eq!(xy_vsfilter_ssf_memory_read(owner, 4), -1);
            xy_vsfilter_ssf_memory_release(owner);
            assert!(xy_vsfilter_ssf_memory_create(core::ptr::null(), 0).is_null());
            xy_vsfilter_ssf_memory_release(core::ptr::null_mut());
        }
    }

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
