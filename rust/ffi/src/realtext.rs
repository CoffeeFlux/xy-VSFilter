// SPDX-License-Identifier: GPL-3.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::realtext::Tags;
use xy_vsfilter_compat::realtext_attributes::Attributes;
use xy_vsfilter_compat::realtext_lower::Lower;
use xy_vsfilter_compat::realtext_tag::Tag;
use xy_vsfilter_compat::realtext_text::Text;
use xy_vsfilter_compat::realtext_timecode::Timecode;
use xy_vsfilter_compat::realtext_timeline::Timeline;

/// # Safety
/// For accepted precision, output provides four aligned writable i32 slots.
/// On declined precision it may be null and is not accessed. Successful calls
/// initialize every slot.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_format(
    time: i32,
    precision: i32,
    output: *mut i32,
) -> u32 {
    let Some(parts) = xy_vsfilter_compat::realtext_format::components(time, precision) else {
        return 0;
    };
    for (index, part) in parts.into_iter().enumerate() {
        // SAFETY: caller supplies four writable aligned slots; no prior values
        // are read and every in-bounds slot is initialized exactly once.
        unsafe {
            output.add(index).write(part);
        }
    }
    1
}

#[no_mangle]
pub extern "C" fn xy_vsfilter_realtext_timeline_create() -> *mut Timeline {
    Box::into_raw(Box::new(Timeline::default()))
}

/// # Safety
/// owner is live and uniquely borrowed during this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_timeline_select(
    owner: *mut Timeline,
    next: i32,
    final_line: u32,
    map_empty: u32,
) -> u32 {
    // SAFETY: unique native RAII owner; no native storage is borrowed.
    unsafe {
        let state = &mut *owner;
        state.select(
            if final_line != 0 { None } else { Some(next) },
            map_empty != 0,
        )
    }
}

/// # Safety
/// owner is live and uniquely borrowed, after native map insertion completes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_timeline_advance(
    owner: *mut Timeline,
    start: i32,
    end: i32,
) {
    // SAFETY: exclusive live owner; scalar values are copied into owned history.
    unsafe {
        let state = &mut *owner;
        state.advance(start, end);
    }
}

/// # Safety
/// owner is live and immutable. Invalid queries return zero.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_timeline_field(
    owner: *const Timeline,
    field: u32,
) -> i32 {
    // SAFETY: immutable scalar query before the unique release.
    let state = unsafe { &*owner };
    match field {
        0 => state.first,
        1 => state.second,
        _ => 0,
    }
}

/// # Safety
/// Release the unique owner once after all other operations.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_timeline_release(owner: *mut Timeline) {
    if !owner.is_null() {
        // SAFETY: native RAII returns its uniquely owned allocation once.
        drop(unsafe { Box::from_raw(owner) });
    }
}

#[no_mangle]
pub extern "C" fn xy_vsfilter_realtext_lower_create() -> *mut Lower {
    Box::into_raw(Box::new(Lower::default()))
}

/// # Safety
/// owner is live and exclusive; input is a disjoint initialized u16 span.
/// No input loan survives the call or reaches native case conversion.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_lower_next(
    owner: *mut Lower,
    input: *const u16,
    length: usize,
    response: u16,
) -> u32 {
    // SAFETY: live unique owner and disjoint initialized native input.
    unsafe {
        let state = &mut *owner;
        u32::from(state.next(units(input, length), response))
    }
}

/// # Safety
/// owner is live and immutable. Invalid scalar queries return zero.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_lower_field(
    owner: *const Lower,
    field: u32,
    index: usize,
) -> usize {
    // SAFETY: immutable bounded query before release.
    let state = unsafe { &*owner };
    match field {
        0 => usize::from(state.character),
        1 => state.text.len(),
        2 => usize::from(state.text.get(index).copied().unwrap_or(0)),
        _ => 0,
    }
}

/// # Safety
/// Release the uniquely owned state once, after all operations.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_lower_release(owner: *mut Lower) {
    if !owner.is_null() {
        // SAFETY: native RAII returns its uniquely owned allocation once.
        drop(unsafe { Box::from_raw(owner) });
    }
}

#[no_mangle]
pub extern "C" fn xy_vsfilter_realtext_text_create() -> *mut Text {
    Box::into_raw(Box::new(Text::new()))
}

/// # Safety
/// owner is live and exclusively borrowed. Input is a disjoint initialized
/// u16 span; this loan ends before native callbacks or string updates resume.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_text_next(
    owner: *mut Text,
    input: *const u16,
    length: usize,
    ignore: u32,
    response: u32,
) -> u32 {
    // SAFETY: initialized native input and a disjoint uniquely owned state.
    unsafe {
        let state = &mut *owner;
        state.next(units(input, length), ignore != 0, response != 0)
    }
}

