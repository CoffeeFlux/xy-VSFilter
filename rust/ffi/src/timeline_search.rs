// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::timeline_search::{Mode, Search};

/// Modes: 0=entry, 1=next segment, 2=containing segment. Invalid mode returns null.
#[no_mangle]
pub extern "C" fn xy_vsfilter_search_create(mode: u32, time: i64, count: i32) -> *mut Search {
    let mode = match mode {
        0 => Mode::Entry,
        1 => Mode::NextSegment,
        2 => Mode::ContainingSegment,
        _ => return core::ptr::null_mut(),
    };
    Box::into_raw(Box::new(Search::new(mode, time, count)))
}

/// # Safety
/// owner is the unique live pointer from create; no concurrent access. Supply
/// the requested native translation, after all native calls have returned.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_search_supply(owner: *mut Search, value: i64) {
    // SAFETY: the noncopyable native wrapper supplies exclusive access.
    unsafe { &mut *owner }.supply(value);
}

/// Fields: 0=pending, 1=requested index, 2=request start rather than end,
/// 3=result index, 4=found. Invalid fields return zero. Query results only after
/// pending is zero. Request fields are zero when no request remains.
/// # Safety
/// owner remains live and immutable for this call. No borrowed storage escapes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_search_field(owner: *const Search, field: u32) -> i32 {
    // SAFETY: the native wrapper retains the owner without mutation.
    let search = unsafe { &*owner };
    match field {
        0 => i32::from(search.request().is_some()),
        1 => search.request().map_or(0, |request| request.1),
        2 => search.request().map_or(0, |request| i32::from(request.0)),
        3 => search.result().0,
        4 => i32::from(search.result().1),
        _ => 0,
    }
}

/// # Safety
/// Release the unique live pointer from create exactly once, after all calls.
/// Null is accepted; native code must not free or retain Rust storage.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_search_release(owner: *mut Search) {
    if !owner.is_null() {
        // SAFETY: exclusive ownership returns to Rust without outstanding borrows.
        drop(unsafe { Box::from_raw(owner) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn scalar_protocol_and_owner_lifetime() {
        let owner = xy_vsfilter_search_create(2, 3, 2);
        // SAFETY: one live owner, sequential calls, released exactly once.
        unsafe {
            for (index, start, value) in [(1, 0, 6), (0, 0, 2), (1, 1, 4)] {
                assert_eq!(xy_vsfilter_search_field(owner, 0), 1);
                assert_eq!(xy_vsfilter_search_field(owner, 1), index);
                assert_eq!(xy_vsfilter_search_field(owner, 2), start);
                xy_vsfilter_search_supply(owner, value);
            }
            assert_eq!(xy_vsfilter_search_field(owner, 0), 0);
            assert_eq!(xy_vsfilter_search_field(owner, 3), -1);
            assert_eq!(xy_vsfilter_search_field(owner, 4), 0);
            xy_vsfilter_search_release(owner);
            xy_vsfilter_search_release(core::ptr::null_mut());
        }
        assert!(xy_vsfilter_search_create(u32::MAX, 0, 0).is_null());
    }
}
