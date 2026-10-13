// SPDX-License-Identifier: GPL-3.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::dvb::{Decoder, Mode, Request};

#[no_mangle]
pub extern "C" fn xy_vsfilter_dvb_create(bits: u32) -> *mut Decoder {
    let mode = match bits {
        2 => Mode::Two,
        4 => Mode::Four,
        8 => Mode::Eight,
        _ => return core::ptr::null_mut(),
    };
    Box::into_raw(Box::new(Decoder::new(mode)))
}

/// # Safety
/// owner is the unique live pointer from create, without concurrent access.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_dvb_supply(owner: *mut Decoder, value: u8) {
    // SAFETY: the noncopyable native wrapper supplies exclusive access.
    unsafe { &mut *owner }.supply(value);
}

/// Fields: 0=request (0=done, 1=EOF, 2=bits, 3=byte, 4=run); 1=bit count;
/// 2=run count; 3=palette. Inapplicable/invalid fields return zero.
/// # Safety
/// owner remains live and immutable for this call. No storage escapes Rust.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_dvb_field(owner: *const Decoder, field: u32) -> i32 {
    // SAFETY: the native wrapper retains the live owner without mutation.
    match (field, unsafe { &*owner }.request()) {
        (0, Request::Done) => 0,
        (0, Request::Eof) => 1,
        (0, Request::Bits(_)) => 2,
        (0, Request::Byte) => 3,
        (0, Request::Run { .. }) => 4,
        (1, Request::Bits(bits)) => i32::from(bits),
        (2, Request::Run { count, .. }) => i32::from(count),
        (3, Request::Run { palette, .. }) => i32::from(palette),
        _ => 0,
    }
}

/// # Safety
/// Return the unique live owner exactly once after all calls; null is allowed.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_dvb_release(owner: *mut Decoder) {
    if !owner.is_null() {
        // SAFETY: exclusive ownership returns to Rust without outstanding borrows.
        drop(unsafe { Box::from_raw(owner) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn protocol_and_owner_lifetime() {
        assert!(xy_vsfilter_dvb_create(3).is_null());
        let owner = xy_vsfilter_dvb_create(2);
        // SAFETY: one live owner, sequential calls, released exactly once.
        unsafe {
            xy_vsfilter_dvb_supply(owner, 0);
            assert_eq!(xy_vsfilter_dvb_field(owner, 0), 2);
            assert_eq!(xy_vsfilter_dvb_field(owner, 1), 2);
            xy_vsfilter_dvb_supply(owner, 3);
            assert_eq!(xy_vsfilter_dvb_field(owner, 0), 4);
            assert_eq!(xy_vsfilter_dvb_field(owner, 2), 1);
            assert_eq!(xy_vsfilter_dvb_field(owner, 3), 3);
            xy_vsfilter_dvb_supply(owner, 0);
            xy_vsfilter_dvb_supply(owner, 1);
            assert_eq!(xy_vsfilter_dvb_field(owner, 0), 0);
            xy_vsfilter_dvb_release(owner);
        }
    }
}

use xy_vsfilter_compat::dvb::{FieldDecoder, FieldRequest};

#[no_mangle]
pub extern "C" fn xy_vsfilter_dvb_field_create(
    position: i32,
    length: i16,
    x: i16,
    y: i16,
) -> *mut FieldDecoder {
    Box::into_raw(Box::new(FieldDecoder::new(position, length, x, y)))
}

/// # Safety
/// owner is the unique live field pointer, without concurrent access.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_dvb_field_supply(
    owner: *mut FieldDecoder,
    value: i32,
    x: i16,
    y: i16,
) {
    // SAFETY: the noncopyable native wrapper supplies exclusive access.
    unsafe { &mut *owner }.supply(value, x, y);
}

/// Fields: 0=request (0=done, 1=position, 2=byte, 3=pixels, 4=skip,
/// 5=unknown); 1=pixel mode or skip count; 2=x; 3=y.
/// # Safety
/// owner remains live and immutable for this call. No storage escapes Rust.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_dvb_field_query(
    owner: *const FieldDecoder,
    field: u32,
) -> i32 {
    // SAFETY: the native wrapper retains the live owner without mutation.
    let decoder = unsafe { &*owner };
    match (field, decoder.request()) {
        (0, FieldRequest::Done) => 0,
        (0, FieldRequest::Position) => 1,
        (0, FieldRequest::Byte) => 2,
        (0, FieldRequest::Pixels(_)) => 3,
        (0, FieldRequest::Skip(_)) => 4,
        (0, FieldRequest::Unknown) => 5,
        (1, FieldRequest::Pixels(Mode::Two)) => 2,
        (1, FieldRequest::Pixels(Mode::Four)) => 4,
        (1, FieldRequest::Pixels(Mode::Eight)) => 8,
        (1, FieldRequest::Skip(count)) => i32::from(count),
        (2, _) => i32::from(decoder.coordinates().0),
        (3, _) => i32::from(decoder.coordinates().1),
        _ => 0,
    }
}

/// # Safety
/// Return the unique live field owner exactly once; null is allowed.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_dvb_field_release(owner: *mut FieldDecoder) {
    if !owner.is_null() {
        // SAFETY: ownership returns without outstanding borrows.
        drop(unsafe { Box::from_raw(owner) });
    }
}

#[cfg(test)]
mod field_tests {
    use super::*;
    #[test]
    fn field_owner_and_coordinate_protocol() {
        let owner = xy_vsfilter_dvb_field_create(4, 12, -1, 32767);
        // SAFETY: sequential calls to one live owner, released exactly once.
        unsafe {
            xy_vsfilter_dvb_field_supply(owner, 4, 0, 0);
            xy_vsfilter_dvb_field_supply(owner, 0x10, 0, 0);
            assert_eq!(xy_vsfilter_dvb_field_query(owner, 1), 2);
            xy_vsfilter_dvb_field_supply(owner, 0, 20, 32767);
            xy_vsfilter_dvb_field_supply(owner, 8, 0, 0);
            xy_vsfilter_dvb_field_supply(owner, 0xf0, 0, 0);
            assert_eq!(xy_vsfilter_dvb_field_query(owner, 2), -1);
            assert_eq!(xy_vsfilter_dvb_field_query(owner, 3), -32767);
            xy_vsfilter_dvb_field_release(owner);
        }
    }
}
