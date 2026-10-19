// SPDX-License-Identifier: GPL-2.0-or-later
//! Detached ASS evaluation. Registry admission never spans CRT or native clip
//! construction. Revision checks refuse overwriting effects changed by reentry.
use crate::{
    document_registry::{Error, DOCUMENTS},
    effects::Admission,
    windows_crt,
};
use core::ffi::c_void;
use xy_vsfilter_compat::{
    ass_override::{self, Clip, Evaluation, Services, State},
    ass_tree::Tree,
    document_style::{self, StyleValue},
    effects,
};

type Gate = unsafe extern "C" fn();
type Callback = unsafe extern "C" fn(*mut c_void, *const u8, usize) -> u32;
fn error(e: Error) -> u32 {
    match e {
        Error::Busy => 2,
        Error::Exhausted => 5,
        _ => 1,
    }
}
struct Windows {
    gate: Admission,
    id: u64,
    revision: u64,
    callback: Option<Callback>,
    context: *mut c_void,
}
impl effects::Services for Windows {
    fn space(&mut self, u: u16) -> bool {
        windows_crt::space(u)
    }
    fn compare(&mut self, a: &[u16], b: &[u16]) -> i32 {
        windows_crt::compare(a, b)
    }
    fn scan(&mut self, input: &[u16], count: usize) -> (i32, [i32; 4]) {
        windows_crt::scan_effect(input, count)
    }
    fn integer(&mut self, input: &[u16]) -> i32 {
        windows_crt::number(input, 10).0
    }
    fn float(&mut self, input: &[u16]) -> f64 {
        windows_crt::float(input)
    }
}
impl Services for Windows {
    fn integer_base(&mut self, input: &[u16], base: i32) -> i32 {
        windows_crt::number(input, base).0
    }
    fn pow(&mut self, base: f64, exponent: f64) -> f64 {
        windows_crt::power(base, exponent)
    }
    fn checkpoint(
        &mut self,
        state: &State,
        style: &StyleValue,
        effects: &effects::State,
        clip: Option<Clip<'_>>,
    ) -> Result<(), u32> {
        let callback = self.callback.ok_or(3u32)?;
        let style = document_style::encode(style).map_err(|_| 3u32)?;
        let mut packet = state.encode();
        let text = clip.as_ref().map(|c| c.text).unwrap_or(&[]);
        for value in [
            u32::try_from(style.len()).map_err(|_| 5u32)?,
            u32::from(clip.is_some()),
            u32::try_from(text.len()).map_err(|_| 5u32)?,
            u32::from(clip.as_ref().is_some_and(|c| c.inverse)),
        ] {
            packet.extend_from_slice(&value.to_le_bytes());
        }
        for value in [
            clip.as_ref().map(|c| c.scale_x).unwrap_or(0.0),
            clip.as_ref().map(|c| c.scale_y).unwrap_or(0.0),
        ] {
            packet.extend_from_slice(&value.to_bits().to_le_bytes());
        }
        packet.extend_from_slice(&style);
        for unit in text {
            packet.extend_from_slice(&unit.to_le_bytes());
        }
        self.gate
            .call(|| crate::effects::commit(&DOCUMENTS, self.id, self.revision, effects.clone()))?;
        self.revision = self.revision.checked_add(1).ok_or(5u32)?;
        // SAFETY: entry contract supplies a synchronous nonunwinding callback
        // and its context. The complete detached packet lives through the call;
        // no registry loan or admission gate is held, and nothing is retained.
        let status = unsafe { callback(self.context, packet.as_ptr(), packet.len()) };
        if status != 0 {
            return Err(status);
        }
        // Even a final callback must not hide reentrant mutation. Earlier native
        // side effects are not rolled back, just as failed clip allocation isn't.
        let (revision, _) = self
            .gate
            .call(|| crate::effects::snapshot(&DOCUMENTS, self.id))?;
        if revision != self.revision {
            return Err(2);
        }
        Ok(())
    }
}
fn decode(bytes: &[u8]) -> Result<(State, StyleValue, StyleValue, bool), u32> {
    let header = bytes.get(..144).ok_or(3u32)?;
    let state = State::decode(&header[..132]).ok_or(3u32)?;
    let number =
        |offset| u32::from_le_bytes(header[offset..offset + 4].try_into().unwrap()) as usize;
    let a = number(132);
    let b = number(136);
    let animate = number(140);
    let middle = 144usize.checked_add(a).ok_or(5u32)?;
    if middle.checked_add(b) != Some(bytes.len()) || animate > 1 {
        return Err(3);
    }
    let current = document_style::decode(bytes.get(144..middle).ok_or(3u32)?).map_err(|_| 3u32)?;
    let original = document_style::decode(bytes.get(middle..).ok_or(3u32)?).map_err(|_| 3u32)?;
    Ok((state, current, original, animate != 0))
}
/// # Safety
/// Input is length readable bytes (count <= isize::MAX), copied before any
/// service. Callback/context are valid for this synchronous call only; callback
/// may read the detached packet only during its invocation, must catch native
/// exceptions and must not unwind. Paired gate callbacks provide same-thread
/// serialization of registry admission and cannot unwind. No pointer retained.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_ass_override_v1(
    tree_id: u64,
    list: u32,
    document_id: u64,
    effects_id: u64,
    input: *const u8,
    length: usize,
    context: *mut c_void,
    callback: Option<Callback>,
    enter: Option<Gate>,
    leave: Option<Gate>,
) -> u32 {
    if length > isize::MAX as usize {
        return 5;
    }
    if input.is_null() || callback.is_none() || enter.is_some() != leave.is_some() {
        return 3;
    }
    // SAFETY: readable input per caller, copied before admission and callbacks.
    let input = unsafe { core::slice::from_raw_parts(input, length) }.to_vec();
    let result = (|| {
        let (state, style, original, animate) = decode(&input)?;
        let gate = Admission { enter, leave };
        let (tree, names) = gate
            .call(|| {
                DOCUMENTS.try_with(|r| {
                    let tree = if tree_id == 0 {
                        Tree::default()
                    } else {
                        r.ass_tree(tree_id).map_err(error)?.clone()
                    };
                    let names = r
                        .document(document_id)
                        .map_err(error)?
                        .named_styles()
                        .clone();
                    Ok::<_, u32>((tree, names))
                })
            })
            .map_err(error)??;
        let (revision, effects) = gate.call(|| crate::effects::snapshot(&DOCUMENTS, effects_id))?;
        let mut svc = Windows {
            gate,
            id: effects_id,
            revision,
            context,
            callback,
        };
        let mut eval = Evaluation {
            state,
            style,
            original,
            effects,
        };
        eval.apply(&tree, list, animate, &names, &mut svc)?;
        svc.checkpoint(&eval.state, &eval.style, &eval.effects, None)
    })();
    result.err().unwrap_or(0)
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_ass_animation_v1(
    dst: f64,
    src: f64,
    animate: u32,
    time: i32,
    delay: i32,
    start: i32,
    end: i32,
    accel: f64,
) -> f64 {
    let state = State {
        time,
        delay,
        start,
        end,
        accel,
        ..State::default()
    };
    let mut svc = Windows {
        gate: Admission {
            enter: None,
            leave: None,
        },
        id: 0,
        revision: 0,
        context: core::ptr::null_mut(),
        callback: None,
    };
    ass_override::animation(&state, dst, src, animate != 0, &mut svc)
}

#[cfg(test)]
pub(crate) fn test_exports(document_id: u64) {
    use alloc::vec::Vec;
    use core::ptr;
    struct Observation {
        packets: Vec<Vec<u8>>,
        reenter: u64,
    }
    unsafe extern "C" fn observe(context: *mut c_void, bytes: *const u8, length: usize) -> u32 {
        // SAFETY: this test supplies a live exclusive Observation and the export
        // supplies a detached packet readable through this synchronous callback.
        let observation = unsafe { &mut *context.cast::<Observation>() };
        observation
            .packets
            .push(unsafe { core::slice::from_raw_parts(bytes, length) }.to_vec());
        if observation.reenter != 0 {
            assert_eq!(
                crate::effects::xy_vsfilter_effects_clear_v1(observation.reenter),
                0
            );
        }
        0
    }
    let mut input = State::default().encode();
    for value in [192u32, 192, 0] {
        input.extend_from_slice(&value.to_le_bytes());
    }
    input.extend_from_slice(&[0; 384]);
    let mut observation = Observation {
        packets: Vec::new(),
        reenter: 0,
    };
    let context = (&mut observation as *mut Observation).cast::<c_void>();
    let mut id = [0; 8];
    // SAFETY: buffers and context stay live, callback observes only during the
    // call. This runs in the existing serialized global registry export test.
    unsafe {
        assert_eq!(
            crate::effects::xy_vsfilter_effects_create_v1(id.as_mut_ptr()),
            0
        );
        let id = u64::from_le_bytes(id);
        assert_eq!(
            xy_vsfilter_ass_override_v1(
                0,
                0,
                document_id,
                id,
                input.as_ptr(),
                input.len(),
                context,
                Some(observe),
                None,
                None
            ),
            0
        );
        assert_eq!(observation.packets.len(), 1);
        assert_eq!(&observation.packets[0][..132], &input[..132]);
        assert_eq!(
            xy_vsfilter_ass_override_v1(
                0,
                1,
                document_id,
                id,
                input.as_ptr(),
                input.len(),
                context,
                Some(observe),
                None,
                None
            ),
            4
        );
        assert_eq!(
            xy_vsfilter_ass_override_v1(
                document_id,
                0,
                document_id,
                id,
                input.as_ptr(),
                input.len(),
                context,
                Some(observe),
                None,
                None
            ),
            1
        );
        assert_eq!(
            xy_vsfilter_ass_override_v1(
                0,
                0,
                document_id,
                id,
                ptr::null(),
                0,
                context,
                Some(observe),
                None,
                None
            ),
            3
        );
        assert_eq!(
            xy_vsfilter_ass_override_v1(
                0,
                0,
                document_id,
                id,
                input.as_ptr(),
                usize::MAX,
                context,
                Some(observe),
                None,
                None
            ),
            5
        );
        assert_eq!(observation.packets.len(), 1);
        observation.reenter = id;
        assert_eq!(
            xy_vsfilter_ass_override_v1(
                0,
                0,
                document_id,
                id,
                input.as_ptr(),
                input.len(),
                context,
                Some(observe),
                None,
                None
            ),
            2
        );
        assert_eq!(observation.packets.len(), 2);
        assert_eq!(crate::effects::xy_vsfilter_effects_destroy_v1(id), 0);
        assert_eq!(
            xy_vsfilter_ass_override_v1(
                0,
                0,
                document_id,
                id,
                input.as_ptr(),
                input.len(),
                context,
                Some(observe),
                None,
                None
            ),
            1
        );
        assert_eq!(observation.packets.len(), 2);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn malformed_style_lengths_and_flags_refuse_before_services() {
        let mut input = State::default().encode();
        for value in [192u32, 192, 0] {
            input.extend_from_slice(&value.to_le_bytes());
        }
        input.extend_from_slice(&[0; 384]);
        assert!(decode(&input).is_ok());
        assert_eq!(decode(&input[..143]).err(), Some(3));
        input[140] = 2;
        assert_eq!(decode(&input).err(), Some(3));
        input[140] = 0;
        input[132..136].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(decode(&input).is_err());
    }
}