/// # Safety
/// owner is live and immutable. Invalid scalar queries return zero.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_text_field(
    owner: *const Text,
    field: u32,
    index: usize,
) -> usize {
    // SAFETY: bounded immutable access before release.
    let state = unsafe { &*owner };
    match field {
        0 => state.position as usize,
        1 => usize::from(state.character),
        2 => state.text.len(),
        3 => usize::from(state.text.get(index).copied().unwrap_or(0)),
        _ => 0,
    }
}

/// # Safety
/// Release the unique owner exactly once after all operations.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_text_release(owner: *mut Text) {
    if !owner.is_null() {
        // SAFETY: noncopyable native RAII returns the uniquely owned allocation.
        drop(unsafe { Box::from_raw(owner) });
    }
}

#[no_mangle]
pub extern "C" fn xy_vsfilter_realtext_tag_create() -> *mut Tag {
    Box::into_raw(Box::new(Tag::new()))
}

/// # Safety
/// owner is live and uniquely borrowed; input is a disjoint initialized u16
/// span, borrowed only for this call. Native callbacks run after it returns.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_tag_next(
    owner: *mut Tag,
    input: *const u16,
    length: usize,
    ignore: u32,
    response: u32,
    position: u32,
) -> u32 {
    // SAFETY: exclusive owner and disjoint initialized native input.
    unsafe {
        let state = &mut *owner;
        state.next(units(input, length), ignore != 0, response != 0, position)
    }
}

/// # Safety
/// owner is live and immutable. Invalid scalar queries return zero.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_tag_field(
    owner: *const Tag,
    field: u32,
    index: usize,
) -> usize {
    // SAFETY: immutable query before release of the live owner.
    let state = unsafe { &*owner };
    match field {
        0 => state.position as usize,
        1 => state.patch as usize,
        2 => usize::from(state.character),
        3 => state.consume,
        4 => state.text.len(),
        5 => usize::from(state.text.get(index).copied().unwrap_or(0)),
        _ => 0,
    }
}

/// # Safety
/// Release the unique owner once, after all other operations.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_tag_release(owner: *mut Tag) {
    if !owner.is_null() {
        // SAFETY: native RAII returns its uniquely owned allocation once.
        drop(unsafe { Box::from_raw(owner) });
    }
}

#[no_mangle]
pub extern "C" fn xy_vsfilter_realtext_timecode_create(length: usize) -> *mut Timecode {
    Box::into_raw(Box::new(Timecode::new(length)))
}

/// # Safety
/// owner is live and uniquely borrowed; input is a disjoint initialized u16
/// span. No input borrow survives this call or reaches native CRT conversion.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_timecode_next(
    owner: *mut Timecode,
    input: *const u16,
    length: usize,
) -> u32 {
    // SAFETY: exclusive live owner and disjoint initialized native input.
    unsafe {
        let state = &mut *owner;
        state.next(units(input, length))
    }
}

/// # Safety
/// owner is live and immutable. Fields: final-field flag, missing index, part
/// length and bounded part units. Invalid queries return zero.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_timecode_field(
    owner: *const Timecode,
    field: u32,
    index: usize,
) -> usize {
    // SAFETY: immutable scalar query before the unique release.
    let state = unsafe { &*owner };
    match field {
        0 => usize::from(state.last),
        1 => state.missing,
        2 => state.part.len(),
        3 => usize::from(state.part.get(index).copied().unwrap_or(0)),
        _ => 0,
    }
}

