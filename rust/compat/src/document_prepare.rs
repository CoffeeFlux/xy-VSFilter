// SPDX-License-Identifier: GPL-2.0-or-later
//! Final supplied CString preparation only. Native FastTrim, initial rejection,
//! WebVTT, charset conversion and append/callback orchestration remain outside.
//! Model of captured MFC CStringW Remove/Replace/TrimLeft source. The installed
//! WCHAR dispatch and one-code-unit CharNext are source-verified on both Windows
//! architectures (docs/rust-document-cstring-helper-evidence.json), alongside
//! bounded native/Rust observations. Allocation failure/timing is not emulated.
use crate::document::PreparedEntry;
use alloc::vec::Vec;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    Add,
    EntryOnly,
    SetStr,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    CountOverflow,
    LengthOverflow,
    UnresolvedTail,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Fields {
    pub text: Vec<u16>,
    pub style: Vec<u16>,
}
const DEFAULT: &[u16] = &[68, 101, 102, 97, 117, 108, 116];
fn bounded(length: usize) -> Result<(), Error> {
    if length > i32::MAX as usize {
        return Err(Error::CountOverflow);
    }
    if length
        .checked_add(1)
        .and_then(|n| n.checked_mul(2))
        .is_none_or(|n| n > isize::MAX as usize)
    {
        return Err(Error::LengthOverflow);
    }
    Ok(())
}
fn prefix(units: &[u16]) -> &[u16] {
    &units[..units.iter().position(|&u| u == 0).unwrap_or(units.len())]
}
/// CString Replace(L"\n",L"\\N") preserves its original end pointer during
/// expansion. Later NUL-separated chunks can therefore be skipped. One skipped
/// LF adds the initialized former terminator to logical content; more than one
/// requires uninitialized allocation tail and is explicitly unresolved.
pub fn replace_lf(input: &[u16]) -> Result<Vec<u16>, Error> {
    bounded(input.len())?;
    let count = input.iter().filter(|&&u| u == 10).count();
    let length = input
        .len()
        .checked_add(count)
        .ok_or(Error::LengthOverflow)?;
    bounded(length)?;
    if count == 0 {
        return Ok(input.to_vec());
    }
    let original_end = input.len();
    let mut buffer = input.to_vec();
    buffer.push(0);
    let mut cursor = 0;
    let mut replaced = 0;
    while cursor < original_end {
        loop {
            let end = cursor + prefix(&buffer[cursor..]).len();
            let Some(relative) = buffer[cursor..end].iter().position(|&u| u == 10) else {
                break;
            };
            let target = cursor + relative;
            buffer.splice(target..target + 1, [92, 78]);
            replaced += 1;
            cursor = target + 2;
        }
        cursor += prefix(&buffer[cursor..]).len() + 1;
    }
    match count - replaced {
        0 => {
            buffer.pop();
        }
        1 => {}
        _ => return Err(Error::UnresolvedTail),
    }
    debug_assert_eq!(buffer.len(), length);
    Ok(buffer)
}
/// Complete final suffix, accepting full raw fields even if text/style becomes
/// empty. SetStr observes LF-only and never normalizes style or Unicode flags.
pub fn prepare(mode: Mode, text: &[u16], style: &[u16]) -> Result<Fields, Error> {
    bounded(text.len())?;
    bounded(style.len())?;
    let cleaned;
    let input = if mode == Mode::SetStr {
        text
    } else {
        cleaned = text
            .iter()
            .copied()
            .filter(|&u| u != 13)
            .collect::<Vec<_>>();
        &cleaned
    };
    let text = replace_lf(input)?;
    let mut style = style.to_vec();
    if mode != Mode::SetStr {
        if style.is_empty() {
            style = DEFAULT.to_vec();
        } else if mode == Mode::EntryOnly || prefix(&style) != DEFAULT {
            let stars = style.iter().take_while(|&&u| u == 42).count();
            style.drain(..stars);
        }
    }
    Ok(Fields { text, style })
}
/// Preserves all other initialized fields. Caller selects existing Add mode and
/// performs append/readorder assignment after this preparation succeeds.
pub fn prepare_add_record(
    mut entry: PreparedEntry,
    entry_only: bool,
) -> Result<PreparedEntry, Error> {
    let fields = prepare(
        if entry_only {
            Mode::EntryOnly
        } else {
            Mode::Add
        },
        &entry.text,
        &entry.style,
    )?;
    entry.text = fields.text;
    entry.style = fields.style;
    Ok(entry)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn raw_suffix_and_empty_results_are_preserved() {
        assert_eq!(
            prepare(Mode::Add, &[13], &[42, 42]).unwrap(),
            Fields {
                text: Vec::new(),
                style: Vec::new()
            }
        );
        assert_eq!(
            prepare(Mode::EntryOnly, &[0xd800, 13, 0, 10, 0xdc00], &[42, 0, 42]).unwrap(),
            Fields {
                text: alloc::vec![0xd800, 0, 92, 78, 0xdc00],
                style: alloc::vec![0, 42]
            }
        );
        let mut raw = DEFAULT.to_vec();
        raw.extend_from_slice(&[0, 42, 0xd800]);
        assert_eq!(prepare(Mode::Add, &[92, 78, 10], &raw).unwrap().style, raw);
        assert_eq!(
            prepare(Mode::SetStr, &[13, 10], &[42]).unwrap(),
            Fields {
                text: alloc::vec![13, 92, 78],
                style: alloc::vec![42]
            }
        );
        assert_eq!(prepare(Mode::Add, &[], &[]).unwrap().style, DEFAULT);
    }
    #[test]
    fn original_end_bound_distinguishes_initialized_terminator_from_unknown_tail() {
        assert_eq!(replace_lf(&[10, 0, 10]), Ok(alloc::vec![92, 78, 0, 10, 0]));
        assert_eq!(replace_lf(&[10, 10, 0, 10, 10]), Err(Error::UnresolvedTail));
        assert_eq!(
            replace_lf(&[10, 0, 10, 10]),
            Ok(alloc::vec![92, 78, 0, 92, 78, 92, 78])
        );
        assert_eq!(
            replace_lf(&[0, 10, 0, 10]),
            Ok(alloc::vec![0, 92, 78, 0, 10, 0])
        );
    }
    #[test]
    fn entry_payload_and_flags_are_untouched() {
        let entry = PreparedEntry {
            text: alloc::vec![10],
            style: alloc::vec![42, 65],
            actor: alloc::vec![0, 0xd800],
            effect: alloc::vec![0xdc00],
            unicode: false,
            margins: [-1, 2, 3, 4],
            layer: -2,
            start: 5,
            end: 6,
            readorder: -9,
        };
        let result = prepare_add_record(entry.clone(), false).unwrap();
        let mut expected = entry;
        expected.text = alloc::vec![92, 78];
        expected.style = alloc::vec![65];
        assert_eq!(result, expected);
        assert_eq!(bounded(i32::MAX as usize + 1), Err(Error::CountOverflow));
    }
}
