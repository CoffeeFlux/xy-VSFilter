// SPDX-License-Identifier: GPL-3.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::hdmv::{Decoder, Request};

#[no_mangle]
pub extern "C" fn xy_vsfilter_hdmv_create(x: i16, y: i16, height: i16) -> *mut Decoder {
    Box::into_raw(Box::new(Decoder::new(x, y, height)))
}

/// # Safety
/// owner is the unique live pointer from create, without concurrent access.
/// No native calls or native unwinding occur inside this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_hdmv_supply(owner: *mut Decoder, value: u8) {
    // SAFETY: the noncopyable native wrapper supplies exclusive access.
    unsafe { &mut *owner }.supply(value);
}

/// Fields: 0=request (0=done, 1=EOF, 2=byte, 3=paint); 1=x, 2=y, 3=width,
/// 4=palette index. Paint fields return zero outside a paint request.
/// # Safety
/// owner remains live and immutable for this call. No storage escapes Rust.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_hdmv_field(owner: *const Decoder, field: u32) -> i32 {
    // SAFETY: the native wrapper retains the live owner without mutation.
    let request = unsafe { &*owner }.request();
    match (field, request) {
        (0, Request::Done) => 0,
        (0, Request::Eof) => 1,
        (0, Request::Byte) => 2,
        (0, Request::Paint { .. }) => 3,
        (1, Request::Paint { x, .. }) => i32::from(x),
        (2, Request::Paint { y, .. }) => i32::from(y),
        (3, Request::Paint { width, .. }) => i32::from(width),
        (4, Request::Paint { palette, .. }) => i32::from(palette),
        _ => 0,
    }
}

/// # Safety
/// Return the unique live owner exactly once, after all queries. Null is allowed.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_hdmv_release(owner: *mut Decoder) {
    if !owner.is_null() {
        // SAFETY: exclusive ownership returns to Rust without outstanding borrows.
        drop(unsafe { Box::from_raw(owner) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn native_protocol_and_owner_lifetime() {
        let owner = xy_vsfilter_hdmv_create(-2, 3, 1);
        // SAFETY: one live owner, sequential calls, released once.
        unsafe {
            assert_eq!(xy_vsfilter_hdmv_field(owner, 0), 1);
            xy_vsfilter_hdmv_supply(owner, 0);
            assert_eq!(xy_vsfilter_hdmv_field(owner, 0), 2);
            xy_vsfilter_hdmv_supply(owner, 7);
            assert_eq!(xy_vsfilter_hdmv_field(owner, 0), 3);
            assert_eq!(xy_vsfilter_hdmv_field(owner, 1), -2);
            assert_eq!(xy_vsfilter_hdmv_field(owner, 2), 3);
            assert_eq!(xy_vsfilter_hdmv_field(owner, 3), 1);
            assert_eq!(xy_vsfilter_hdmv_field(owner, 4), 7);
            xy_vsfilter_hdmv_supply(owner, 0);
            xy_vsfilter_hdmv_supply(owner, 1);
            assert_eq!(xy_vsfilter_hdmv_field(owner, 0), 0);
            xy_vsfilter_hdmv_release(owner);
            xy_vsfilter_hdmv_release(core::ptr::null_mut());
        }
    }
}