/// # Safety
/// Release the unique owner once, after all other operations.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_realtext_timecode_release(owner: *mut Timecode) {
    if !owner.is_null() {
        // SAFETY: native RAII returns the uniquely owned allocation once.
        drop(unsafe { Box::from_raw(owner) });
    }
}

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
    fn timestamp_components_initialize_all_output_without_reading_it() {
        let mut output = [core::mem::MaybeUninit::<i32>::uninit(); 4];
        // SAFETY: four aligned writable slots; success initializes all of them.
        // Declined precision does not access its null output pointer.
        unsafe {
            assert_eq!(
                xy_vsfilter_realtext_format(-3_723_999, 1, output.as_mut_ptr().cast()),
                1
            );
            assert_eq!(output.map(|part| part.assume_init()), [-1, -2, -3, -49]);
            assert_eq!(
                xy_vsfilter_realtext_format(999, i32::MIN, core::ptr::null_mut()),
                0
            );
        }
    }

    #[test]
    fn timeline_owns_history_across_native_line_insertions() {
        let owner = xy_vsfilter_realtext_timeline_create();
        // SAFETY: unique live owner with only scalar calls and one release.
        unsafe {
            assert_eq!(xy_vsfilter_realtext_timeline_select(owner, 1000, 0, 1), 1);
            assert_eq!(xy_vsfilter_realtext_timeline_field(owner, 0), 0);
            assert_eq!(xy_vsfilter_realtext_timeline_field(owner, 1), 1000);
            xy_vsfilter_realtext_timeline_advance(owner, 1000, 0);
            assert_eq!(xy_vsfilter_realtext_timeline_select(owner, 500, 0, 0), 2);
            assert_eq!(xy_vsfilter_realtext_timeline_field(owner, 0), 1000);
            xy_vsfilter_realtext_timeline_advance(owner, 500, 2000);
            assert_eq!(xy_vsfilter_realtext_timeline_select(owner, 0, 1, 0), 1);
            assert_eq!(xy_vsfilter_realtext_timeline_field(owner, 0), 500);
            assert_eq!(xy_vsfilter_realtext_timeline_field(owner, 1), 2000);
            assert_eq!(xy_vsfilter_realtext_timeline_field(owner, u32::MAX), 0);
            xy_vsfilter_realtext_timeline_release(owner);
        }
    }

    #[test]
    fn lowercase_input_can_change_between_native_conversions() {
        let owner = xy_vsfilter_realtext_lower_create();
        let mut input = [65u16, 66];
        // SAFETY: unique live owner, initialized short-lived input loans,
        // bounded queries, and exactly one release.
        unsafe {
            assert_eq!(
                xy_vsfilter_realtext_lower_next(owner, input.as_ptr(), 2, 0),
                1
            );
            input[1] = 90;
            assert_eq!(
                xy_vsfilter_realtext_lower_next(owner, input.as_ptr(), 2, 97),
                1
            );
            assert_eq!(xy_vsfilter_realtext_lower_field(owner, 0, 0), 90);
            assert_eq!(
                xy_vsfilter_realtext_lower_next(owner, core::ptr::null(), 0, 122),
                0
            );
            assert_eq!(xy_vsfilter_realtext_lower_field(owner, 1, 0), 2);
            assert_eq!(xy_vsfilter_realtext_lower_field(owner, 2, 1), 122);
            assert_eq!(xy_vsfilter_realtext_lower_field(owner, 2, usize::MAX), 0);
            xy_vsfilter_realtext_lower_release(owner);
        }
    }

    #[test]
    fn text_scanning_does_not_keep_native_input_borrows() {
        let owner = xy_vsfilter_realtext_text_create();
        let mut input = [97u16, 0, 0xd800, 60];
        // SAFETY: initialized input loaned only during each call, uniquely
        // owned live state, scalar queries, and a single final release.
        unsafe {
            assert_eq!(
                xy_vsfilter_realtext_text_next(owner, input.as_ptr(), 4, 1, 0),
                2
            );
            input[0] = 98;
            assert_eq!(
                xy_vsfilter_realtext_text_next(owner, input.as_ptr(), 4, 1, 0),
                4
            );
            input.fill(7);
            assert_eq!(xy_vsfilter_realtext_text_field(owner, 2, 0), 3);
            assert_eq!(xy_vsfilter_realtext_text_field(owner, 3, 0), 98);
            assert_eq!(xy_vsfilter_realtext_text_field(owner, 3, 2), 0xd800);
            assert_eq!(xy_vsfilter_realtext_text_field(owner, 3, usize::MAX), 0);
            xy_vsfilter_realtext_text_release(owner);
        }
    }

    #[test]
    fn tag_name_storage_survives_native_input_changes() {
        let owner = xy_vsfilter_realtext_tag_create();
        let mut input = [60u16, 33, 0xd800, 0, 62, 120];
        // SAFETY: unique live owner, initialized input for each short loan,
        // bounded scalar queries, and a single release after all operations.
        unsafe {
            assert_eq!(
                xy_vsfilter_realtext_tag_next(owner, input.as_ptr(), 6, 1, 0, 0),
                6
            );
            input.fill(7);
            assert_eq!(xy_vsfilter_realtext_tag_field(owner, 4, 0), 3);
            assert_eq!(xy_vsfilter_realtext_tag_field(owner, 5, 1), 0xd800);
            assert_eq!(xy_vsfilter_realtext_tag_field(owner, 5, usize::MAX), 0);
            assert_eq!(
                xy_vsfilter_realtext_tag_next(owner, core::ptr::null(), 0, 1, 0, 0),
                0
            );
            xy_vsfilter_realtext_tag_release(owner);
        }
    }

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

    #[test]
    fn timecode_fields_outlive_their_native_input_loans() {
        let owner = xy_vsfilter_realtext_timecode_create(5);
        let mut input = [49u16, 58, 0xd800, 0, 50];
        // SAFETY: initialized input during each short loan; only scalar queries
        // occur afterward, followed by a unique release.
        unsafe {
            assert_eq!(
                xy_vsfilter_realtext_timecode_next(owner, input.as_ptr(), 5),
                1
            );
            input.fill(7);
            assert_eq!(xy_vsfilter_realtext_timecode_field(owner, 2, 0), 3);
            assert_eq!(xy_vsfilter_realtext_timecode_field(owner, 3, 0), 0xd800);
            assert_eq!(xy_vsfilter_realtext_timecode_field(owner, 3, usize::MAX), 0);
            assert_eq!(
                xy_vsfilter_realtext_timecode_next(owner, core::ptr::null(), 0),
                2
            );
            xy_vsfilter_realtext_timecode_release(owner);
        }
    }
}
