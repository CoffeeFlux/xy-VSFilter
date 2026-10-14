// SPDX-License-Identifier: GPL-2.0-or-later
use alloc::boxed::Box;
use xy_vsfilter_compat::drawing_tokens::Tokens;

/// # Safety
/// For nonzero length, input is a readable aligned array of length u16 units,
/// immutable throughout this call. Nothing retains a borrow of that storage.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_drawing_tokens_create(
    input: *const u16,
    length: u32,
) -> *mut Tokens {
    if length == 0 {
        return Box::into_raw(Box::new(Tokens::scan(&[])));
    }
    if input.is_null() || u64::from(length) > isize::MAX as u64 / 2 {
        return core::ptr::null_mut();
    }
    // SAFETY: caller owns the immutable typed input array for this call.
    let input = unsafe { core::slice::from_raw_parts(input, length as usize) };
    Box::into_raw(Box::new(Tokens::scan(input)))
}

/// # Safety
/// owner is live and exclusively accessible until this call returns.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_drawing_tokens_prepare(owner: *mut Tokens) -> u32 {
    // SAFETY: the bridge holds no input borrow or callback across this mutation.
    u32::from(unsafe { &mut *owner }.prepare())
}

/// # Safety
/// owner is live and exclusive; nonempty input is aligned and initialized for
/// length u16 values, immutable and disjoint from owner; length*2 fits isize.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_drawing_tokens_reset(
    owner: *mut Tokens,
    input: *const u16,
    length: u32,
) {
    let input = if length == 0 {
        &[]
    } else {
        // SAFETY: a fresh native-prepared copy, borrowed only until return.
        unsafe { core::slice::from_raw_parts(input, length as usize) }
    };
    // SAFETY: exclusive owner and disjoint input copy.
    unsafe { *owner = Tokens::scan(input) };
}

/// # Safety
/// owner is live and not concurrently mutated or released. Fields: 0=count,
/// 1=command, 2=parameter length, 3=parameter unit. Invalid queries return zero.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_drawing_tokens_field(
    owner: *const Tokens,
    field: u32,
    token: usize,
    unit: usize,
) -> usize {
    // SAFETY: bridge retains the immutable owner throughout the query.
    let tokens = unsafe { &*owner };
    if field == 0 {
        return tokens.tokens().len();
    }
    let Some(item) = tokens.tokens().get(token) else {
        return 0;
    };
    match field {
        1 => usize::from(item.command),
        2 => item.parameters.len(),
        3 => tokens
            .parameters(token)
            .and_then(|p| p.get(unit))
            .copied()
            .unwrap_or(0) as usize,
        _ => 0,
    }
}

/// # Safety
/// Release the unique owner returned by create exactly once, after all queries.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_drawing_tokens_release(owner: *mut Tokens) {
    if !owner.is_null() {
        // SAFETY: noncopyable C++ RAII owner transfers the allocation back once.
        drop(unsafe { Box::from_raw(owner) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn input_borrow_ends_at_creation_and_queries_stay_bounded() {
        let owner = {
            let input = [109u16, 32, 0xd800, 32, 49];
            // SAFETY: live aligned input, valid explicit length.
            unsafe { xy_vsfilter_drawing_tokens_create(input.as_ptr(), input.len() as u32) }
        };
        // SAFETY: live unique owner; queries finish before exactly one release.
        unsafe {
            assert_eq!(xy_vsfilter_drawing_tokens_field(owner, 0, 0, 0), 1);
            assert_eq!(xy_vsfilter_drawing_tokens_field(owner, 1, 0, 0), 109);
            assert_eq!(xy_vsfilter_drawing_tokens_field(owner, 3, 0, 0), 0xd800);
            assert_eq!(xy_vsfilter_drawing_tokens_field(owner, 3, 0, usize::MAX), 0);
            xy_vsfilter_drawing_tokens_release(owner);
        }
    }

    #[test]
    fn fallback_reimports_a_native_copy_and_preparation_replaces_tokens() {
        let input = [109u16, 0, 49];
        let mut prepared = [42u16, 109, 32, 49, 42, 108, 32, 50];
        // SAFETY: initialized disjoint inputs, live unique owner, one release.
        unsafe {
            let owner = xy_vsfilter_drawing_tokens_create(input.as_ptr(), 3);
            assert_eq!(xy_vsfilter_drawing_tokens_prepare(owner), 0);
            xy_vsfilter_drawing_tokens_reset(owner, prepared.as_ptr(), 8);
            prepared.fill(0);
            assert_eq!(xy_vsfilter_drawing_tokens_field(owner, 0, 0, 0), 2);
            assert_eq!(xy_vsfilter_drawing_tokens_field(owner, 3, 1, 0), 50);
            assert_eq!(xy_vsfilter_drawing_tokens_prepare(owner), 1);
            assert_eq!(xy_vsfilter_drawing_tokens_field(owner, 0, 0, 0), 2);
            xy_vsfilter_drawing_tokens_release(owner);
        }
    }
}
