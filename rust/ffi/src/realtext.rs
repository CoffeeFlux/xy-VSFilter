// SPDX-License-Identifier: GPL-3.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::realtext::Tags;

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
    unsafe { (&mut *owner).begin(units(name, length), flags) as u32 }
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
    unsafe { (&*owner).attribute_kind(units(name, length)) }
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
        (&mut *owner).attribute(
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
        (&mut *owner).end_font();
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
    let result = unsafe { (&*owner).result() };
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
}
