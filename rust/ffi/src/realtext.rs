// SPDX-License-Identifier: GPL-3.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::realtext::Tags;
use xy_vsfilter_compat::realtext_attributes::Attributes;

#[no_mangle]
pub extern "C" fn xy_vsfilter_realtext_attributes_create(position: u32) -> *mut Attributes {
    Box::into_raw(Box::new(Attributes::new(position)))
}

/// # Safety
/// owner is live and uniquely borrowed; input is an initialized aligned u16
/// span disjoint from owner. Its borrow ends before native callbacks resume.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_attributes_next(
    owner: *mut Attributes,
    input: *const u16,
    length: usize,
    ignore: u32,
    response: u32,
) -> u32 {
    // SAFETY: disjoint immutable input and unique live state, supplied by bridge.
    unsafe {
        let state = &mut *owner;
        state.next(units(input, length), ignore != 0, response != 0)
    }
}

/// # Safety
/// owner remains live and immutable. Queries return position, requested unit,
/// name length/unit and value length/unit; out-of-range queries return zero.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_attributes_field(
    owner: *const Attributes,
    field: u32,
    index: usize,
) -> usize {
    // SAFETY: immutable query before the unique owner is released.
    let state = unsafe { &*owner };
    match field {
        0 => state.position as usize,
        1 => usize::from(state.character),
        2 => state.name.len(),
        3 => usize::from(state.name.get(index).copied().unwrap_or(0)),
        4 => state.value.len(),
        5 => usize::from(state.value.get(index).copied().unwrap_or(0)),
        _ => 0,
    }
}

/// # Safety
/// Release the unique owner exactly once, after all other operations.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_attributes_release(owner: *mut Attributes) {
    if !owner.is_null() {
        // SAFETY: the RAII adapter transfers its uniquely owned allocation back.
        drop(unsafe { Box::from_raw(owner) });
    }
}

// SAFETY required by every caller below: nonempty spans are initialized aligned
// u16 arrays, disjoint from the live uniquely owned Tags state. They remain
// immutable during the call; the safe core retains no borrowed native storage.
unsafe fn units<'a>(data: *const u16, length: usize) -> &'a [u16] {
    if length == 0 {
        &[]
    } else {
        // SAFETY: private bridge supplies the documented typed span.
        unsafe { core::slice::from_raw_parts(data, length) }
    }
}

#[no_mangle]
pub extern "C" fn xy_vsfilter_realtext_create(
    ignore: u32,
    minimum: i32,
    maximum: i32,
) -> *mut Tags {
    Box::into_raw(Box::new(Tags::new(ignore, minimum, maximum)))
}

/// # Safety
/// owner is live and uniquely borrowed; name follows the immutable span
/// contract above. The name's lifetime ends at this call's return.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_begin(
    owner: *mut Tags,
    name: *const u16,
    length: usize,
    flags: u32,
) -> u32 {
    // SAFETY: disjoint native input and exclusively accessed live owner.
    unsafe {
        let state = &mut *owner;
        state.begin(units(name, length), flags) as u32
    }
}

/// # Safety
/// owner is live and immutable; name follows the span contract above.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_attribute_kind(
    owner: *const Tags,
    name: *const u16,
    length: usize,
) -> u32 {
    // SAFETY: immutable live owner and initialized native name.
    unsafe {
        let state = &*owner;
        state.attribute_kind(units(name, length))
    }
}

/// # Safety
/// owner is live and uniquely borrowed. Both strings follow the span contract
/// above; no input borrow survives this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_attribute(
    owner: *mut Tags,
    name: *const u16,
    name_length: usize,
    value: *const u16,
    value_length: usize,
    numeric: u32,
    size: i32,
) {
    // SAFETY: uniquely borrowed owner, disjoint immutable native strings.
    unsafe {
        let state = &mut *owner;
        state.attribute(
            units(name, name_length),
            units(value, value_length),
            numeric != 0,
            size,
        );
    }
}

/// # Safety
/// owner is live and uniquely borrowed during this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_end(owner: *mut Tags) {
    // SAFETY: bridge exclusively owns this live object.
    unsafe {
        let state = &mut *owner;
        state.end_font();
    }
}

/// # Safety
/// owner is live and immutable. Field 0 returns length, 1 returns a bounded code
/// unit. Invalid queries return zero.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_field(
    owner: *const Tags,
    field: u32,
    index: usize,
) -> usize {
    // SAFETY: immutable access to the retained owner.
    let state = unsafe { &*owner };
    let result = state.result();
    match field {
        0 => result.len(),
        1 => usize::from(result.get(index).copied().unwrap_or(0)),
        _ => 0,
    }
}

/// # Safety
/// Release the unique owner exactly once, after all other calls.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_release(owner: *mut Tags) {
    if !owner.is_null() {
        // SAFETY: the noncopyable C++ bridge transfers its allocation back once.
        drop(unsafe { Box::from_raw(owner) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn names_and_attributes_are_owned_after_each_call() {
        let owner = xy_vsfilter_realtext_create(0, 14, 25);
        // SAFETY: live exclusive owner, initialized temporary inputs, bounded
        // scalar queries before a unique release.
        unsafe {
            {
                let name = [102u16, 111, 110, 116];
                assert_eq!(xy_vsfilter_realtext_begin(owner, name.as_ptr(), 4, 3), 1);
            }
            {
                let name = [115u16, 105, 122, 101];
                let value = [50u16, 48];
                assert_eq!(
                    xy_vsfilter_realtext_attribute_kind(owner, name.as_ptr(), 4),
                    2
                );
                xy_vsfilter_realtext_attribute(owner, name.as_ptr(), 4, value.as_ptr(), 2, 1, 20);
            }
            xy_vsfilter_realtext_end(owner);
            assert_eq!(xy_vsfilter_realtext_field(owner, 0, 0), 0);
            {
                let text = [0xd800u16, 0];
                xy_vsfilter_realtext_begin(owner, text.as_ptr(), 2, 4);
            }
            let len = xy_vsfilter_realtext_field(owner, 0, 0);
            assert_eq!(xy_vsfilter_realtext_field(owner, 1, len - 2), 0xd800);
            assert_eq!(xy_vsfilter_realtext_field(owner, 1, usize::MAX), 0);
            xy_vsfilter_realtext_release(owner);
        }
    }

    #[test]
    fn attribute_input_loans_end_before_native_callbacks() {
        let owner = xy_vsfilter_realtext_attributes_create(0);
        let mut input = [97u16, 61, 49, 62];
        // SAFETY: uniquely owned state, initialized input loaned only for each
        // call, bounded queries and one release. Native storage changes between
        // callbacks while the owned parser remains live.
        unsafe {
            assert_eq!(
                xy_vsfilter_realtext_attributes_next(owner, input.as_ptr(), 4, 0, 0),
                1
            );
            input[0] = 98;
            let mut action = 1;
            while action == 1 {
                action = xy_vsfilter_realtext_attributes_next(owner, input.as_ptr(), 4, 0, 0);
            }
            assert_eq!(action, 2);
            input.fill(7);
            assert_eq!(xy_vsfilter_realtext_attributes_field(owner, 3, 0), 98);
            assert_eq!(xy_vsfilter_realtext_attributes_field(owner, 5, 0), 49);
            assert_eq!(
                xy_vsfilter_realtext_attributes_field(owner, 5, usize::MAX),
                0
            );
            xy_vsfilter_realtext_attributes_release(owner);
        }
    }
}
