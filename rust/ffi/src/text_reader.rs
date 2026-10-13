// SPDX-License-Identifier: GPL-2.0-or-later
//! Private buffered-reader owner. Native I/O may borrow its fixed buffer between
//! calls, but must finish that borrow before any further Rust call or release.
use alloc::boxed::Box;
use xy_vsfilter_compat::{
    text::Encoding,
    text_reader::{Action, Reader},
};

#[no_mangle]
pub extern "C" fn xy_vsfilter_reader_create() -> *mut Reader {
    Box::into_raw(Box::new(Reader::default()))
}

/// # Safety
/// owner is the live unique pointer from reader_create, with no outstanding
/// buffer borrow. Operations: 0=begin line, 1=invalidate, 2=append replacement,
/// 3=finish read (argument=byte count), 4=advance (low bits=encoding, bit 8=BOM),
/// 5=decode one character (argument=encoding). Invalid operations return MAX.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_reader_step(
    owner: *mut Reader,
    op: u32,
    argument: u32,
) -> u64 {
    // SAFETY: the noncopyable bridge retains exclusive ownership for this call.
    let reader = unsafe { &mut *owner };
    match op {
        0 => {
            reader.begin_line();
            0
        }
        1 => {
            reader.invalidate();
            0
        }
        2 => {
            reader.append_replacement();
            0
        }
        3 => u64::from(reader.finish_read(argument as usize)),
        4 | 5 => {
            let encoding = match argument & 0xff {
                0 => Encoding::Default,
                1 => Encoding::Utf8,
                2 => Encoding::LittleEndian16,
                3 => Encoding::BigEndian16,
                _ => return u64::MAX,
            };
            if op == 5 {
                return u64::from(reader.decode(encoding).value as u32);
            }
            match reader.advance(encoding, argument & 0x100 != 0) {
                Action::Read => 0,
                Action::Line => 1,
                Action::Reopen => 2,
                Action::Log(c) => 3 | (u64::from(c as u32) << 32),
            }
        }
        _ => u64::MAX,
    }
}

/// # Safety
/// owner is live, not concurrently mutated/released, and not borrowed for I/O.
/// Fields: 0=cursor, 1=buffer count, 2=line length, 3=line unit at index.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_reader_field(
    owner: *const Reader,
    field: u32,
    index: usize,
) -> usize {
    // SAFETY: owner remains live and immutable throughout the query.
    let reader = unsafe { &*owner };
    match field {
        0 => reader.position(),
        1 => reader.count(),
        2 => reader.line().len(),
        3 => reader.line().get(index).copied().unwrap_or(0) as usize,
        _ => 0,
    }
}

/// Return the writable destination after step returned Read. Its length is
/// returned separately by reader_capacity BEFORE this call.
/// # Safety
/// owner is exclusively owned. Write at most that capacity; keep no pointer
/// after native Read returns/throws. During the borrow, make no Rust calls with
/// this owner, including queries. No Rust frame is active during native Read.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_reader_destination(owner: *mut Reader) -> *mut u8 {
    // SAFETY: exclusive owner; fixed array stays put until owner is released.
    unsafe { &mut *owner }.read_destination().as_mut_ptr()
}

/// # Safety
/// Same exclusive ownership contract as step, with no outstanding I/O borrow.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_reader_capacity(owner: *mut Reader) -> u32 {
    // SAFETY: exclusive owner for this call. The slice is not retained.
    unsafe { &mut *owner }.read_destination().len() as u32
}

/// # Safety
/// Release the unique pointer from reader_create exactly once, after all native
/// I/O and queries have ended. Null is accepted.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_reader_release(owner: *mut Reader) {
    if !owner.is_null() {
        // SAFETY: the bridge transfers its unique allocation back exactly once.
        drop(unsafe { Box::from_raw(owner) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn native_buffer_borrow_ends_before_resume_and_release() {
        let owner = xy_vsfilter_reader_create();
        // SAFETY: one owner; each native-style write ends before a Rust call.
        unsafe {
            assert_eq!(xy_vsfilter_reader_step(owner, 4, 1), 0);
            assert_eq!(xy_vsfilter_reader_capacity(owner), 4096);
            let buffer = xy_vsfilter_reader_destination(owner);
            buffer.write(b'a');
            buffer.add(1).write(0xf0);
            assert_eq!(xy_vsfilter_reader_step(owner, 3, 2), 1);
            assert_eq!(xy_vsfilter_reader_step(owner, 4, 1), 0);
            assert_eq!(xy_vsfilter_reader_field(owner, 0, 0), 1);
            assert_eq!(xy_vsfilter_reader_capacity(owner), 4095);
            let buffer = xy_vsfilter_reader_destination(owner);
            buffer.write(0x9f);
            buffer.add(1).write(0x98);
            buffer.add(2).write(0x80);
            buffer.add(3).write(10);
            assert_eq!(xy_vsfilter_reader_step(owner, 3, 4), 1);
            assert_eq!(xy_vsfilter_reader_step(owner, 4, 1), 1);
            assert_eq!(xy_vsfilter_reader_field(owner, 2, 0), 3);
            assert_eq!(xy_vsfilter_reader_field(owner, 3, 1), 0xd83d);
            assert_eq!(xy_vsfilter_reader_field(owner, 3, 2), 0xde00);
            assert_eq!(xy_vsfilter_reader_field(owner, 3, usize::MAX), 0);
            xy_vsfilter_reader_release(owner);
        }
    }
}
