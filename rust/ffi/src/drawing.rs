// SPDX-License-Identifier: GPL-2.0-or-later
//! Scalar-only private drawing owner; no pointer to path storage escapes Rust.
use alloc::boxed::Box;
use xy_vsfilter_compat::drawing::{Drawing, Point};

#[no_mangle]
pub extern "C" fn xy_vsfilter_drawing_create() -> *mut Drawing {
    Box::into_raw(Box::new(Drawing::default()))
}

/// # Safety
/// owner is the live unique pointer from create, with no concurrent operations.
/// Start a command (op 0, x=raw command unit); supply successful points (op 1)
/// or failure (op 2) until the return is zero, before the next command. Op 3
/// finishes the path. Coordinate arguments are ignored for failure and finish.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_drawing_step(
    owner: *mut Drawing,
    op: u32,
    x: i32,
    y: i32,
) -> u32 {
    // SAFETY: the noncopyable C++ owner supplies exclusive access for this call.
    let path = unsafe { &mut *owner };
    u32::from(match op {
        0 => path.command(x as u32),
        1 => path.point(Some(Point { x, y })),
        2 => path.point(None),
        3 => path.finish(),
        _ => false,
    })
}

/// # Safety
/// owner remains live and immutable for this call. Fields: 0=count, 1=type,
/// 2=packed x/y signed 32-bit coordinates. Invalid indices/fields return zero.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_drawing_field(
    owner: *const Drawing,
    field: u32,
    index: usize,
) -> u64 {
    // SAFETY: the caller retains the owner and does not mutate during queries.
    let vertices = unsafe { &*owner }.vertices();
    if field == 0 {
        return vertices.len() as u64;
    }
    let Some(vertex) = vertices.get(index) else {
        return 0;
    };
    match field {
        1 => u64::from(vertex.kind),
        2 => u64::from(vertex.point.x as u32) | (u64::from(vertex.point.y as u32) << 32),
        _ => 0,
    }
}

/// # Safety
/// Release the unique pointer from create exactly once after all queries; null
/// is accepted. No native code frees an allocation or retains a borrowed point.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_drawing_release(owner: *mut Drawing) {
    if !owner.is_null() {
        // SAFETY: the C++ owner transfers the unique allocation back to Rust.
        drop(unsafe { Box::from_raw(owner) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn packed_signed_coordinates_and_unique_owner_lifetime() {
        let owner = xy_vsfilter_drawing_create();
        // SAFETY: one live owner, sequential complete commands, released once.
        unsafe {
            assert_eq!(xy_vsfilter_drawing_step(owner, 0, i32::from(b'm'), 0), 1);
            assert_eq!(xy_vsfilter_drawing_step(owner, 1, i32::MIN, i32::MAX), 1);
            assert_eq!(xy_vsfilter_drawing_step(owner, 2, 0, 0), 0);
            assert_eq!(xy_vsfilter_drawing_step(owner, 3, 0, 0), 1);
            assert_eq!(xy_vsfilter_drawing_field(owner, 0, 0), 1);
            assert_eq!(xy_vsfilter_drawing_field(owner, 1, 0), 6);
            assert_eq!(
                xy_vsfilter_drawing_field(owner, 2, 0),
                0x7fff_ffff_8000_0000
            );
            assert_eq!(xy_vsfilter_drawing_field(owner, 2, usize::MAX), 0);
            xy_vsfilter_drawing_release(owner);
        }
    }
}
