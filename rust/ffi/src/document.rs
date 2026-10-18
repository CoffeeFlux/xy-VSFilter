// SPDX-License-Identifier: GPL-2.0-or-later
//! Private v1 document boundary for the opt-in experimental native caller.
//! See include/xy_rust_document.h. All canonical access goes through typed IDs
//! and nonblocking admission. Input bytes are owned before admission, and output
//! bytes are staged until every registry loan ends. No explicit host callbacks are
//! made; allocator/new-handler reentry can observe Busy, and its compatible host
//! handling is unresolved.
use alloc::boxed::Box;
use alloc::vec::Vec;
use core::ptr;
use xy_vsfilter_compat::document::{
    AddMode, ConversionMode, Document, IdentityField, InterpretationField, LayoutField,
    MetadataField, Refusal, ResolvedStyle, RetiredEmpty, ScriptField, StyleRenameError,
    StyleResolutionError,
};
use xy_vsfilter_compat::document_style as style_wire;
use xy_vsfilter_compat::document_wire::{self as wire, Counts, EntryHeader, SegmentHeader};
use xy_vsfilter_compat::timeline_search::Mode;

use crate::document_registry::{Error as RegistryError, Kind, Registry, Resource, DOCUMENTS};

const OK: u32 = 0;
const BAD_HANDLE: u32 = 1;
const BUSY: u32 = 2;
const INVALID_INPUT: u32 = 3;
const OUT_OF_RANGE: u32 = 4;
const COUNT_OVERFLOW: u32 = 5;
const UNRESOLVED_REFERENCE: u32 = 6;

macro_rules! required {
    ($value:expr) => {
        match $value {
            Ok(value) => value,
            Err(status) => return status,
        }
    };
}

fn registry_error(error: RegistryError) -> u32 {
    match error {
        RegistryError::Busy => BUSY,
        RegistryError::InvalidHandle | RegistryError::TypeMismatch => BAD_HANDLE,
        RegistryError::Exhausted => COUNT_OVERFLOW,
    }
}
fn refusal(error: Refusal) -> u32 {
    match error {
        Refusal::StaleEntryIndex | Refusal::WebVttEndIterator | Refusal::UnsuppliedMode => {
            UNRESOLVED_REFERENCE
        }
        Refusal::EntryCountOverflow | Refusal::TimelineCountOverflow => COUNT_OVERFLOW,
        Refusal::EntryIndex | Refusal::SegmentIndex => OUT_OF_RANGE,
        Refusal::InvalidPermutation | Refusal::TimeCountMismatch => INVALID_INPUT,
    }
}
fn wire_error(error: wire::Error) -> u32 {
    match error {
        wire::Error::CountOverflow | wire::Error::LengthOverflow => COUNT_OVERFLOW,
        wire::Error::InvalidShape | wire::Error::InvalidBool | wire::Error::NonzeroReserved => {
            INVALID_INPUT
        }
    }
}
fn style_wire_error(error: style_wire::Error) -> u32 {
    match error {
        style_wire::Error::CountOverflow | style_wire::Error::LengthOverflow => COUNT_OVERFLOW,
        _ => INVALID_INPUT,
    }
}
fn status(result: Result<(), u32>) -> u32 {
    result.err().unwrap_or(OK)
}
fn admitted<T>(
    operation: impl for<'a> FnOnce(&'a mut Registry) -> Result<T, u32>,
) -> Result<T, u32> {
    DOCUMENTS.try_with(operation).map_err(registry_error)?
}
fn mutate(owner: u64, operation: impl FnOnce(&mut Document) -> Result<(), Refusal>) -> u32 {
    status(admitted(|registry| {
        operation(registry.document_mut(owner).map_err(registry_error)?).map_err(refusal)
    }))
}
fn insert(resource: Resource) -> Result<u64, u32> {
    // Do not map the (error, resource) inside admission: a rejected resource must
    // be destroyed after the registry guard has ended.
    match DOCUMENTS.try_with(|registry| registry.insert(resource)) {
        Ok(Ok(id)) => Ok(id),
        Ok(Err((error, resource))) => {
            drop(resource);
            Err(registry_error(error))
        }
        Err(error) => Err(registry_error(error)),
    }
}

/// # Safety
/// Output receives one immutable snapshot ID in eight writable bytes. Snapshot
/// contains raw UTF-16LE section text, without a packet header.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_format_export_section_v1(
    format: u32,
    section: u32,
    width: i32,
    height: i32,
    collisions: i32,
    scaled: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    if scaled > 1 {
        return INVALID_INPUT;
    }
    let text = required!(xy_vsfilter_compat::document_export::format_section(
        format,
        section,
        width,
        height,
        collisions,
        scaled != 0
    )
    .map_err(|error| match error {
        xy_vsfilter_compat::document_export::Error::InvalidFormat
        | xy_vsfilter_compat::document_export::Error::InvalidSection => INVALID_INPUT,
        xy_vsfilter_compat::document_export::Error::CountOverflow => COUNT_OVERFLOW,
    }));
    let bytes = required!(raw_units_bytes(&text));
    let id = required!(insert(Resource::Snapshot(bytes)));
    // SAFETY: output preflighted; no registry reference remains.
    unsafe {
        write_output(output, &id.to_le_bytes());
    }
    OK
}

/// # Safety
/// Input is one exact Entry64 wire with natively prepared text. Output receives
/// one immutable snapshot ID; its bytes are raw UTF-16LE without a header.
/// Input may overlap output and is fully copied before any output writes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_format_export_row_v1(
    format: u32,
    sequence: i32,
    start_ms: i32,
    end_ms: i32,
    frame_start: i32,
    frame_end: i32,
    margin_v: i32,
    input: *const u8,
    length: usize,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    if format >= 14 {
        return INVALID_INPUT;
    }
    // SAFETY: caller supplies the complete readable input wire.
    let input = required!(unsafe { copy_input(input, length) });
    let entry = required!(wire::decode_entry(&input).map_err(wire_error));
    let numbers = xy_vsfilter_compat::document_export::Numbers {
        sequence,
        start_ms,
        end_ms,
        frame_start,
        frame_end,
        margin_v,
    };
    let text = required!(
        xy_vsfilter_compat::document_export::format_row(format, numbers, &entry).map_err(|error| {
            match error {
                xy_vsfilter_compat::document_export::Error::InvalidFormat
                | xy_vsfilter_compat::document_export::Error::InvalidSection => INVALID_INPUT,
                xy_vsfilter_compat::document_export::Error::CountOverflow => COUNT_OVERFLOW,
            }
        })
    );
    // All core formatting and encoding occurs without registry admission.
    let bytes = required!(raw_units_bytes(&text));
    let id = required!(insert(Resource::Snapshot(bytes)));
    // SAFETY: preflighted output; staged input and registry loans have ended.
    unsafe {
        write_output(output, &id.to_le_bytes());
    }
    OK
}

fn translated_search_bytes(state: xy_vsfilter_compat::document_search::State) -> [u8; 24] {
    let mut bytes = [0u8; 24];
    bytes[..4].copy_from_slice(&u32::from(state.pending).to_le_bytes());
    bytes[4..8].copy_from_slice(&u32::from(state.start).to_le_bytes());
    bytes[8..12].copy_from_slice(&state.request_index.to_le_bytes());
    bytes[12..16].copy_from_slice(&state.result_index.to_le_bytes());
    bytes[16..20].copy_from_slice(&u32::from(state.found).to_le_bytes());
    bytes[20..].copy_from_slice(&state.count.to_le_bytes());
    bytes
}
/// # Safety
/// Output32 receives run ID:u64 followed by six LE state words. Native supplies
/// each requested translated timestamp only after this registry call returns.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_search_begin_v1(
    owner: u64,
    mode: u32,
    time: i64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 32));
    let mode = match mode {
        0 => Mode::Entry,
        1 => Mode::NextSegment,
        2 => Mode::ContainingSegment,
        _ => return INVALID_INPUT,
    };
    let (result, state) = required!(admitted(|registry| {
        let run = xy_vsfilter_compat::document_search::Run::new(
            registry.document(owner).map_err(registry_error)?,
            mode,
            time,
        )
        .map_err(refusal)?;
        let state = run.state();
        Ok((registry.insert(Resource::SearchRun { owner, run }), state))
    }));
    let id = match result {
        Ok(id) => id,
        Err((error, resource)) => {
            drop(resource);
            return registry_error(error);
        }
    };
    let mut bytes = [0u8; 32];
    bytes[..8].copy_from_slice(&id.to_le_bytes());
    bytes[8..].copy_from_slice(&translated_search_bytes(state));
    // SAFETY: output validated before insertion; registry admission has ended.
    unsafe {
        write_output(output, &bytes);
    }
    OK
}
/// # Safety
/// Output24 receives pending,start,requestIndex,resultIndex,found,count as six
/// LE words. Counts stay frozen; the bound owner must still exist.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_search_supply_v1(
    id: u64,
    value: i64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 24));
    let state = required!(admitted(|registry| {
        let (_, run) = registry.search_pair(id).map_err(registry_error)?;
        run.supply(value).map_err(|_| INVALID_INPUT)
    }));
    // SAFETY: checks precede mutation; no registry reference escapes.
    unsafe {
        write_output(output, &translated_search_bytes(state));
    }
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_search_drop_v1(id: u64) -> u32 {
    status(
        DOCUMENTS
            .destroy(id, Kind::SearchRun)
            .map_err(registry_error),
    )
}

/// # Safety
/// Output receives a document-bound WebVTT session ID in eight writable bytes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_vtt_begin_v1(
    owner: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let id = required!(admitted(|registry| {
        registry.document(owner).map_err(registry_error)?;
        let id = registry.reserve_insert().map_err(registry_error)?;
        registry.insert_reserved(
            id,
            Resource::VttRun {
                owner,
                run: xy_vsfilter_compat::vtt_document::Run::new(),
            },
        );
        Ok(id)
    }));
    // SAFETY: preflighted output; registry borrowing has ended.
    unsafe { write_output(output, &id.to_le_bytes()) };
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_vtt_drop_v1(id: u64) -> u32 {
    status(DOCUMENTS.destroy(id, Kind::VttRun).map_err(registry_error))
}
/// # Safety
/// Initialized raw UTF16LE and one strict-bool whitespace flag per unit. Flags
/// preserve CString FastTrimRight without Unicode scalar conversion.
/// EOF is !ReadString. Both spans are copied before registry admission.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_vtt_read_v1(
    id: u64,
    eof: u32,
    input: *const u8,
    length: usize,
    spaces: *const u8,
    space_length: usize,
) -> u32 {
    if eof > 1 || length % 2 != 0 || space_length != length / 2 {
        return INVALID_INPUT;
    }
    // SAFETY: initialized bounded caller buffer, copied before borrowing state.
    let text = units(&required!(unsafe { copy_input(input, length) }));
    // SAFETY: same caller-buffer contract for predicate flags.
    let spaces = required!(unsafe { copy_input(spaces, space_length) });
    status(admitted(|registry| {
        let (doc, run) = registry.vtt_pair(id).map_err(registry_error)?;
        run.read(doc, &text, &spaces, eof != 0).map_err(ass_error)
    }))
}
/// # Safety
/// Command0 supplies header scanf count and a three-string packet only for
/// count2/3 (otherwise empty). Command1 supplies converted raw UTF16LE with
/// value0. Command2 supplies a key/foreground/background packet with value0.
/// Three-string packets are three LE u32 unit counts followed by raw spans.
/// All input is copied and decoded before registry admission.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_vtt_packet_v1(
    id: u64,
    command: u32,
    value: i32,
    input: *const u8,
    length: usize,
) -> u32 {
    if command > 2
        || (command != 0 && value != 0)
        || length % 2 != 0
        || (command == 0 && !(-1..=3).contains(&value))
    {
        return INVALID_INPUT;
    }
    // SAFETY: caller supplies initialized readable bytes; no native loan retained.
    let bytes = required!(unsafe { copy_input(input, length) });
    let parts = if command == 2 || (command == 0 && value >= 2) {
        required!(xy_vsfilter_compat::vtt_document::decode_three(&bytes).map_err(ass_error))
    } else {
        if command == 0 && !bytes.is_empty() {
            return INVALID_INPUT;
        }
        Default::default()
    };
    let text = if command == 1 {
        units(&bytes)
    } else {
        Vec::new()
    };
    status(admitted(|registry| {
        let (_, run) = registry.vtt_pair(id).map_err(registry_error)?;
        match command {
            0 => run.header(value, parts),
            1 => run.converted(&text),
            2 => run.color(parts),
            _ => unreachable!(),
        }
        .map_err(ass_error)
    }))
}
/// Initialized CRT time values: count5 uses first three and a zero fourth;
/// count7 uses all four; every other count requires four zeros.
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_vtt_time_v1(
    id: u64,
    count: i32,
    h: i32,
    m: i32,
    s: i32,
    ms: i32,
) -> u32 {
    status(admitted(|registry| {
        registry
            .vtt_pair(id)
            .map_err(registry_error)?
            .1
            .time(count, [h, m, s, ms])
            .map_err(ass_error)
    }))
}
/// Command0 acknowledges style extraction; command1 acknowledges completed Add.
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_vtt_step_v1(id: u64, command: u32) -> u32 {
    status(admitted(|registry| {
        registry
            .vtt_pair(id)
            .map_err(registry_error)?
            .1
            .acknowledge(command)
            .map_err(ass_error)
    }))
}
/// # Safety
/// Output receives an independent snapshot ID in eight writable bytes. Group0:
/// op/accepted:u32[2],start/end:i64,raw UTF16LE payload. Group1: raw cue tags.
/// Group2: map count:u32 then key/foreground/background three-string packets.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_vtt_part_v1(
    id: u64,
    group: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let bytes = required!(admitted(|registry| registry
        .vtt_pair(id)
        .map_err(registry_error)?
        .1
        .snapshot(group)
        .map_err(ass_error)));
    let snapshot = required!(insert(Resource::Snapshot(bytes)));
    // SAFETY: preflighted writable output; snapshot owns all bytes independently.
    unsafe { write_output(output, &snapshot.to_le_bytes()) };
    OK
}

/// # Safety
/// Output receives a document-bound SAMI session ID in eight writable bytes.
/// Position is the native file position captured before format discovery.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_sami_begin_v1(
    owner: u64,
    position: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let id = required!(admitted(|registry| {
        registry.document(owner).map_err(registry_error)?;
        let id = registry.reserve_insert().map_err(registry_error)?;
        registry.insert_reserved(
            id,
            Resource::SamiRun {
                owner,
                run: xy_vsfilter_compat::sami_document::Run::new(position),
            },
        );
        Ok(id)
    }));
    // SAFETY: preflighted output; registry borrowing has ended.
    unsafe { write_output(output, &id.to_le_bytes()) };
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_sami_drop_v1(id: u64) -> u32 {
    status(DOCUMENTS.destroy(id, Kind::SamiRun).map_err(registry_error))
}
/// # Safety
/// Initialized raw UTF16LE and one strict-bool whitespace flag per unit. Flags
/// are used only for successful post-rewind reads; other phases supply zeros.
/// EOF is !ReadString. Both spans are copied before registry admission.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_sami_read_v1(
    id: u64,
    eof: u32,
    input: *const u8,
    length: usize,
    spaces: *const u8,
    space_length: usize,
) -> u32 {
    if eof > 1 || length % 2 != 0 || space_length != length / 2 {
        return INVALID_INPUT;
    }
    // SAFETY: initialized bounded caller buffer, copied before borrowing state.
    let text = units(&required!(unsafe { copy_input(input, length) }));
    // SAFETY: same caller-buffer contract for predicate flags.
    let spaces = required!(unsafe { copy_input(spaces, space_length) });
    status(admitted(|registry| {
        registry
            .sami_pair(id)
            .map_err(registry_error)?
            .1
            .read(&text, &spaces, eof != 0)
            .map_err(ass_error)
    }))
}
/// # Safety
/// Initialized uppercase UTF16LE copy of the requested line, with unchanged
/// counted length. Input is copied before admission; no native loan survives.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_sami_upper_v1(
    id: u64,
    input: *const u8,
    length: usize,
) -> u32 {
    if length % 2 != 0 {
        return INVALID_INPUT;
    }
    // SAFETY: initialized bounded caller buffer, copied before admission.
    let text = units(&required!(unsafe { copy_input(input, length) }));
    status(admitted(|registry| {
        registry
            .sami_pair(id)
            .map_err(registry_error)?
            .1
            .uppercase(&text)
            .map_err(ass_error)
    }))
}
/// Commands:0 rewind ack(value0),1 strict-bool iswdigit reply,2 Add ack(value0).
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_sami_step_v1(id: u64, command: u32, value: u32) -> u32 {
    if command > 2 || (command == 1 && value > 1) || (command != 1 && value != 0) {
        return INVALID_INPUT;
    }
    status(admitted(|registry| {
        let (_, run) = registry.sami_pair(id).map_err(registry_error)?;
        match command {
            0 => run.rewind(),
            1 => run.digit(value != 0),
            2 => run.acknowledge(),
            _ => unreachable!(),
        }
        .map_err(ass_error)
    }))
}
/// # Safety
/// Output receives independent snapshot ID in eight writable bytes. Snapshot:
/// op/accepted/digit-unit/reserved:u32[4], rewind-position:u64,start/end:i64,
/// raw UTF16LE. No native or run loan escapes this call.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_sami_part_v1(
    id: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let bytes = required!(admitted(|registry| Ok(registry
        .sami_pair(id)
        .map_err(registry_error)?
        .1
        .snapshot())));
    let snapshot = required!(insert(Resource::Snapshot(bytes)));
    // SAFETY: preflighted writable output; immutable snapshot owns its bytes.
    unsafe { write_output(output, &snapshot.to_le_bytes()) };
    OK
}

/// # Safety
/// Output receives a document-bound MicroDVD run ID in eight writable bytes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_microdvd_begin_v1(
    owner: u64,
    charset: i32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let id = required!(admitted(|registry| {
        registry.document(owner).map_err(registry_error)?;
        let id = registry.reserve_insert().map_err(registry_error)?;
        registry.insert_reserved(
            id,
            Resource::MicroDvdRun {
                owner,
                run: xy_vsfilter_compat::microdvd_document::Run::new(charset),
            },
        );
        Ok(id)
    }));
    // SAFETY: preflighted writable output; no registry loan remains.
    unsafe { write_output(output, &id.to_le_bytes()) };
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_microdvd_drop_v1(id: u64) -> u32 {
    status(
        DOCUMENTS
            .destroy(id, Kind::MicroDvdRun)
            .map_err(registry_error),
    )
}
/// # Safety
/// Raw initialized UTF16LE plus one strict-bool whitespace flag per code unit.
/// Both spans are copied before admission. EOF is !ReadString.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_microdvd_read_v1(
    id: u64,
    eof: u32,
    input: *const u8,
    length: usize,
    spaces: *const u8,
    space_length: usize,
) -> u32 {
    if eof > 1 || length % 2 != 0 || space_length != length / 2 {
        return INVALID_INPUT;
    }
    // SAFETY: initialized bounded caller spans, copied before any registry loan.
    let text = units(&required!(unsafe { copy_input(input, length) }));
    // SAFETY: same caller-span contract for the whitespace predicates.
    let spaces = required!(unsafe { copy_input(spaces, space_length) });
    status(admitted(|registry| {
        let (doc, run) = registry.microdvd_pair(id).map_err(registry_error)?;
        run.read(doc, &text, &spaces, eof != 0).map_err(ass_error)
    }))
}
/// Commands0/1 supply primary/fallback scanf count and initialized values.
/// Fallback count is BEFORE legacy +1; only count1 supplies first. Command2
/// acknowledges Add and requires zero scalars. No partial native locals cross.
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_microdvd_step_v1(
    id: u64,
    command: u32,
    value: i32,
    first: i32,
    last: i32,
) -> u32 {
    if command > 2
        || (command == 1 && last != 0)
        || (command == 2 && (value != 0 || first != 0 || last != 0))
    {
        return INVALID_INPUT;
    }
    status(admitted(|registry| {
        let (doc, run) = registry.microdvd_pair(id).map_err(registry_error)?;
        match command {
            0 => run.primary(doc, value, first, last),
            1 => run.fallback(doc, value, first),
            2 => run.acknowledge(),
            _ => unreachable!(),
        }
        .map_err(ass_error)
    }))
}
/// # Safety
/// Initialized raw UTF16LE normalized style name, copied before admission.
/// Success acknowledges completed native AddStyle; charset is captured before
/// AddStyle can destroy the native style adapter. Failure requires empty name
/// and zero charset. No native style pointer crosses or is retained.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_microdvd_style_v1(
    id: u64,
    success: u32,
    charset: i32,
    input: *const u8,
    length: usize,
) -> u32 {
    if success > 1 || length % 2 != 0 || (success == 0 && (length != 0 || charset != 0)) {
        return INVALID_INPUT;
    }
    // SAFETY: initialized bounded caller span; copied before mutation/admission.
    let name = units(&required!(unsafe { copy_input(input, length) }));
    status(admitted(|registry| {
        let (_, run) = registry.microdvd_pair(id).map_err(registry_error)?;
        run.style(success != 0, &name, charset).map_err(ass_error)
    }))
}
/// # Safety
/// Output receives immutable snapshot ID in eight writable bytes. Group0 is
/// op/accepted/charset/reserved:u32[4],start/end:i64,raw UTF16LE. Group1 is raw
/// UTF16LE candidate style name during Style, current name otherwise.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_microdvd_part_v1(
    id: u64,
    group: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let bytes = required!(admitted(|registry| registry
        .microdvd_pair(id)
        .map_err(registry_error)?
        .1
        .snapshot(group)
        .map_err(ass_error)));
    let snapshot = required!(insert(Resource::Snapshot(bytes)));
    // SAFETY: preflighted output; independent snapshot owns all result bytes.
    unsafe { write_output(output, &snapshot.to_le_bytes()) };
    OK
}

/// # Safety
/// Output receives a document-bound text parser run ID in eight writable bytes.
/// Formats:0 SubRip,1 MPL2,2 OldSubRip,3 VPlayer. No constructor mutation.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_text_parser_begin_v1(
    owner: u64,
    format: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let format = match format {
        0 => xy_vsfilter_compat::text_document::Format::SubRip,
        1 => xy_vsfilter_compat::text_document::Format::Mpl2,
        2 => xy_vsfilter_compat::text_document::Format::OldSubRip,
        3 => xy_vsfilter_compat::text_document::Format::VPlayer,
        _ => return INVALID_INPUT,
    };
    let id = required!(admitted(|registry| {
        registry.document(owner).map_err(registry_error)?;
        let id = registry.reserve_insert().map_err(registry_error)?;
        registry.insert_reserved(
            id,
            Resource::TextRun {
                owner,
                run: xy_vsfilter_compat::text_document::Run::new(format),
            },
        );
        Ok(id)
    }));
    // SAFETY: validated output; no registry borrow remains.
    unsafe { write_output(output, &id.to_le_bytes()) };
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_text_parser_drop_v1(id: u64) -> u32 {
    status(DOCUMENTS.destroy(id, Kind::TextRun).map_err(registry_error))
}
/// # Safety
/// Initialized raw UTF16LE plus one strict-bool CString whitespace flag per unit.
/// Both native spans are copied before admission; EOF is !ReadString result.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_text_parser_read_v1(
    id: u64,
    eof: u32,
    input: *const u8,
    length: usize,
    spaces: *const u8,
    space_length: usize,
) -> u32 {
    if eof > 1 || length % 2 != 0 || space_length != length / 2 {
        return INVALID_INPUT;
    }
    // SAFETY: bounded initialized caller buffers, copied before registry loans.
    let text = units(&required!(unsafe { copy_input(input, length) }));
    // SAFETY: same caller-buffer contract for predicate bytes.
    let spaces = required!(unsafe { copy_input(spaces, space_length) });
    status(admitted(|registry| {
        let (doc, run) = registry.text_pair(id).map_err(registry_error)?;
        run.read(doc, &text, &spaces, eof != 0).map_err(ass_error)
    }))
}
/// # Safety
/// Command0 supplies scanf count plus exactly eight initialized i32 LE values;
/// SubRip uses all8 when count>=11, MPL2 first2 at count2, OldSubRip first6 at
/// count6, VPlayer first3 at count3. Other counts ignore the initialized values.
/// Command1 supplies strict-bool numeric-only caption result with empty input;
/// command2 acknowledges Add completion with value0 and empty input. Never copy
/// uninitialized native scan locals for a partial conversion. Command3 supplies
/// charset-aware FindChar's index (-1 for absent), with empty input.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_text_parser_step_v1(
    id: u64,
    command: u32,
    value: i32,
    input: *const u8,
    length: usize,
) -> u32 {
    if command > 3
        || (command == 0 && length != 32)
        || (command != 0 && length != 0)
        || (command == 1 && !(0..=1).contains(&value))
        || (command == 2 && value != 0)
    {
        return INVALID_INPUT;
    }
    // SAFETY: initialized byte packet is copied before registry admission.
    let bytes = required!(unsafe { copy_input(input, length) });
    let mut values = [0; 8];
    for (chunk, number) in bytes.chunks_exact(4).zip(&mut values) {
        *number = i32::from_le_bytes(chunk.try_into().unwrap());
    }
    status(admitted(|registry| {
        let (_, run) = registry.text_pair(id).map_err(registry_error)?;
        match command {
            0 => run.header(value, values),
            1 => run.caption(value != 0),
            2 => run.acknowledge(),
            3 => run.found_pipe(value),
            _ => unreachable!(),
        }
        .map_err(ass_error)
    }))
}
/// # Safety
/// Output receives immutable snapshot ID in eight writable bytes. Snapshot:
/// op:u32(ReadHeader0/ScanHeader1/ReadCaption2/ScanCaption3/Emit4/Done5/Failed6/FindPipe7),
/// accepted:bool32,start:i64,end:i64, then raw UTF16LE. No native/run loan escapes.
/// FindPipe alone prefixes its UTF16LE payload with a u32 search position.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_text_parser_part_v1(
    id: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let bytes = required!(admitted(|registry| Ok(registry
        .text_pair(id)
        .map_err(registry_error)?
        .1
        .snapshot())));
    let snapshot = required!(insert(Resource::Snapshot(bytes)));
    // SAFETY: validated output; independent snapshot survives owner/run release.
    unsafe { write_output(output, &snapshot.to_le_bytes()) };
    OK
}

/// # Safety
/// Output receives a validated, document-bound run ID in eight writable bytes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_xombie_begin_v1(
    owner: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let id = required!(admitted(|registry| {
        registry.document(owner).map_err(registry_error)?;
        let id = registry.reserve_insert().map_err(registry_error)?;
        registry.insert_reserved(
            id,
            Resource::XombieRun {
                owner,
                run: Default::default(),
            },
        );
        Ok(id)
    }));
    // SAFETY: preflighted output, no registry loan remains.
    unsafe { write_output(output, &id.to_le_bytes()) };
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_xombie_drop_v1(id: u64) -> u32 {
    status(
        DOCUMENTS
            .destroy(id, Kind::XombieRun)
            .map_err(registry_error),
    )
}
/// # Safety
/// Raw UTF16LE and one strict-bool whitespace byte per unit are initialized
/// immutable spans. Both are copied before admission; no native loan escapes.
/// EOF is !ReadString result, including its discarded nonempty final fragment.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_xombie_read_v1(
    id: u64,
    eof: u32,
    input: *const u8,
    length: usize,
    spaces: *const u8,
    space_length: usize,
) -> u32 {
    if eof > 1 || length % 2 != 0 || length / 2 != space_length {
        return INVALID_INPUT;
    }
    // SAFETY: bounded initialized caller buffers are copied before admission.
    let bytes = required!(unsafe { copy_input(input, length) });
    // SAFETY: same caller-buffer contract for supplied native predicates.
    let spaces = required!(unsafe { copy_input(spaces, space_length) });
    let text = units(&bytes);
    status(admitted(|registry| {
        let (doc, run) = registry.xombie_pair(id).map_err(registry_error)?;
        run.read(doc, &text, &spaces, eof != 0).map_err(ass_error)
    }))
}
/// # Safety
/// Commands0 classify,1 text,2 i32 bits,3 f64 bits,4 acknowledge,5 numeric failure.
/// Text and remainder are copied raw UTF16LE before admission. Commands0/1 use
/// zero bits, commands2/3 empty text, commands4/5 empty spans and zero bits.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_xombie_step_v1(
    id: u64,
    command: u32,
    bits: u64,
    input: *const u8,
    length: usize,
    remainder: *const u8,
    remainder_length: usize,
) -> u32 {
    if command > 5
        || length % 2 != 0
        || remainder_length % 2 != 0
        || (matches!(command, 0 | 1 | 4 | 5) && bits != 0)
        || (command >= 2 && length != 0)
        || (matches!(command, 0 | 4 | 5) && remainder_length != 0)
        || (command == 2 && bits > u32::MAX as u64)
    {
        return INVALID_INPUT;
    }
    // SAFETY: bounded initialized text spans, copied before registry loans.
    let text = units(&required!(unsafe { copy_input(input, length) }));
    // SAFETY: disjoint ownership of copied remainder even if caller spans overlap.
    let rest = units(&required!(unsafe {
        copy_input(remainder, remainder_length)
    }));
    status(admitted(|registry| {
        let (doc, run) = registry.xombie_pair(id).map_err(registry_error)?;
        match command {
            0 => run.classify(&text),
            1..=3 => run.supply(doc, command + 1, bits, &text, &rest),
            4 => run.acknowledge(),
            5 => run.failed(doc),
            _ => unreachable!(),
        }
        .map_err(ass_error)
    }))
}
/// # Safety
/// Output receives an immutable snapshot ID in eight writable bytes. Groups:
/// 0 op/sep/kind/accepted:u32[4] then raw UTF16LE,1 style wire,2 raw style name,
/// 3 entry wire (Unicode supplied later by native file),4 decoded font bytes.
/// Only group0 is available in every phase; snapshots survive owner/run release.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_xombie_part_v1(
    id: u64,
    group: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let bytes = required!(admitted(|registry| registry
        .xombie_pair(id)
        .map_err(registry_error)?
        .1
        .part(group)
        .map_err(ass_error)));
    let snapshot = required!(insert(Resource::Snapshot(bytes)));
    // SAFETY: preflighted output; independently owned snapshot has no run loan.
    unsafe { write_output(output, &snapshot.to_le_bytes()) };
    OK
}

/// # Safety
/// Output is an eight-byte writable run ID. Capacity is checked before the
/// initial BT601/TV document mutation; the run binds a validated document ID.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_ass_begin_v1(
    owner: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let id = required!(admitted(|registry| {
        registry.document(owner).map_err(registry_error)?;
        let id = registry.reserve_insert().map_err(registry_error)?;
        let run = xy_vsfilter_compat::ass_document::Run::begin(
            registry.document_mut(owner).map_err(registry_error)?,
        );
        registry.insert_reserved(id, Resource::AssRun { owner, run });
        Ok(id)
    }));
    // SAFETY: output validated before mutation; registry loans have ended.
    unsafe { write_output(output, &id.to_le_bytes()) };
    OK
}

/// # Safety
/// Input is a readable UTF-16LE byte span, copied before admission. Output is
/// 16 writable bytes (kind/version/style-version/accepted, all LE i32), may
/// overlap input, and is unchanged on error. Neither buffer is retained.
/// Commands: 0 classify lowered key, 1 successful GetInt, 2 failed GetInt,
/// 3 detected script version, 4 extracted/lowered text value, 5 begin dialogue,
/// 6 dialogue numeric reply, 7 commit dialogue with trimmed text and flags
/// (bit0 Unicode, bit1 native Default match). Other text commands use value 0.
/// Command8 acknowledges the current input record (0 failure,1 success).
/// Commands9/10/11 begin fonts, acknowledge installation, end fonts (no value/text).
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_ass_step_v1(
    id: u64,
    command: u32,
    value: i32,
    input: *const u8,
    length: usize,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 16));
    if command > 11
        || (matches!(command, 0 | 4 | 5) && value != 0)
        || (matches!(command, 1..=3 | 6 | 8) && length != 0)
        || (command == 2 && value != 0)
        || (command == 7 && !(0..=3).contains(&value))
        || (command == 8 && !(0..=1).contains(&value))
        || (command >= 9 && (value != 0 || length != 0))
        || length % 2 != 0
    {
        return INVALID_INPUT;
    }
    // SAFETY: bounded native input is copied before any document/session loan.
    let bytes = required!(unsafe { copy_input(input, length) });
    let text: Vec<u16> = bytes
        .chunks_exact(2)
        .map(|b| u16::from_le_bytes([b[0], b[1]]))
        .collect();
    let state = required!(admitted(|registry| {
        let (document, run) = registry.ass_pair(id).map_err(registry_error)?;
        match command {
            0 => run.classify(&text),
            1 => run.number(document, Some(value)),
            2 => run.number(document, None),
            3 => run.script_version(value),
            4 => run.text(document, &text),
            5 => run.begin_dialogue(&text),
            6 => run.dialogue_number(value),
            7 => run.append_dialogue(document, &text, value & 1 != 0, value & 2 != 0),
            8 => run.end_record(value != 0),
            9 => run.begin_fonts(),
            10 => run.font_installed(),
            11 => run.end_fonts(),
            _ => unreachable!(),
        }
        .map_err(ass_error)?;
        Ok(run.state())
    }));
    let mut bytes = [0; 16];
    for (chunk, value) in bytes.chunks_exact_mut(4).zip(state) {
        chunk.copy_from_slice(&value.to_le_bytes());
    }
    // SAFETY: capacity checked before mutations; no registry borrow escapes.
    unsafe { write_output(output, &bytes) };
    OK
}

#[no_mangle]
pub extern "C" fn xy_vsfilter_document_ass_drop_v1(id: u64) -> u32 {
    status(DOCUMENTS.destroy(id, Kind::AssRun).map_err(registry_error))
}

fn ass_error(error: xy_vsfilter_compat::ass_document::Error) -> u32 {
    use xy_vsfilter_compat::ass_document::Error;
    match error {
        Error::Protocol => INVALID_INPUT,
        Error::UnsuppliedScreen => UNRESOLVED_REFERENCE,
        Error::Storage(error) => refusal(error),
        Error::CountOverflow => COUNT_OVERFLOW,
        Error::Preparation(crate_error) => match crate_error {
            xy_vsfilter_compat::document_prepare::Error::UnresolvedTail => UNRESOLVED_REFERENCE,
            _ => COUNT_OVERFLOW,
        },
    }
}

/// # Safety
/// Input is raw UTF-16LE; spaces supplies one initialized strict-bool byte per
/// unit from CString traits. Both spans are copied before admission and may
/// overlap output. Output is 16 writable bytes with the existing ASS state.
/// EOF is the negation of ReadString's result; a false read discards its text.
/// Requires an active font session awaiting a read.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_ass_font_read_v1(
    id: u64,
    eof: u32,
    input: *const u8,
    length: usize,
    spaces: *const u8,
    space_length: usize,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 16));
    if eof > 1 || length % 2 != 0 || space_length != length / 2 {
        return INVALID_INPUT;
    }
    // SAFETY: initialized bounded spans; ownership precedes registry admission.
    let bytes = required!(unsafe { copy_input(input, length) });
    // SAFETY: same caller-buffer contract; neither loan is retained.
    let spaces = required!(unsafe { copy_input(spaces, space_length) });
    let text = units(&bytes);
    let state = required!(admitted(|registry| {
        let (_, run) = registry.ass_pair(id).map_err(registry_error)?;
        run.read_font(&text, &spaces, eof != 0).map_err(ass_error)?;
        Ok(run.state())
    }));
    let mut bytes = [0; 16];
    for (chunk, value) in bytes.chunks_exact_mut(4).zip(state) {
        chunk.copy_from_slice(&value.to_le_bytes());
    }
    // SAFETY: output preflighted before any mutation; all registry loans ended.
    unsafe { write_output(output, &bytes) };
    OK
}

/// # Safety
/// Input is raw UTF-16LE; spaces supplies one initialized strict-bool byte per
/// unit from CString traits. Both spans are copied before admission and may
/// overlap output. Output is 16 writable bytes with the existing ASS state.
/// EOF is the negation of ReadString's result, not an inferred file position.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_ass_read_v1(
    id: u64,
    eof: u32,
    input: *const u8,
    length: usize,
    spaces: *const u8,
    space_length: usize,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 16));
    if eof > 1 || length % 2 != 0 || space_length != length / 2 {
        return INVALID_INPUT;
    }
    // SAFETY: initialized bounded spans; ownership precedes registry admission.
    let bytes = required!(unsafe { copy_input(input, length) });
    // SAFETY: same caller-buffer contract; neither loan is retained.
    let spaces = required!(unsafe { copy_input(spaces, space_length) });
    let text = units(&bytes);
    let state = required!(admitted(|registry| {
        let (_, run) = registry.ass_pair(id).map_err(registry_error)?;
        run.read_line(&text, &spaces, eof != 0).map_err(ass_error)?;
        Ok(run.state())
    }));
    let mut bytes = [0; 16];
    for (chunk, value) in bytes.chunks_exact_mut(4).zip(state) {
        chunk.copy_from_slice(&value.to_le_bytes());
    }
    // SAFETY: output preflighted before any mutation; all registry loans ended.
    unsafe { write_output(output, &bytes) };
    OK
}

/// # Safety
/// Output receives an immutable snapshot ID in eight writable bytes. Snapshot
/// header is action:u32(Read=0,Install=1,Done=2),reserved:u32=0, followed by
/// decoded font bytes during Install. No borrowed native or registry state.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_ass_font_part_v1(
    id: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let bytes = required!(admitted(|registry| registry
        .ass_pair(id)
        .map_err(registry_error)?
        .1
        .font_part()
        .map_err(ass_error)));
    let snapshot = required!(insert(Resource::Snapshot(bytes)));
    // SAFETY: validated output and independently owned snapshot ID.
    unsafe { write_output(output, &snapshot.to_le_bytes()) };
    OK
}

/// # Safety
/// Output receives an immutable snapshot ID in eight writable bytes. Snapshot
/// header is action:u32 (Read=0, LowerKey=1, Record=2, Done=3), reserved:u32=0,
/// followed by UTF-16LE. Group0 returns key only during LowerKey; group1 returns
/// payload only during Record. Snapshots retain no run/document/native loan.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_ass_input_part_v1(
    id: u64,
    group: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let bytes = required!(admitted(|registry| registry
        .ass_pair(id)
        .map_err(registry_error)?
        .1
        .input_part(group)
        .map_err(ass_error)));
    let snapshot = required!(insert(Resource::Snapshot(bytes)));
    // SAFETY: validated output and independently owned snapshot ID.
    unsafe { write_output(output, &snapshot.to_le_bytes()) };
    OK
}

/// # Safety
/// Output receives one immutable snapshot ID in eight writable bytes. Snapshot
/// starts with request:u32 and separator:u32, then raw UTF-16LE. Group 0 is the
/// next numeric token, group 1 the stripped style, group 4 the untrimmed text.
/// No document or session borrow escapes; snapshots survive run destruction.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_ass_dialogue_part_v1(
    id: u64,
    group: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let bytes = required!(admitted(|registry| {
        registry
            .ass_pair(id)
            .map_err(registry_error)?
            .1
            .dialogue_part(group)
            .map_err(ass_error)
    }));
    let snapshot = required!(insert(Resource::Snapshot(bytes)));
    // SAFETY: validated output, and all native/session loans have ended.
    unsafe { write_output(output, &snapshot.to_le_bytes()) };
    OK
}

/// # Safety
/// Raw text/remainder are bounded UTF-16LE byte spans copied before admission;
/// output receives 16 state bytes and may overlap either input. Commands:
/// 0 begin(text=input),1 integer bits,2 double bits,3 string field(text=value),
/// 4 release completed style. Commands1..3 supply the native remaining buffer.
/// No native object address is accepted as the run ID or retained in Rust.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_ass_style_v1(
    id: u64,
    command: u32,
    bits: u64,
    text: *const u8,
    length: usize,
    remainder: *const u8,
    remainder_length: usize,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 16));
    if command > 4
        || length % 2 != 0
        || remainder_length % 2 != 0
        || (matches!(command, 0 | 3 | 4) && bits != 0)
        || (matches!(command, 1 | 2 | 4) && length != 0)
        || (matches!(command, 0 | 4) && remainder_length != 0)
    {
        return INVALID_INPUT;
    }
    // SAFETY: initialized bounded spans copied before taking any registry loan.
    let text = required!(unsafe { copy_input(text, length) });
    // SAFETY: same caller-buffer contract; no native reference escapes.
    let remainder = required!(unsafe { copy_input(remainder, remainder_length) });
    let text = units(&text);
    let remainder = units(&remainder);
    let state = required!(admitted(|registry| {
        let (_, run) = registry.ass_pair(id).map_err(registry_error)?;
        match command {
            0 => run.begin_style(&text),
            1..=3 => run.style_value(command, bits, &text, &remainder),
            4 => run.end_style(),
            _ => unreachable!(),
        }
        .map_err(ass_error)?;
        Ok(run.state())
    }));
    let mut bytes = [0; 16];
    for (chunk, value) in bytes.chunks_exact_mut(4).zip(state) {
        chunk.copy_from_slice(&value.to_le_bytes());
    }
    // SAFETY: output preflighted before mutation; registry loans ended.
    unsafe { write_output(output, &bytes) };
    OK
}
/// # Safety
/// Output receives an immutable snapshot ID in eight writable bytes. Groups:
/// 0 next field:u32,reserved:u32=0,raw remainder;1 same header and raw name;
/// 2 the existing complete style wire. Groups1/2 require finished conversion.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_ass_style_part_v1(
    id: u64,
    group: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let bytes = required!(admitted(|registry| registry
        .ass_pair(id)
        .map_err(registry_error)?
        .1
        .style_part(group)
        .map_err(ass_error)));
    let snapshot = required!(insert(Resource::Snapshot(bytes)));
    // SAFETY: independent snapshot ID; no registry reference escapes.
    unsafe { write_output(output, &snapshot.to_le_bytes()) };
    OK
}

fn open_error(error: xy_vsfilter_compat::document_open::Error) -> u32 {
    use xy_vsfilter_compat::document_open::Error;
    match error {
        Error::MissingParserResult | Error::UnexpectedParserResult | Error::Finished => {
            INVALID_INPUT
        }
        Error::UnsuppliedScreen => UNRESOLVED_REFERENCE,
    }
}
/// # Safety
/// Output receives a typed run ID in eight writable bytes. No owner mutation.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_open_lifecycle_begin_v1(
    owner: u64,
    parser_count: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    if parser_count > i32::MAX as u32 {
        return COUNT_OVERFLOW;
    }
    let resource = Resource::OpenRun {
        owner,
        run: xy_vsfilter_compat::document_open::Run::new(parser_count),
    };
    let result = DOCUMENTS.try_with(|registry| {
        if let Err(error) = registry.document(owner) {
            return Err((error, resource));
        }
        registry.insert(resource)
    });
    let id = match result {
        Ok(Ok(id)) => id,
        Ok(Err((error, resource))) => {
            drop(resource);
            return registry_error(error);
        }
        Err(error) => return registry_error(error),
    };
    // SAFETY: validated output, registry admission has ended.
    unsafe {
        write_output(output, &id.to_le_bytes());
    }
    OK
}
/// # Safety
/// Output receives action:u32,index:u32 in eight writable bytes. Ack -2 peeks,
/// -1 acknowledges a non-parse action, 0/1 supplies a parser result.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_open_lifecycle_step_v1(
    id: u64,
    ack: i32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    if !(-2..=1).contains(&ack) {
        return INVALID_INPUT;
    }
    let action = required!(admitted(|registry| {
        let (document, run) = registry.open_pair(id).map_err(registry_error)?;
        if ack == -2 {
            Ok(run.action())
        } else {
            run.advance(document, if ack == -1 { None } else { Some(ack != 0) })
                .map_err(open_error)
        }
    }));
    let mut bytes = [0u8; 8];
    bytes[..4].copy_from_slice(&(action.kind as u32).to_le_bytes());
    bytes[4..].copy_from_slice(&action.index.to_le_bytes());
    // SAFETY: all fallible output checks precede advance; no reference escapes.
    unsafe {
        write_output(output, &bytes);
    }
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_open_lifecycle_drop_v1(id: u64) -> u32 {
    status(DOCUMENTS.destroy(id, Kind::OpenRun).map_err(registry_error))
}

fn style_copy_error(error: xy_vsfilter_compat::document_style_copy::ErrorKind) -> u32 {
    use xy_vsfilter_compat::document_style_copy::ErrorKind as Error;
    match error {
        Error::PendingCandidate | Error::NoPendingCandidate | Error::InvalidPreparedValue => {
            INVALID_INPUT
        }
        Error::UnresolvedNull => UNRESOLVED_REFERENCE,
        Error::CountOverflow => COUNT_OVERFLOW,
    }
}
/// # Safety
/// Output receives a typed run ID in eight writable bytes. Opening validates
/// source and destination, but does not clear or read their named style maps.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_copy_styles_open_v1(
    destination: u64,
    source: u64,
    append: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    if append > 1 {
        return INVALID_INPUT;
    }
    let resource = Resource::StyleCopyRun {
        destination,
        source,
        run: xy_vsfilter_compat::document_style_copy::Run::new(append != 0),
    };
    let result = DOCUMENTS.try_with(|registry| {
        if let Err(error) = registry
            .document(destination)
            .and_then(|_| registry.document(source))
        {
            return Err((error, resource));
        }
        registry.insert(resource)
    });
    let id = match result {
        Ok(Ok(id)) => id,
        Ok(Err((error, resource))) => {
            drop(resource);
            return registry_error(error);
        }
        Err(error) => return registry_error(error),
    };
    // SAFETY: output preflighted and admission ended.
    unsafe {
        write_output(output, &id.to_le_bytes());
    }
    OK
}
/// # Safety
/// Output receives snapshot ID in eight bytes. Packet16 is action:u32
/// (0 Done,1 Advanced,2 NeedSuffix),reserved-zero:u32,units:u64,raw UTF-16LE name.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_copy_styles_step_v1(
    id: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let result = required!(admitted(|registry| {
        let length = {
            let (destination, source, run) =
                registry.style_copy_pair(id).map_err(registry_error)?;
            let units = if !run.initialized() {
                0
            } else {
                source
                    .unwrap_or(destination)
                    .named_styles()
                    .iter()
                    .nth(run.next_index())
                    .map_or(0, |(name, _)| name.len().max(7))
            };
            if units > i32::MAX as usize {
                return Err(COUNT_OVERFLOW);
            }
            units
                .checked_mul(2)
                .and_then(|n| n.checked_add(16))
                .filter(|n| *n <= isize::MAX as usize)
                .ok_or(COUNT_OVERFLOW)?
        };
        let reply = registry.reserve_insert().map_err(registry_error)?;
        let mut bytes = Vec::with_capacity(length);
        let (destination, source, run) = registry.style_copy_pair(id).map_err(registry_error)?;
        let result = match run.step(destination, source) {
            Ok(result) => result,
            Err(error) => return Ok(Err(error)),
        };
        use xy_vsfilter_compat::document_style_copy::Step;
        let (action, name) = match result.event {
            Step::Done => (0u32, Vec::new()),
            Step::Advanced => (1u32, Vec::new()),
            Step::NeedSuffix(name) => (2u32, name),
        };
        bytes.extend_from_slice(&action.to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());
        bytes.extend_from_slice(&(name.len() as u64).to_le_bytes());
        for unit in &name {
            bytes.extend_from_slice(&unit.to_le_bytes());
        }
        registry.insert_reserved(reply, Resource::Snapshot(bytes));
        Ok(Ok((reply, (result.retired, name))))
    }));
    let (reply, retired) = match result {
        Ok(result) => result,
        Err(error) => {
            let status = style_copy_error(error.kind);
            drop(error.retired);
            return status;
        }
    };
    drop(retired);
    // SAFETY: output validated before mutation; no registry reference remains.
    unsafe {
        write_output(output, &reply.to_le_bytes());
    }
    OK
}
/// # Safety
/// Candidate is raw UTF-16LE and output receives one applied u64LE boolean.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_copy_styles_submit_v1(
    id: u64,
    candidate: *const u8,
    length: usize,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    required!(element_shape(length, 2));
    // SAFETY: caller supplies readable input, copied before admission/output.
    let candidate = required!(unsafe { copy_input(candidate, length) });
    let candidate = units(&candidate);
    let result = required!(admitted(|registry| {
        let (destination, _, run) = registry.style_copy_pair(id).map_err(registry_error)?;
        Ok(run.submit(destination, &candidate))
    }));
    let result = match result {
        Ok(result) => result,
        Err(error) => {
            let status = style_copy_error(error.kind);
            drop(error.retired);
            return status;
        }
    };
    let applied = u64::from(result.applied);
    drop(result.retired);
    // SAFETY: output preflighted and registry references ended.
    unsafe {
        write_output(output, &applied.to_le_bytes());
    }
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_copy_styles_drop_v1(id: u64) -> u32 {
    status(
        DOCUMENTS
            .destroy(id, Kind::StyleCopyRun)
            .map_err(registry_error),
    )
}

fn style_add_error(error: xy_vsfilter_compat::document_style_add::Error) -> u32 {
    use xy_vsfilter_compat::document_style_add::Error;
    match error {
        Error::InvalidPreparedValue | Error::Finished => INVALID_INPUT,
        Error::UnresolvedNull => UNRESOLVED_REFERENCE,
        Error::CountOverflow => COUNT_OVERFLOW,
    }
}
/// # Safety
/// Name and nullable style follow the existing raw byte-buffer contract. Output
/// receives snapshot ID and run ID in16bytes; packet24 holds action:u32,reserved:u32,run:u64,units:u64.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_add_style_begin_v1(
    owner: u64,
    name: *const u8,
    name_length: usize,
    style: *const u8,
    style_length: usize,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 16));
    required!(element_shape(name_length, 2));
    // SAFETY: both complete inputs are copied before admission/output writes.
    let name = required!(unsafe { copy_input(name, name_length) });
    let style = required!(unsafe { copy_input(style, style_length) });
    let name = units(&name);
    let mut incoming = Some(if style.is_empty() {
        None
    } else {
        Some(required!(
            style_wire::decode(&style).map_err(style_wire_error)
        ))
    });
    let packet_length = required!(name
        .len()
        .max(7)
        .checked_mul(2)
        .and_then(|n| n.checked_add(24))
        .filter(|n| *n <= isize::MAX as usize)
        .ok_or(COUNT_OVERFLOW));
    let (reply, run_id, retired) = required!(admitted(|registry| {
        registry.document(owner).map_err(registry_error)?;
        let (reply, run_id) = registry.reserve_two().map_err(registry_error)?;
        let mut bytes = Vec::with_capacity(packet_length);
        let result = xy_vsfilter_compat::document_style_add::begin(
            registry.document_mut(owner).map_err(registry_error)?,
            &name,
            &mut incoming,
        )
        .map_err(style_add_error)?;
        use xy_vsfilter_compat::document_style_add::Action;
        let (action, session) = match result.action {
            Action::Done => (0u32, None),
            Action::Conflict(run) => (1u32, Some(run)),
        };
        let session_id = if session.is_some() { run_id } else { 0 };
        bytes.extend_from_slice(&action.to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());
        bytes.extend_from_slice(&session_id.to_le_bytes());
        bytes.extend_from_slice(&(result.normalized_name.len() as u64).to_le_bytes());
        for unit in &result.normalized_name {
            bytes.extend_from_slice(&unit.to_le_bytes());
        }
        registry.insert_reserved(reply, Resource::Snapshot(bytes));
        if let Some(run) = session {
            registry.insert_reserved(run_id, Resource::StyleAddRun { owner, run });
        }
        Ok((reply, session_id, (result.retired, result.normalized_name)))
    }));
    drop(retired);
    drop(incoming);
    // SAFETY: capacity preflighted and no registry reference remains.
    let mut handles = [0u8; 16];
    handles[..8].copy_from_slice(&reply.to_le_bytes());
    handles[8..].copy_from_slice(&run_id.to_le_bytes());
    unsafe {
        write_output(output, &handles);
    }
    OK
}
/// # Safety
/// Candidate is raw UTF-16LE; output receives one u64LE applied boolean.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_add_style_submit_v1(
    id: u64,
    candidate: *const u8,
    length: usize,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    required!(element_shape(length, 2));
    // SAFETY: complete input copied before admission and overlapping output.
    let candidate = required!(unsafe { copy_input(candidate, length) });
    let candidate = units(&candidate);
    let result = required!(admitted(|registry| {
        let (document, run) = registry.style_add_pair(id).map_err(registry_error)?;
        run.submit(document, &candidate).map_err(style_add_error)
    }));
    let applied = u64::from(result.applied);
    drop(result.retired);
    // SAFETY: validated output, registry admission ended.
    unsafe {
        write_output(output, &applied.to_le_bytes());
    }
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_add_style_drop_v1(id: u64) -> u32 {
    status(
        DOCUMENTS
            .destroy(id, Kind::StyleAddRun)
            .map_err(registry_error),
    )
}

fn unknown_error(error: xy_vsfilter_compat::document_unknown::Error) -> u32 {
    use xy_vsfilter_compat::document_unknown::Error;
    match error {
        Error::PendingResponse | Error::NoPendingWarning => INVALID_INPUT,
        Error::UnresolvedPendingRow => UNRESOLVED_REFERENCE,
    }
}

/// # Safety
/// Output is a writable byte buffer of at least eight bytes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_unknown_open_v1(
    owner: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let run = xy_vsfilter_compat::document_unknown::Run::new();
    let resource = Resource::UnknownRun { owner, run };
    // Return rejected resource out of admission for destruction.
    let result = DOCUMENTS.try_with(|registry| {
        if let Err(error) = registry.document(owner) {
            return Err((error, resource));
        }
        registry.insert(resource)
    });
    let id = match result {
        Ok(Ok(id)) => id,
        Ok(Err((error, resource))) => {
            drop(resource);
            return registry_error(error);
        }
        Err(error) => return registry_error(error),
    };
    // SAFETY: output validated before insertion; admission has ended.
    unsafe {
        write_output(output, &id.to_le_bytes());
    }
    OK
}

/// # Safety
/// Output receives a snapshot ID in eight writable bytes. Packet is kind:u32,
/// reserved-zero:u32, unit-count:u64, then full raw UTF-16LE warning units.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_unknown_step_v1(
    id: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let (reply, retired) = required!(admitted(|registry| {
        // Validate owner and calculate the maximum packet before any Run mutation.
        let size = {
            let (document, run) = registry.unknown_pair(id).map_err(registry_error)?;
            let count = document
                .entries()
                .get(run.next_index())
                .map_or(0, |entry| entry.style.len());
            if count > i32::MAX as usize {
                return Err(COUNT_OVERFLOW);
            }
            count
                .checked_mul(2)
                .and_then(|n| n.checked_add(16))
                .filter(|n| *n <= isize::MAX as usize)
                .ok_or(COUNT_OVERFLOW)?
        };
        let reserved = registry.reserve_insert().map_err(registry_error)?;
        let mut bytes = Vec::with_capacity(size);
        let (document, run) = registry.unknown_pair(id).map_err(registry_error)?;
        let outcome = run.step(document).map_err(unknown_error)?;
        use xy_vsfilter_compat::document_unknown::Step;
        let (kind, name) = match outcome.event {
            Step::Finished => (0u32, Vec::new()),
            Step::Advanced => (1u32, Vec::new()),
            Step::Warning(name) => (2u32, name),
        };
        bytes.extend_from_slice(&kind.to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());
        bytes.extend_from_slice(&(name.len() as u64).to_le_bytes());
        for unit in &name {
            bytes.extend_from_slice(&unit.to_le_bytes());
        }
        registry.insert_reserved(reserved, Resource::Snapshot(bytes));
        Ok((reserved, (outcome.retired, name)))
    }));
    drop(retired);
    // SAFETY: preflighted output; no registry reference remains.
    unsafe {
        write_output(output, &reply.to_le_bytes());
    }
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_unknown_resume_v1(id: u64, accepted: u32) -> u32 {
    if accepted > 1 {
        return INVALID_INPUT;
    }
    let retired = required!(admitted(|registry| {
        let (document, run) = registry.unknown_pair(id).map_err(registry_error)?;
        run.resume(document, accepted != 0).map_err(unknown_error)
    }));
    drop(retired);
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_unknown_drop_v1(id: u64) -> u32 {
    status(
        DOCUMENTS
            .destroy(id, Kind::UnknownRun)
            .map_err(registry_error),
    )
}

// These four helpers are the only raw-buffer boundary. Address/length checks
// cannot prove accessibility or lifetime; those remain the caller's contract.
fn byte_range(pointer: *const u8, length: usize) -> Result<(), u32> {
    if length > isize::MAX as usize
        || (length != 0 && pointer.is_null())
        || (pointer as usize).checked_add(length).is_none()
    {
        Err(INVALID_INPUT)
    } else {
        Ok(())
    }
}
unsafe fn copy_input(pointer: *const u8, length: usize) -> Result<Vec<u8>, u32> {
    byte_range(pointer, length)?;
    if length == 0 {
        return Ok(Vec::new());
    }
    // SAFETY: numeric bounds were checked; caller supplies initialized readable
    // bytes for this entire range. No reference escapes this immediate copy.
    Ok(unsafe { core::slice::from_raw_parts(pointer, length) }.to_vec())
}
fn output_capacity(pointer: *mut u8, capacity: usize, required: usize) -> Result<(), u32> {
    byte_range(pointer.cast_const(), capacity)?;
    if capacity < required {
        Err(INVALID_INPUT)
    } else {
        Ok(())
    }
}
unsafe fn write_output(pointer: *mut u8, bytes: &[u8]) {
    if !bytes.is_empty() {
        // SAFETY: capacity was validated before mutation. Caller owns the live
        // exclusive destination, which cannot alias staged Rust-owned bytes.
        unsafe { ptr::copy_nonoverlapping(bytes.as_ptr(), pointer, bytes.len()) };
    }
}
fn element_shape(length: usize, width: usize) -> Result<(), u32> {
    if length % width != 0 {
        Err(INVALID_INPUT)
    } else if length / width > i32::MAX as usize {
        Err(COUNT_OVERFLOW)
    } else {
        Ok(())
    }
}
fn units(bytes: &[u8]) -> Vec<u16> {
    bytes
        .chunks_exact(2)
        .map(|pair| u16::from_le_bytes([pair[0], pair[1]]))
        .collect()
}

/// # Safety
/// Output follows the byte-buffer contract in xy_rust_document.h, capacity >=8.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_create_v1(output: *mut u8, capacity: usize) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let id = required!(insert(Resource::Document(Box::default())));
    // SAFETY: caller contract and capacity were validated before insertion.
    unsafe { write_output(output, &id.to_le_bytes()) };
    OK
}

#[no_mangle]
pub extern "C" fn xy_vsfilter_document_copy_storage_v1(destination: u64, source: u64) -> u32 {
    let retired = required!(admitted(|registry| {
        registry
            .copy_document(destination, source)
            .map_err(registry_error)
    }));
    drop(retired); // All replaced storage is destroyed after admission.
    OK
}
/// # Safety
/// Input follows the byte-buffer contract; copied before registry admission.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_set_default_style_v1(
    owner: u64,
    input: *const u8,
    length: usize,
) -> u32 {
    // SAFETY: caller supplies the documented live input byte region.
    let input = required!(unsafe { copy_input(input, length) });
    let mut value = Some(required!(
        style_wire::decode(&input).map_err(style_wire_error)
    ));
    // Borrow pending ownership so rejected admission/handles also drop outside.
    let retired = required!(admitted(|registry| {
        let document = registry.document_mut(owner).map_err(registry_error)?;
        Ok(document.replace_default_style(value.take()))
    }));
    drop(retired);
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_copy_selected_state_v1(
    destination: u64,
    source: u64,
) -> u32 {
    let retired = required!(admitted(|registry| registry
        .copy_selected_state(destination, source)
        .map_err(registry_error)));
    drop(retired);
    OK
}
/// Coherent base Empty only; virtual callbacks remain in the native caller.
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_empty_v1(owner: u64) -> u32 {
    let retired = required!(admitted(|registry| Ok(registry
        .document_mut(owner)
        .map_err(registry_error)?
        .empty())));
    let RetiredEmpty {
        styles,
        segments,
        entries,
    } = retired;
    // Explicit payload destruction order, entirely after registry admission.
    drop(styles);
    drop(segments);
    drop(entries);
    OK
}
/// # Safety
/// Packet follows the byte-buffer contract and exact v1 style-rename encoding.
/// Native classification/CRT naming are complete; incoming Put remains native.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_commit_style_rename_v1(
    owner: u64,
    input: *const u8,
    length: usize,
) -> u32 {
    // SAFETY: caller supplies the documented live packet region.
    let input = required!(unsafe { copy_input(input, length) });
    let mut prepared = required!(wire::decode_style_rename(&input).map_err(wire_error));
    let retired = required!(admitted(|registry| {
        registry
            .document_mut(owner)
            .map_err(registry_error)?
            .commit_style_rename(&prepared.old, &prepared.new, &mut prepared.previous)
            .map_err(|error| match error {
                StyleRenameError::CountOverflow => COUNT_OVERFLOW,
                StyleRenameError::InvalidPreparedValue => INVALID_INPUT,
            })
    }));
    drop(retired);
    OK
}
fn metadata_field(field: u32) -> Result<MetadataField, u32> {
    match field {
        0 => Ok(MetadataField::PARType),
        1 => Ok(MetadataField::ColorMatrix),
        2 => Ok(MetadataField::ColorRange),
        _ => Err(INVALID_INPUT),
    }
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_set_metadata_v1(owner: u64, field: u32, value: i32) -> u32 {
    if field > 5 || (matches!(field, 3 | 5) && value != 0 && value != 1) {
        return INVALID_INPUT;
    }
    status(admitted(|registry| {
        let document = registry.document_mut(owner).map_err(registry_error)?;
        if field == 5 {
            document.replace_simple(value != 0);
        } else if field == 4 {
            document.replace_lcid(value as u32);
        } else if field == 3 {
            document.replace_forced_default(value != 0);
        } else {
            document.replace_metadata(metadata_field(field)?, value);
        }
        Ok(())
    }))
}
/// # Safety
/// Output follows the byte-buffer contract, capacity >=4; LE i32 or raw LCID u32.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_get_metadata_v1(
    owner: u64,
    field: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 4));
    if field > 5 {
        return INVALID_INPUT;
    }
    let value = required!(admitted(|registry| {
        let document = registry.document(owner).map_err(registry_error)?;
        if field == 5 {
            document.simple().map(i32::from)
        } else if field == 4 {
            document.lcid().map(|bits| bits as i32)
        } else if field == 3 {
            document.forced_default().map(i32::from)
        } else {
            document.metadata(metadata_field(field)?)
        }
        .ok_or(UNRESOLVED_REFERENCE)
    }));
    // SAFETY: output validated above and registry loan has ended.
    unsafe { write_output(output, &value.to_le_bytes()) };
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_set_par_bits_v1(owner: u64, bits: u64) -> u32 {
    status(admitted(|registry| {
        registry
            .document_mut(owner)
            .map_err(registry_error)?
            .replace_par_bits(bits);
        Ok(())
    }))
}
/// # Safety
/// Output follows the byte-buffer contract, capacity >=8; opaque LE u64 bits.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_get_par_bits_v1(
    owner: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let bits = required!(admitted(|registry| registry
        .document(owner)
        .map_err(registry_error)?
        .par_bits()
        .ok_or(UNRESOLVED_REFERENCE)));
    // SAFETY: output validated above and registry loan has ended.
    unsafe { write_output(output, &bits.to_le_bytes()) };
    OK
}
fn script_field(field: u32) -> Result<ScriptField, u32> {
    match field {
        0 => Ok(ScriptField::Width),
        1 => Ok(ScriptField::Height),
        2 => Ok(ScriptField::Wrap),
        3 => Ok(ScriptField::Collisions),
        _ => Err(INVALID_INPUT),
    }
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_set_script_field_v1(
    owner: u64,
    field: u32,
    value: i32,
) -> u32 {
    if field > 6 || (field == 4 && value != 0 && value != 1) {
        return INVALID_INPUT;
    }
    status(admitted(|registry| {
        let document = registry.document_mut(owner).map_err(registry_error)?;
        if field == 4 {
            document.replace_scaled_bas(value != 0);
        } else if field >= 5 {
            document.replace_layout_field(
                if field == 5 {
                    LayoutField::Width
                } else {
                    LayoutField::Height
                },
                value,
            );
        } else {
            document.replace_script_field(script_field(field)?, value);
        }
        Ok(())
    }))
}
/// # Safety
/// Output follows the byte-buffer contract, capacity >=4; LE signed i32.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_get_script_field_v1(
    owner: u64,
    field: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 4));
    if field > 6 {
        return INVALID_INPUT;
    }
    let value = required!(admitted(|registry| {
        let document = registry.document(owner).map_err(registry_error)?;
        if field == 4 {
            document.scaled_bas().map(i32::from)
        } else if field >= 5 {
            document.layout_field(if field == 5 {
                LayoutField::Width
            } else {
                LayoutField::Height
            })
        } else {
            document.script_field(script_field(field)?)
        }
        .ok_or(UNRESOLVED_REFERENCE)
    }));
    // SAFETY: output validated before admission; loan has ended.
    unsafe { write_output(output, &value.to_le_bytes()) };
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_set_script_size_v1(
    owner: u64,
    width: i32,
    height: i32,
) -> u32 {
    status(admitted(|registry| {
        registry
            .document_mut(owner)
            .map_err(registry_error)?
            .set_script_size(width, height);
        Ok(())
    }))
}
/// # Safety
/// Output follows the byte-buffer contract, capacity >=8; LE i32 width/height.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_get_script_size_v1(
    owner: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let size = required!(admitted(|registry| registry
        .document(owner)
        .map_err(registry_error)?
        .script_size()
        .ok_or(UNRESOLVED_REFERENCE)));
    let mut bytes = [0u8; 8];
    bytes[..4].copy_from_slice(&size[0].to_le_bytes());
    bytes[4..].copy_from_slice(&size[1].to_le_bytes());
    // SAFETY: capacity validated above; no registry loan remains.
    unsafe { write_output(output, &bytes) };
    OK
}
fn interpretation_field(field: u32) -> Result<InterpretationField, u32> {
    match field {
        0 => Ok(InterpretationField::Mode),
        1 => Ok(InterpretationField::SubtitleType),
        2 => Ok(InterpretationField::Encoding),
        _ => Err(INVALID_INPUT),
    }
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_set_interpretation_v1(
    owner: u64,
    field: u32,
    value: i32,
) -> u32 {
    let field = required!(interpretation_field(field));
    status(admitted(|registry| {
        registry
            .document_mut(owner)
            .map_err(registry_error)?
            .replace_interpretation(field, value);
        Ok(())
    }))
}
/// # Safety
/// Output follows the byte-buffer contract, capacity >=4; LE signed i32.
/// Absent constructor state is unresolved and leaves the output untouched.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_get_interpretation_v1(
    owner: u64,
    field: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 4));
    let field = required!(interpretation_field(field));
    let value = required!(admitted(|registry| registry
        .document(owner)
        .map_err(registry_error)?
        .interpretation(field)
        .ok_or(UNRESOLVED_REFERENCE)));
    // SAFETY: output was validated above and the registry loan has ended.
    unsafe { write_output(output, &value.to_le_bytes()) };
    OK
}
/// # Safety
/// Input follows the byte-buffer contract and is raw UTF16LE. Field 0 name,
/// 1 path. No normalization or implicit copying of the other identity field.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_set_identity_v1(
    owner: u64,
    field: u32,
    input: *const u8,
    length: usize,
) -> u32 {
    let field = match field {
        0 => IdentityField::Name,
        1 => IdentityField::Path,
        _ => return INVALID_INPUT,
    };
    if length % 2 != 0 {
        return INVALID_INPUT;
    }
    if length / 2 > i32::MAX as usize {
        return COUNT_OVERFLOW;
    }
    // SAFETY: caller supplies the documented live byte region.
    let input = required!(unsafe { copy_input(input, length) });
    let mut value = Some(units(&input));
    let retired = required!(admitted(|registry| {
        let document = registry.document_mut(owner).map_err(registry_error)?;
        Ok(document.replace_identity(field, value.take().ok_or(INVALID_INPUT)?))
    }));
    drop(retired);
    OK
}
fn raw_units_bytes(units: &[u16]) -> Result<Vec<u8>, u32> {
    if units.len() > i32::MAX as usize {
        return Err(COUNT_OVERFLOW);
    }
    let length = units
        .len()
        .checked_mul(2)
        .filter(|&n| n <= isize::MAX as usize)
        .ok_or(COUNT_OVERFLOW)?;
    let mut bytes = Vec::with_capacity(length);
    for unit in units {
        bytes.extend_from_slice(&unit.to_le_bytes());
    }
    Ok(bytes)
}
/// # Safety
/// Output follows the byte-buffer contract, capacity >=4.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_named_style_count_v1(
    owner: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 4));
    let count = required!(admitted(|registry| {
        let count = registry
            .document(owner)
            .map_err(registry_error)?
            .named_styles()
            .count();
        if count > i32::MAX as usize {
            return Err(COUNT_OVERFLOW);
        }
        Ok(count as u32)
    }));
    // SAFETY: output validated above; registry loan has ended.
    unsafe { write_output(output, &count.to_le_bytes()) };
    OK
}
/// # Safety
/// Inputs follow the byte-buffer contract. Key is raw UTF16LE; empty style wire
/// denotes a present null value. Inputs are copied/decoded before admission.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_named_style_put_v1(
    owner: u64,
    key: *const u8,
    key_length: usize,
    style: *const u8,
    style_length: usize,
) -> u32 {
    if key_length % 2 != 0 {
        return INVALID_INPUT;
    }
    if key_length / 2 > i32::MAX as usize {
        return COUNT_OVERFLOW;
    }
    // SAFETY: caller supplies the documented live input regions.
    let key = required!(unsafe { copy_input(key, key_length) });
    // SAFETY: same input contract, independent of key overlap.
    let style = required!(unsafe { copy_input(style, style_length) });
    let mut key = Some(units(&key));
    let mut value = Some(if style.is_empty() {
        None
    } else {
        Some(required!(
            style_wire::decode(&style).map_err(style_wire_error)
        ))
    });
    let retired = required!(admitted(|registry| {
        let map = registry
            .document_mut(owner)
            .map_err(registry_error)?
            .named_styles_mut();
        let key_ref = key.as_ref().ok_or(INVALID_INPUT)?;
        if let Some(old) = map.get_mut(key_ref) {
            return Ok(Some(core::mem::replace(
                old,
                value.take().ok_or(INVALID_INPUT)?,
            )));
        }
        if map.count() >= i32::MAX as usize {
            return Err(COUNT_OVERFLOW);
        }
        Ok(map.insert(
            key.take().ok_or(INVALID_INPUT)?,
            value.take().ok_or(INVALID_INPUT)?,
        ))
    }));
    drop(retired);
    OK
}
/// # Safety
/// Key follows the byte-buffer contract and is raw UTF16LE. Missing is a no-op.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_named_style_remove_v1(
    owner: u64,
    key: *const u8,
    length: usize,
) -> u32 {
    if length % 2 != 0 {
        return INVALID_INPUT;
    }
    if length / 2 > i32::MAX as usize {
        return COUNT_OVERFLOW;
    }
    // SAFETY: caller supplies the documented live input region.
    let key = required!(unsafe { copy_input(key, length) });
    let key = units(&key);
    let retired = required!(admitted(|registry| Ok(registry
        .document_mut(owner)
        .map_err(registry_error)?
        .named_styles_mut()
        .remove(&key))));
    drop(retired);
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_named_style_clear_v1(owner: u64) -> u32 {
    let retired = required!(admitted(|registry| Ok(registry
        .document_mut(owner)
        .map_err(registry_error)?
        .named_styles_mut()
        .clear())));
    drop(retired);
    OK
}
/// # Safety
/// Key follows the byte-buffer contract; output capacity >=16. Overlap allowed.
/// Snapshot request is exactly 0 or 1. Output remains untouched on all errors.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_named_style_lookup_v1(
    owner: u64,
    key: *const u8,
    length: usize,
    snapshot_requested: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 16));
    if snapshot_requested > 1 || length % 2 != 0 {
        return INVALID_INPUT;
    }
    if length / 2 > i32::MAX as usize {
        return COUNT_OVERFLOW;
    }
    // SAFETY: caller supplies the documented live key region.
    let key = required!(unsafe { copy_input(key, length) });
    let key = units(&key);
    let (state, bytes) = required!(admitted(|registry| {
        let map = registry
            .document(owner)
            .map_err(registry_error)?
            .named_styles();
        match map.get(&key) {
            None => Ok((0u32, None)),
            Some(None) => Ok((1u32, None)),
            Some(Some(value)) => Ok((
                2u32,
                if snapshot_requested != 0 {
                    Some(style_wire::encode(value).map_err(style_wire_error)?)
                } else {
                    None
                },
            )),
        }
    }));
    let id = match bytes {
        Some(bytes) => required!(insert(Resource::Snapshot(bytes))),
        None => 0,
    };
    let mut reply = [0u8; 16];
    reply[..4].copy_from_slice(&state.to_le_bytes());
    reply[8..].copy_from_slice(&id.to_le_bytes());
    // SAFETY: validated before admission/snapshot insertion; no live loans now.
    unsafe { write_output(output, &reply) };
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_destroy_v1(owner: u64) -> u32 {
    status(
        DOCUMENTS
            .destroy(owner, Kind::Document)
            .map_err(registry_error),
    )
}

/// # Safety
/// Output follows the byte-buffer contract, capacity >=COUNTS_BYTES.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_counts_v1(
    owner: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, wire::COUNTS_BYTES));
    let bytes = required!(admitted(|registry| {
        let document = registry.document(owner).map_err(registry_error)?;
        let counts = Counts {
            entries: u32::try_from(document.entries().len()).map_err(|_| COUNT_OVERFLOW)?,
            segments: u32::try_from(document.segments().len()).map_err(|_| COUNT_OVERFLOW)?,
        };
        wire::encode_counts(&counts).map_err(wire_error)
    }));
    // SAFETY: validated caller destination; admission and canonical loans ended.
    unsafe { write_output(output, &bytes) };
    OK
}

/// # Safety
/// Output follows the byte-buffer contract, capacity >=ENTRY_HEADER_BYTES.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_entry_header_v1(
    owner: u64,
    index: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, wire::ENTRY_HEADER_BYTES));
    let bytes = required!(admitted(|registry| {
        let entry = registry
            .document(owner)
            .map_err(registry_error)?
            .entries()
            .get(index as usize)
            .ok_or(OUT_OF_RANGE)?;
        let header = EntryHeader::from_entry(entry).map_err(wire_error)?;
        wire::encode_entry_header(&header).map_err(wire_error)
    }));
    // SAFETY: caller destination validated; only a staged value remains.
    unsafe { write_output(output, &bytes) };
    OK
}

/// # Safety
/// Output follows the byte-buffer contract, capacity >=SEGMENT_HEADER_BYTES.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_segment_header_v1(
    owner: u64,
    index: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(
        output,
        capacity,
        wire::SEGMENT_HEADER_BYTES
    ));
    let bytes = required!(admitted(|registry| {
        let segment = registry
            .document(owner)
            .map_err(registry_error)?
            .segments()
            .get(index as usize)
            .ok_or(OUT_OF_RANGE)?;
        let header = SegmentHeader::from_segment(segment).map_err(wire_error)?;
        wire::encode_segment_header(&header).map_err(wire_error)
    }));
    // SAFETY: caller destination validated; only a staged value remains.
    unsafe { write_output(output, &bytes) };
    OK
}

/// # Safety
/// Output follows the byte-buffer contract, capacity >=QUERY_BYTES.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_query_time_v1(
    owner: u64,
    mode: u32,
    time: i64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, wire::QUERY_BYTES));
    let mode = match mode {
        0 => Mode::Entry,
        1 => Mode::NextSegment,
        2 => Mode::ContainingSegment,
        _ => return INVALID_INPUT,
    };
    let bytes = required!(admitted(|registry| {
        let query = registry
            .document(owner)
            .map_err(registry_error)?
            .query_time(mode, time)
            .map_err(refusal)?;
        wire::encode_query(&query).map_err(wire_error)
    }));
    // SAFETY: validated destination, and query admission has ended.
    unsafe { write_output(output, &bytes) };
    OK
}

/// Supplies both layout axes in a single admission; no normalization.
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_set_layout_size_v1(
    owner: u64,
    width: i32,
    height: i32,
) -> u32 {
    status(admitted(|registry| {
        registry
            .document_mut(owner)
            .map_err(registry_error)?
            .set_layout_size(width, height);
        Ok(())
    }))
}
/// # Safety
/// Output follows the byte-buffer contract, capacity >=8.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_snapshot_open_v1(
    owner: u64,
    kind: u32,
    index: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    if kind > 11 {
        return INVALID_INPUT;
    }
    let bytes = required!(admitted(|registry| {
        let document = registry.document(owner).map_err(registry_error)?;
        match kind {
            0 => wire::encode_entry(document.entries().get(index as usize).ok_or(OUT_OF_RANGE)?)
                .map_err(wire_error),
            1 => wire::encode_segment(
                document
                    .segments()
                    .get(index as usize)
                    .ok_or(OUT_OF_RANGE)?,
            )
            .map_err(wire_error),
            2 => {
                if index != 0 {
                    return Err(OUT_OF_RANGE);
                }
                style_wire::encode(document.default_style().ok_or(OUT_OF_RANGE)?)
                    .map_err(style_wire_error)
            }
            3 | 4 => {
                let (key, value) = document
                    .named_styles()
                    .iter()
                    .nth(index as usize)
                    .ok_or(OUT_OF_RANGE)?;
                if kind == 3 {
                    raw_units_bytes(key)
                } else {
                    style_wire::encode(value.as_ref().ok_or(UNRESOLVED_REFERENCE)?)
                        .map_err(style_wire_error)
                }
            }
            5 | 6 => {
                if index != 0 {
                    return Err(OUT_OF_RANGE);
                }
                raw_units_bytes(document.identity(if kind == 5 {
                    IdentityField::Name
                } else {
                    IdentityField::Path
                }))
            }
            7 => {
                if index != 0 {
                    return Err(OUT_OF_RANGE);
                }
                wire::encode_sort_snapshot(document.entries()).map_err(wire_error)
            }
            11 => {
                if index != 0 {
                    return Err(OUT_OF_RANGE);
                }
                wire::encode_time_snapshot(document.entries()).map_err(wire_error)
            }
            10 => {
                if index != 0 {
                    return Err(OUT_OF_RANGE);
                }
                let size = document.layout_size().ok_or(UNRESOLVED_REFERENCE)?;
                let mut bytes = Vec::with_capacity(8);
                for axis in size {
                    bytes.extend_from_slice(&axis.to_le_bytes());
                }
                Ok(bytes)
            }
            _ => {
                let resolved =
                    document
                        .resolve_entry_style(index)
                        .map_err(|error| match error {
                            StyleResolutionError::EntryIndex => OUT_OF_RANGE,
                            StyleResolutionError::Uninitialized => UNRESOLVED_REFERENCE,
                        })?;
                if kind == 8 {
                    match resolved {
                        ResolvedStyle::Null => Err(UNRESOLVED_REFERENCE),
                        ResolvedStyle::Value(value) => {
                            style_wire::encode(value).map_err(style_wire_error)
                        }
                    }
                } else {
                    let (state, charset): (u32, i32) = match resolved {
                        ResolvedStyle::Null => (1, -1),
                        ResolvedStyle::Value(value) => (2, value.char_set),
                    };
                    let mut bytes = Vec::with_capacity(8);
                    bytes.extend_from_slice(&state.to_le_bytes());
                    bytes.extend_from_slice(&charset.to_le_bytes());
                    Ok(bytes)
                }
            }
        }
    }));
    let id = required!(insert(Resource::Snapshot(bytes)));
    // SAFETY: capacity was checked before snapshot insertion, with no loans now.
    unsafe { write_output(output, &id.to_le_bytes()) };
    OK
}

/// # Safety
/// Output follows the byte-buffer contract, capacity >=8.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_snapshot_size_v1(
    snapshot: u64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let length = required!(admitted(|registry| {
        Ok(registry.snapshot(snapshot).map_err(registry_error)?.len() as u64)
    }));
    // SAFETY: capacity checked; no snapshot loan remains.
    unsafe { write_output(output, &length.to_le_bytes()) };
    OK
}

/// # Safety
/// Output follows the byte-buffer contract for exactly length bytes. Zero length
/// permits null and performs no access; offset must still lie within snapshot.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_snapshot_read_v1(
    snapshot: u64,
    offset: u64,
    output: *mut u8,
    length: usize,
) -> u32 {
    required!(output_capacity(output, length, length));
    let offset = required!(usize::try_from(offset).map_err(|_| OUT_OF_RANGE));
    let end = required!(offset.checked_add(length).ok_or(OUT_OF_RANGE));
    let bytes = required!(admitted(|registry| {
        Ok(registry
            .snapshot(snapshot)
            .map_err(registry_error)?
            .get(offset..end)
            .ok_or(OUT_OF_RANGE)?
            .to_vec())
    }));
    // SAFETY: caller range validated; snapshot borrow ended before this copy.
    unsafe { write_output(output, &bytes) };
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_snapshot_drop_v1(snapshot: u64) -> u32 {
    status(
        DOCUMENTS
            .destroy(snapshot, Kind::Snapshot)
            .map_err(registry_error),
    )
}

/// # Safety
/// Input/output follow the byte-buffer contract. Input is one exact entry wire;
/// output capacity >=8. Overlap is permitted: input is copied before admission.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_add_prepared_v1(
    owner: u64,
    mode: u32,
    input: *const u8,
    length: usize,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let (mode, preparation) = match mode {
        0 => (AddMode::Add, None),
        1 => (AddMode::EntryOnly, None),
        2 => (AddMode::WebVtt, None),
        3 => (AddMode::Add, Some(false)),
        4 => (AddMode::EntryOnly, Some(true)),
        5 => (AddMode::WebVtt, Some(false)),
        _ => return INVALID_INPUT,
    };
    // SAFETY: caller lends the bounded readable range; helper snapshots it now.
    let input = required!(unsafe { copy_input(input, length) });
    let entry = required!(wire::decode_entry(&input).map_err(wire_error));
    let entry = if let Some(entry_only) = preparation {
        required!(
            xy_vsfilter_compat::document_prepare::prepare_add_record(entry, entry_only).map_err(
                |error| match error {
                    xy_vsfilter_compat::document_prepare::Error::UnresolvedTail =>
                        UNRESOLVED_REFERENCE,
                    xy_vsfilter_compat::document_prepare::Error::CountOverflow
                    | xy_vsfilter_compat::document_prepare::Error::LengthOverflow => COUNT_OVERFLOW,
                }
            )
        )
    } else {
        entry
    };
    let index = required!(admitted(|registry| {
        let document = registry.document_mut(owner).map_err(registry_error)?;
        // The v1 wire admits at most INT_MAX entries, stricter than the core's
        // final index INT_MAX. This is a declared experimental transport bound,
        // not a native parity result for an enormous document.
        if document.entries().len() >= i32::MAX as usize
            && entry.start <= entry.end
            && (mode == AddMode::EntryOnly || entry.start != entry.end)
        {
            return Err(COUNT_OVERFLOW);
        }
        document.add_prepared(entry, mode).map_err(refusal)
    }));
    let mut bytes = [0; 8];
    bytes[..4].copy_from_slice(&u32::from(index.is_some()).to_le_bytes());
    // Core Add validates the signed native index domain before any mutation.
    bytes[4..].copy_from_slice(&index.map_or(-1, |index| index as i32).to_le_bytes());
    // SAFETY: output capacity checked before any mutation; admission ended.
    unsafe { write_output(output, &bytes) };
    OK
}

fn insert_text_plan(plan: xy_vsfilter_compat::document_text::Plan) -> Result<u64, u32> {
    use xy_vsfilter_compat::document_text::Action;
    // All text/style loans ended. Encode and register only independently owned bytes.
    if plan.text.len() > i32::MAX as usize {
        return Err(COUNT_OVERFLOW);
    }
    let length = plan
        .text
        .len()
        .checked_mul(2)
        .and_then(|n| n.checked_add(32))
        .filter(|n| *n <= isize::MAX as usize)
        .ok_or(COUNT_OVERFLOW)?;
    let mut bytes = Vec::with_capacity(length);
    bytes.extend_from_slice(&(plan.action as u32).to_le_bytes());
    bytes.extend_from_slice(&u32::from(plan.target_unicode).to_le_bytes());
    bytes.extend_from_slice(&u32::from(plan.strip_tags).to_le_bytes());
    bytes.extend_from_slice(&u32::from(plan.has_style).to_le_bytes());
    let charset = if plan.action == Action::NoOp {
        0
    } else {
        plan.charset
    };
    bytes.extend_from_slice(&charset.to_le_bytes());
    bytes.extend_from_slice(&0u32.to_le_bytes());
    bytes.extend_from_slice(&(plan.text.len() as u64).to_le_bytes());
    for unit in &plan.text {
        bytes.extend_from_slice(&unit.to_le_bytes());
    }
    insert(Resource::Snapshot(bytes))
}

/// # Safety
/// Output receives one immutable snapshot ID in eight writable bytes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_text_plan_v1(
    owner: u64,
    index: u32,
    mode: u32,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    use xy_vsfilter_compat::document_text::Mode;
    let mode = match mode {
        0 => Mode::GetWideSsa,
        1 => Mode::GetWideStrip,
        2 => Mode::GetPackedSsa,
        3 => Mode::GetPackedStrip,
        4 => Mode::ConvertWide,
        5 => Mode::ConvertPacked,
        _ => return INVALID_INPUT,
    };
    let plan = required!(admitted(|registry| {
        xy_vsfilter_compat::document_text::plan(
            registry.document(owner).map_err(registry_error)?,
            index,
            mode,
        )
        .map_err(|error| match error {
            StyleResolutionError::EntryIndex => OUT_OF_RANGE,
            StyleResolutionError::Uninitialized => UNRESOLVED_REFERENCE,
        })
    }));
    let id = required!(insert_text_plan(plan));
    // SAFETY: preflighted destination, registry admission has ended.
    unsafe {
        write_output(output, &id.to_le_bytes());
    }
    OK
}

/// # Safety
/// Raw text follows the byte-buffer contract; output receives one snapshot ID.
/// Input is fully copied before admission and may overlap the output buffer.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_set_text_plan_v1(
    owner: u64,
    index: u32,
    input_unicode: u32,
    input: *const u8,
    length: usize,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    if input_unicode > 1 {
        return INVALID_INPUT;
    }
    required!(element_shape(length, 2));
    // SAFETY: caller supplies the readable input region; no pointer enters admission.
    let input = required!(unsafe { copy_input(input, length) });
    let text = units(&input);
    let plan = required!(admitted(|registry| {
        xy_vsfilter_compat::document_text::plan_write(
            registry.document(owner).map_err(registry_error)?,
            index,
            input_unicode != 0,
            &text,
        )
        .map_err(|error| {
            use xy_vsfilter_compat::document_text::WriteError;
            match error {
                WriteError::Entry(StyleResolutionError::EntryIndex) => OUT_OF_RANGE,
                WriteError::Entry(StyleResolutionError::Uninitialized) => UNRESOLVED_REFERENCE,
                WriteError::Preparation(
                    xy_vsfilter_compat::document_prepare::Error::UnresolvedTail,
                ) => UNRESOLVED_REFERENCE,
                WriteError::Preparation(
                    xy_vsfilter_compat::document_prepare::Error::CountOverflow
                    | xy_vsfilter_compat::document_prepare::Error::LengthOverflow,
                ) => COUNT_OVERFLOW,
            }
        })
    }));
    let id = required!(insert_text_plan(plan));
    // SAFETY: capacity preflighted, input snapshotted, no registry reference remains.
    unsafe {
        write_output(output, &id.to_le_bytes());
    }
    OK
}

/// # Safety
/// Input follows the byte-buffer contract and has an even raw-UTF16LE length.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_set_prepared_text_v1(
    owner: u64,
    index: u32,
    flag: i32,
    input: *const u8,
    length: usize,
) -> u32 {
    let flag = match flag {
        -1 => None,
        0 => Some(false),
        1 => Some(true),
        _ => return INVALID_INPUT,
    };
    required!(element_shape(length, 2));
    // SAFETY: caller range is copied before registry admission.
    let input = required!(unsafe { copy_input(input, length) });
    let mut text = Some(units(&input));
    let retired = required!(admitted(|registry| {
        let document = registry.document_mut(owner).map_err(registry_error)?;
        document.entries().get(index as usize).ok_or(OUT_OF_RANGE)?;
        document
            .replace_prepared_text(index as usize, text.take().ok_or(INVALID_INPUT)?, flag)
            .map_err(refusal)
    }));
    drop(retired);
    OK
}

/// # Safety
/// Input follows the byte-buffer contract and has an even raw-UTF16LE length.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_set_prepared_style_v1(
    owner: u64,
    index: u32,
    input: *const u8,
    length: usize,
) -> u32 {
    required!(element_shape(length, 2));
    // SAFETY: caller range is copied before registry admission.
    let input = required!(unsafe { copy_input(input, length) });
    let style = units(&input);
    mutate(owner, |document| {
        document.set_prepared_style(index as usize, style)
    })
}
/// # Safety
/// Both inputs follow the byte-buffer contract and have even raw-UTF16LE byte
/// lengths. Overlap is allowed; both values are owned before admission. CString
/// comparison terminates at first NUL, while replacement preserves all units.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_rename_style_cstring_v1(
    owner: u64,
    old: *const u8,
    old_length: usize,
    new: *const u8,
    new_length: usize,
) -> u32 {
    required!(element_shape(old_length, 2));
    required!(element_shape(new_length, 2));
    // SAFETY: caller provides both readable ranges; neither can change during
    // these copies, and no caller pointer enters the registry closure.
    let old = required!(unsafe { copy_input(old, old_length) });
    let new = required!(unsafe { copy_input(new, new_length) });
    let old = units(&old);
    let new = units(&new);
    mutate(owner, |document| {
        document.rename_style_cstring(&old, &new);
        Ok(())
    })
}

#[no_mangle]
pub extern "C" fn xy_vsfilter_document_set_entry_time_v1(
    owner: u64,
    index: u32,
    start: u32,
    value: i64,
) -> u32 {
    let start = match start {
        0 => false,
        1 => true,
        _ => return INVALID_INPUT,
    };
    mutate(owner, |document| {
        document.set_entry_time(index as usize, start, value)
    })
}

/// # Safety
/// Input follows the byte-buffer contract, containing only u32LE indexes.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_reorder_entries_v1(
    owner: u64,
    input: *const u8,
    length: usize,
) -> u32 {
    required!(element_shape(length, 4));
    // SAFETY: caller range is copied before registry admission.
    let input = required!(unsafe { copy_input(input, length) });
    let order: Vec<_> = input
        .chunks_exact(4)
        .map(|bytes| u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]) as usize)
        .collect();
    mutate(owner, |document| document.reorder_entries(&order))
}

/// # Safety
/// Input follows the byte-buffer contract, containing complete i64LE start/end pairs.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_replace_entry_times_v1(
    owner: u64,
    input: *const u8,
    length: usize,
) -> u32 {
    required!(element_shape(length, 16));
    // SAFETY: caller range is copied before registry admission.
    let input = required!(unsafe { copy_input(input, length) });
    let times: Vec<_> = input
        .chunks_exact(16)
        .map(|bytes| {
            let mut start = [0; 8];
            let mut end = [0; 8];
            start.copy_from_slice(&bytes[..8]);
            end.copy_from_slice(&bytes[8..]);
            (i64::from_le_bytes(start), i64::from_le_bytes(end))
        })
        .collect();
    mutate(owner, |document| document.replace_entry_times(&times))
}
/// # Safety
/// Input contains complete i64LE start/end pairs. Output follows the byte-buffer
/// contract and receives one u64LE applied flag. Native arithmetic/callbacks
/// remain outside admission; staging malformed buffers precedes the mode guard.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_commit_time_conversion_v1(
    owner: u64,
    target: u32,
    input: *const u8,
    length: usize,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 8));
    let target = match target {
        0 => ConversionMode::Time,
        1 => ConversionMode::Frame,
        _ => return INVALID_INPUT,
    };
    required!(element_shape(length, 16));
    // SAFETY: caller range is copied before admission and before output writes.
    let input = required!(unsafe { copy_input(input, length) });
    let times: Vec<_> = input
        .chunks_exact(16)
        .map(|row| {
            let mut start = [0; 8];
            let mut end = [0; 8];
            start.copy_from_slice(&row[..8]);
            end.copy_from_slice(&row[8..]);
            (i64::from_le_bytes(start), i64::from_le_bytes(end))
        })
        .collect();
    let applied = required!(admitted(|registry| registry
        .document_mut(owner)
        .map_err(registry_error)?
        .commit_time_conversion(target, &times)
        .map_err(refusal)));
    // SAFETY: capacity was validated before mutation; no registry reference escapes.
    unsafe { write_output(output, &u64::from(applied).to_le_bytes()) };
    OK
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_rebuild_segments_v1(owner: u64) -> u32 {
    mutate(owner, |document| {
        document.rebuild_segments();
        Ok(())
    })
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_clear_storage_v1(owner: u64) -> u32 {
    mutate(owner, |document| {
        document.clear_entries_and_segments();
        Ok(())
    })
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_remove_entries_v1(owner: u64) -> u32 {
    mutate(owner, |document| {
        document.remove_all_entries();
        Ok(())
    })
}
#[no_mangle]
pub extern "C" fn xy_vsfilter_document_animation_or_v1(
    owner: u64,
    segment: u32,
    animated: u32,
) -> u32 {
    let animated = match animated {
        0 => false,
        1 => true,
        _ => return INVALID_INPUT,
    };
    mutate(owner, |document| {
        document.feedback_animation(segment as usize, animated)
    })
}

/// # Safety
/// Output follows the byte-buffer contract, capacity >=16, checked before correction.
#[no_mangle]
pub unsafe extern "C" fn xy_vsfilter_document_correct_last_end_v1(
    owner: u64,
    bound: i64,
    output: *mut u8,
    capacity: usize,
) -> u32 {
    required!(output_capacity(output, capacity, 16));
    let end = required!(admitted(|registry| {
        Ok(registry
            .document_mut(owner)
            .map_err(registry_error)?
            .correct_last_end(bound))
    }));
    let mut bytes = [0; 16];
    bytes[..4].copy_from_slice(&u32::from(end.is_some()).to_le_bytes());
    bytes[8..].copy_from_slice(&end.unwrap_or(0).to_le_bytes());
    // SAFETY: capacity checked before mutation, with all canonical loans ended.
    unsafe { write_output(output, &bytes) };
    OK
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;
    use xy_vsfilter_compat::document::PreparedEntry;

    fn state(owner: u64) -> Document {
        admitted(|registry| Ok(registry.document(owner).map_err(registry_error)?.clone())).unwrap()
    }
    unsafe fn create() -> u64 {
        let mut bytes = [0xa5; 17];
        // SAFETY: this live byte destination deliberately has an unaligned offset.
        assert_eq!(
            unsafe { xy_vsfilter_document_create_v1(bytes.as_mut_ptr().add(1), 16) },
            OK
        );
        assert_eq!(bytes[0], 0xa5);
        assert_eq!(&bytes[9..], &[0xa5; 8]);
        u64::from_le_bytes(bytes[1..9].try_into().unwrap())
    }
    fn entry(start: i64, end: i64) -> Vec<u8> {
        wire::encode_entry(&PreparedEntry {
            text: vec![0, 0xd800, 0xffff],
            style: vec![0, 65],
            actor: vec![0xdc00],
            effect: vec![],
            unicode: false,
            margins: [-1, 2, -3, 4],
            layer: -1,
            start,
            end,
            readorder: -1,
        })
        .unwrap()
    }
    unsafe fn add(owner: u64, mode: u32, bytes: &[u8]) -> (u32, [u8; 8]) {
        let mut output = [0xa5; 8];
        // SAFETY: live readable input and exclusive full output ranges.
        let status = unsafe {
            xy_vsfilter_document_add_prepared_v1(
                owner,
                mode,
                bytes.as_ptr(),
                bytes.len(),
                output.as_mut_ptr(),
                output.len(),
            )
        };
        (status, output)
    }
    unsafe fn open_snapshot(owner: u64, kind: u32) -> u64 {
        let mut bytes = [0; 8];
        // SAFETY: fully writable byte destination.
        assert_eq!(
            unsafe { xy_vsfilter_document_snapshot_open_v1(owner, kind, 0, bytes.as_mut_ptr(), 8) },
            OK
        );
        u64::from_le_bytes(bytes)
    }
    unsafe fn read_snapshot(id: u64) -> Vec<u8> {
        let mut size = [0; 8];
        // SAFETY: writable size field, followed by allocated exact-size output.
        assert_eq!(
            unsafe { xy_vsfilter_document_snapshot_size_v1(id, size.as_mut_ptr(), 8) },
            OK
        );
        let mut bytes = vec![0; u64::from_le_bytes(size) as usize];
        assert_eq!(
            unsafe {
                xy_vsfilter_document_snapshot_read_v1(id, 0, bytes.as_mut_ptr(), bytes.len())
            },
            OK
        );
        bytes
    }

    #[test]
    fn byte_bounds_and_shapes_reject_without_access() {
        assert_eq!(byte_range(ptr::null(), 0), Ok(()));
        assert_eq!(byte_range(ptr::null(), 1), Err(INVALID_INPUT));
        assert_eq!(
            byte_range(ptr::NonNull::<u8>::dangling().as_ptr(), usize::MAX),
            Err(INVALID_INPUT)
        );
        assert_eq!(byte_range(usize::MAX as *const u8, 1), Err(INVALID_INPUT));
        assert_eq!(element_shape(3, 2), Err(INVALID_INPUT));
        if usize::BITS > 32 {
            let bytes = (i32::MAX as usize + 1).checked_mul(2).unwrap();
            assert_eq!(element_shape(bytes, 2), Err(COUNT_OVERFLOW));
        }
        assert_eq!(element_shape(0, 16), Ok(()));
        // SAFETY: null/zero is explicitly supported and never dereferenced.
        assert_eq!(unsafe { copy_input(ptr::null(), 0) }, Ok(vec![]));
    }

    // All global-export checks share one test to avoid unrelated parallel tests
    // observing private Busy. Registry concurrency is tested on separate stores.
    #[test]
    fn experimental_v1_exports_preserve_ownership_refusals_and_output_atomicity() {
        // SAFETY: allocated byte ranges below are valid for each supplied size;
        // explicit null/oversized checks are rejected before raw accesses.
        unsafe {
            // Supplied-only simplicity uses the existing checked metadata boundary.
            let style_owner = create();
            let mut out = [0xa5; 16];
            assert_eq!(
                xy_vsfilter_document_ass_begin_v1(style_owner, out.as_mut_ptr(), 8),
                OK
            );
            let run = u64::from_le_bytes(out[..8].try_into().unwrap());
            let key: Vec<u8> = "style".encode_utf16().flat_map(u16::to_le_bytes).collect();
            assert_eq!(
                xy_vsfilter_document_ass_step_v1(
                    run,
                    0,
                    0,
                    key.as_ptr(),
                    key.len(),
                    out.as_mut_ptr(),
                    16
                ),
                OK
            );
            out.fill(0xa5);
            for (id, command, bits, length, capacity, expected) in [
                (run, 0, 0, 1, 16, INVALID_INPUT),
                (run, 0, 1, 0, 16, INVALID_INPUT),
                (run, 0, 0, 0, 15, INVALID_INPUT),
                (run, 4, 0, 0, 16, INVALID_INPUT),
                (style_owner, 0, 0, 0, 16, BAD_HANDLE),
            ] {
                assert_eq!(
                    xy_vsfilter_document_ass_style_v1(
                        id,
                        command,
                        bits,
                        key.as_ptr(),
                        length,
                        ptr::null(),
                        0,
                        out.as_mut_ptr(),
                        capacity
                    ),
                    expected
                );
                assert_eq!(out, [0xa5; 16]);
            }
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_ass_style_v1(
                        run,
                        0,
                        0,
                        ptr::null(),
                        0,
                        ptr::null(),
                        0,
                        out.as_mut_ptr(),
                        16
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                xy_vsfilter_document_ass_style_v1(
                    run,
                    0,
                    0,
                    key.as_ptr(),
                    key.len(),
                    ptr::null(),
                    0,
                    out.as_mut_ptr(),
                    16
                ),
                OK
            );
            let raw = [42, 0, 65, 0, 0, 0, 0, 0xd8];
            loop {
                assert_eq!(
                    xy_vsfilter_document_ass_style_part_v1(run, 0, out.as_mut_ptr(), 8),
                    OK
                );
                let part = u64::from_le_bytes(out[..8].try_into().unwrap());
                let bytes = read_snapshot(part);
                assert_eq!(xy_vsfilter_document_snapshot_drop_v1(part), OK);
                let field = u32::from_le_bytes(bytes[..4].try_into().unwrap());
                if field == 0 {
                    break;
                }
                let (command, bits, text, len) = match field {
                    1 | 2 => (3, 0, raw.as_ptr(), raw.len()),
                    3 | 12..=15 | 17 | 18 => (2, (-0.0f64).to_bits(), ptr::null(), 0),
                    _ => (1, u32::MAX.into(), ptr::null(), 0),
                };
                out.fill(0xa5);
                assert_eq!(
                    xy_vsfilter_document_ass_style_v1(
                        run,
                        command,
                        bits,
                        text,
                        len,
                        ptr::null(),
                        0,
                        out.as_mut_ptr(),
                        15
                    ),
                    INVALID_INPUT
                );
                assert_eq!(out, [0xa5; 16]);
                assert_eq!(
                    xy_vsfilter_document_ass_style_v1(
                        run,
                        command,
                        bits,
                        text,
                        len,
                        ptr::null(),
                        0,
                        out.as_mut_ptr(),
                        16
                    ),
                    OK
                );
            }
            assert_eq!(
                xy_vsfilter_document_ass_style_part_v1(run, 2, out.as_mut_ptr(), 8),
                OK
            );
            let result = u64::from_le_bytes(out[..8].try_into().unwrap());
            let bytes = read_snapshot(result);
            assert_eq!(
                xy_vsfilter_document_ass_style_v1(
                    run,
                    4,
                    0,
                    ptr::null(),
                    0,
                    ptr::null(),
                    0,
                    out.as_mut_ptr(),
                    16
                ),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_ass_style_part_v1(run, 2, out.as_mut_ptr(), 8),
                INVALID_INPUT
            );
            assert_eq!(xy_vsfilter_document_destroy_v1(style_owner), OK);
            assert_eq!(
                xy_vsfilter_document_ass_style_part_v1(run, 0, out.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(xy_vsfilter_document_ass_drop_v1(run), OK);
            assert_eq!(read_snapshot(result), bytes);
            assert_eq!(
                style_wire::decode(&bytes).unwrap().font_name,
                [42, 65, 0, 0xd800]
            );
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(result), OK);
            let owner = create();
            let mut output = [0xa5; 16];
            assert_eq!(
                xy_vsfilter_document_ass_begin_v1(owner, output.as_mut_ptr(), 8),
                OK
            );
            let run = u64::from_le_bytes(output[..8].try_into().unwrap());
            let mut input: Vec<u8> = " [Events] "
                .encode_utf16()
                .flat_map(u16::to_le_bytes)
                .collect();
            let mut spaces = [0u8; 10];
            spaces[0] = 1;
            spaces[9] = 1;
            output.fill(0xa5);
            for (eof, len, space_len, capacity) in [
                (2, 20, 10, 16),
                (1, 19, 10, 16),
                (1, 20, 9, 16),
                (1, 20, 10, 15),
            ] {
                assert_eq!(
                    xy_vsfilter_document_ass_read_v1(
                        run,
                        eof,
                        input.as_ptr(),
                        len,
                        spaces.as_ptr(),
                        space_len,
                        output.as_mut_ptr(),
                        capacity
                    ),
                    INVALID_INPUT
                );
                assert_eq!(output, [0xa5; 16]);
            }
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_ass_read_v1(
                        run,
                        1,
                        input.as_ptr(),
                        20,
                        spaces.as_ptr(),
                        10,
                        output.as_mut_ptr(),
                        16
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                xy_vsfilter_document_ass_read_v1(
                    owner,
                    1,
                    input.as_ptr(),
                    20,
                    spaces.as_ptr(),
                    10,
                    output.as_mut_ptr(),
                    16
                ),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_ass_read_v1(
                    run,
                    1,
                    input.as_ptr(),
                    20,
                    spaces.as_ptr(),
                    10,
                    output.as_mut_ptr(),
                    16
                ),
                OK
            );
            input.fill(0);
            spaces.fill(0);
            assert_eq!(
                xy_vsfilter_document_ass_input_part_v1(run, 0, output.as_mut_ptr(), 8),
                OK
            );
            let key = u64::from_le_bytes(output[..8].try_into().unwrap());
            let retained = read_snapshot(key);
            assert_eq!(&retained[..8], &[1, 0, 0, 0, 0, 0, 0, 0]);
            assert_eq!(
                &retained[8..],
                &"[Events]"
                    .encode_utf16()
                    .flat_map(u16::to_le_bytes)
                    .collect::<Vec<_>>()
            );
            output.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_ass_input_part_v1(run, 1, output.as_mut_ptr(), 8),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_ass_input_part_v1(run, 0, output.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_ass_input_part_v1(
                        run,
                        0,
                        output.as_mut_ptr(),
                        8
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(output, [0xa5; 16]);
            let lower: Vec<u8> = "[events]"
                .encode_utf16()
                .flat_map(u16::to_le_bytes)
                .collect();
            assert_eq!(
                xy_vsfilter_document_ass_step_v1(
                    run,
                    0,
                    0,
                    lower.as_ptr(),
                    lower.len(),
                    output.as_mut_ptr(),
                    16
                ),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_ass_step_v1(
                    run,
                    8,
                    1,
                    ptr::null(),
                    0,
                    output.as_mut_ptr(),
                    16
                ),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_ass_read_v1(
                    run,
                    1,
                    ptr::null(),
                    0,
                    ptr::null(),
                    0,
                    output.as_mut_ptr(),
                    16
                ),
                INVALID_INPUT
            );
            assert_eq!(xy_vsfilter_document_ass_drop_v1(run), OK);
            assert_eq!(xy_vsfilter_document_destroy_v1(owner), OK);
            assert_eq!(read_snapshot(key), retained);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(key), OK);
            let ass_owner = create();
            let mut out = [0xa5; 16];
            let before = state(ass_owner);
            assert_eq!(
                xy_vsfilter_document_ass_begin_v1(ass_owner, out.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_ass_begin_v1(ass_owner, ptr::null_mut(), 8),
                INVALID_INPUT
            );
            assert_eq!(state(ass_owner), before);
            assert_eq!(out, [0xa5; 16]);
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_ass_begin_v1(ass_owner, out.as_mut_ptr(), 8))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                xy_vsfilter_document_ass_begin_v1(ass_owner, out.as_mut_ptr(), 8),
                OK
            );
            let ass_run = u64::from_le_bytes(out[..8].try_into().unwrap());
            assert_eq!(
                state(ass_owner).metadata(MetadataField::ColorMatrix),
                Some(0)
            );
            assert_eq!(
                state(ass_owner).metadata(MetadataField::ColorRange),
                Some(1)
            );
            let key: Vec<u8> = "playresx"
                .encode_utf16()
                .flat_map(u16::to_le_bytes)
                .collect();
            out.fill(0xa5);
            for (id, command, value, length, capacity, expected) in [
                (ass_run, 0, 0, key.len(), 15, INVALID_INPUT),
                (ass_run, 0, 0, 1, 16, INVALID_INPUT),
                (ass_run, 0, 1, key.len(), 16, INVALID_INPUT),
                (ass_run, 9, 0, 0, 16, INVALID_INPUT),
                (ass_owner, 0, 0, key.len(), 16, BAD_HANDLE),
                (ass_run, 1, 10, 0, 16, INVALID_INPUT),
            ] {
                assert_eq!(
                    xy_vsfilter_document_ass_step_v1(
                        id,
                        command,
                        value,
                        key.as_ptr(),
                        length,
                        out.as_mut_ptr(),
                        capacity
                    ),
                    expected
                );
                assert_eq!(out, [0xa5; 16]);
            }
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_ass_step_v1(
                        ass_run,
                        0,
                        0,
                        key.as_ptr(),
                        key.len(),
                        out.as_mut_ptr(),
                        16
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(out, [0xa5; 16]);
            // Same native range can be input and output: copying precedes loans.
            out.copy_from_slice(&key);
            assert_eq!(
                xy_vsfilter_document_ass_step_v1(
                    ass_run,
                    0,
                    0,
                    out.as_ptr(),
                    16,
                    out.as_mut_ptr(),
                    16
                ),
                OK
            );
            assert_eq!(
                out.chunks_exact(4)
                    .map(|b| i32::from_le_bytes(b.try_into().unwrap()))
                    .collect::<Vec<_>>(),
                vec![4, 3, 3, 0]
            );
            out.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_ass_step_v1(
                    ass_run,
                    1,
                    1280,
                    ptr::null(),
                    0,
                    out.as_mut_ptr(),
                    16
                ),
                UNRESOLVED_REFERENCE
            );
            assert_eq!(out, [0xa5; 16]);
            assert_eq!(xy_vsfilter_document_set_script_size_v1(ass_owner, 0, 0), OK);
            assert_eq!(
                xy_vsfilter_document_ass_step_v1(
                    ass_run,
                    1,
                    1280,
                    ptr::null(),
                    0,
                    out.as_mut_ptr(),
                    16
                ),
                OK
            );
            assert_eq!(state(ass_owner).script_size(), Some([1280, 1024]));
            assert_eq!(xy_vsfilter_document_ass_drop_v1(ass_owner), BAD_HANDLE);
            assert_eq!(xy_vsfilter_document_destroy_v1(ass_owner), OK);
            out.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_ass_step_v1(
                    ass_run,
                    0,
                    0,
                    key.as_ptr(),
                    key.len(),
                    out.as_mut_ptr(),
                    16
                ),
                BAD_HANDLE
            );
            assert_eq!(out, [0xa5; 16]);
            assert_eq!(xy_vsfilter_document_ass_drop_v1(ass_run), OK);
            assert_eq!(xy_vsfilter_document_ass_drop_v1(ass_run), BAD_HANDLE);
            let owner = create();
            assert_eq!(
                xy_vsfilter_document_ass_begin_v1(owner, out.as_mut_ptr(), 8),
                OK
            );
            let run = u64::from_le_bytes(out[..8].try_into().unwrap());
            let key: Vec<u8> = "dialogue"
                .encode_utf16()
                .flat_map(u16::to_le_bytes)
                .collect();
            assert_eq!(
                xy_vsfilter_document_ass_step_v1(
                    run,
                    0,
                    0,
                    key.as_ptr(),
                    key.len(),
                    out.as_mut_ptr(),
                    16
                ),
                OK
            );
            let input: Vec<u8> = "Marked=1,0:00:01.20,0:00:01.20,**Default,,1,2,3,same,same tail"
                .encode_utf16()
                .flat_map(u16::to_le_bytes)
                .collect();
            assert_eq!(
                xy_vsfilter_document_ass_step_v1(
                    run,
                    5,
                    0,
                    input.as_ptr(),
                    input.len(),
                    out.as_mut_ptr(),
                    16
                ),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_ass_dialogue_part_v1(run, 0, out.as_mut_ptr(), 8),
                OK
            );
            let token = u64::from_le_bytes(out[..8].try_into().unwrap());
            let retained = read_snapshot(token);
            assert_eq!(&retained[8..], &[49, 0]);
            out.fill(0xa5);
            for group in [1, 4, 99] {
                assert_eq!(
                    xy_vsfilter_document_ass_dialogue_part_v1(run, group, out.as_mut_ptr(), 8),
                    INVALID_INPUT
                );
                assert_eq!(out, [0xa5; 16]);
            }
            assert_eq!(
                xy_vsfilter_document_ass_dialogue_part_v1(run, 0, out.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_ass_dialogue_part_v1(owner, 0, out.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_ass_dialogue_part_v1(
                        run,
                        0,
                        out.as_mut_ptr(),
                        8
                    ))
                    .unwrap(),
                BUSY
            );
            for number in [1, 0, 0, 1, 20, 0, 0, 1, 20, 1, 2, 3] {
                assert_eq!(
                    xy_vsfilter_document_ass_step_v1(
                        run,
                        6,
                        number,
                        ptr::null(),
                        0,
                        out.as_mut_ptr(),
                        16
                    ),
                    OK
                );
            }
            out.fill(0xa5);
            let body: Vec<u8> = "same tail"
                .encode_utf16()
                .flat_map(u16::to_le_bytes)
                .collect();
            assert_eq!(
                xy_vsfilter_document_ass_step_v1(
                    run,
                    7,
                    4,
                    body.as_ptr(),
                    body.len(),
                    out.as_mut_ptr(),
                    16
                ),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_ass_step_v1(
                    run,
                    7,
                    3,
                    body.as_ptr(),
                    body.len(),
                    out.as_mut_ptr(),
                    15
                ),
                INVALID_INPUT
            );
            assert_eq!(out, [0xa5; 16]);
            assert!(state(owner).entries().is_empty());
            assert_eq!(
                xy_vsfilter_document_ass_step_v1(
                    run,
                    7,
                    3,
                    body.as_ptr(),
                    body.len(),
                    out.as_mut_ptr(),
                    16
                ),
                OK
            );
            assert_eq!(state(owner).entries().len(), 1);
            assert!(state(owner).entries()[0].effect.is_empty());
            assert_eq!(state(owner).entries()[0].start, 12_000_000);
            assert_eq!(state(owner).entries()[0].readorder, 0);
            assert_eq!(xy_vsfilter_document_ass_drop_v1(run), OK);
            assert_eq!(xy_vsfilter_document_destroy_v1(owner), OK);
            assert_eq!(read_snapshot(token), retained);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(token), OK);
            // Checked font protocol: copied buffers, preflight atomicity, Busy,
            // stale owners, and independent decoded snapshots.
            let font_owner = create();
            assert_eq!(
                xy_vsfilter_document_ass_begin_v1(font_owner, out.as_mut_ptr(), 8),
                OK
            );
            let font_run = u64::from_le_bytes(out[..8].try_into().unwrap());
            let key: Vec<u8> = "fontname"
                .encode_utf16()
                .flat_map(u16::to_le_bytes)
                .collect();
            assert_eq!(
                xy_vsfilter_document_ass_step_v1(
                    font_run,
                    0,
                    0,
                    key.as_ptr(),
                    key.len(),
                    out.as_mut_ptr(),
                    16
                ),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_ass_step_v1(
                    font_run,
                    9,
                    0,
                    ptr::null(),
                    0,
                    out.as_mut_ptr(),
                    16
                ),
                OK
            );
            let mut encoded = [33u8, 0, 33, 0];
            for (id, eof, length, spaces, capacity, expected) in [
                (font_run, 2, 4, 2, 16, INVALID_INPUT),
                (font_run, 0, 3, 2, 16, INVALID_INPUT),
                (font_run, 0, 4, 1, 16, INVALID_INPUT),
                (font_run, 0, 4, 2, 15, INVALID_INPUT),
                (font_owner, 0, 4, 2, 16, BAD_HANDLE),
            ] {
                out.fill(0xa5);
                assert_eq!(
                    xy_vsfilter_document_ass_font_read_v1(
                        id,
                        eof,
                        encoded.as_ptr(),
                        length,
                        [0, 0].as_ptr(),
                        spaces,
                        out.as_mut_ptr(),
                        capacity
                    ),
                    expected
                );
                assert_eq!(out, [0xa5; 16]);
            }
            assert_eq!(
                xy_vsfilter_document_ass_font_read_v1(
                    font_run,
                    0,
                    encoded.as_ptr(),
                    4,
                    [0, 2].as_ptr(),
                    2,
                    out.as_mut_ptr(),
                    16
                ),
                INVALID_INPUT
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_ass_font_read_v1(
                        font_run,
                        0,
                        encoded.as_ptr(),
                        4,
                        [0, 0].as_ptr(),
                        2,
                        out.as_mut_ptr(),
                        16
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                xy_vsfilter_document_ass_font_read_v1(
                    font_run,
                    0,
                    encoded.as_ptr(),
                    4,
                    [0, 0].as_ptr(),
                    2,
                    out.as_mut_ptr(),
                    16
                ),
                OK
            );
            encoded.fill(0);
            assert_eq!(
                xy_vsfilter_document_ass_font_read_v1(
                    font_run,
                    1,
                    ptr::null(),
                    0,
                    ptr::null(),
                    0,
                    out.as_mut_ptr(),
                    16
                ),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_ass_font_part_v1(font_run, out.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_ass_font_part_v1(font_owner, out.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_ass_font_part_v1(
                        font_run,
                        out.as_mut_ptr(),
                        8
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                xy_vsfilter_document_ass_font_part_v1(font_run, out.as_mut_ptr(), 8),
                OK
            );
            let font_snapshot = u64::from_le_bytes(out[..8].try_into().unwrap());
            assert_eq!(read_snapshot(font_snapshot), [1, 0, 0, 0, 0, 0, 0, 0, 0]);
            assert_eq!(
                xy_vsfilter_document_ass_step_v1(
                    font_run,
                    11,
                    0,
                    ptr::null(),
                    0,
                    out.as_mut_ptr(),
                    16
                ),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_ass_step_v1(
                    font_run,
                    10,
                    0,
                    ptr::null(),
                    0,
                    out.as_mut_ptr(),
                    16
                ),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_ass_step_v1(
                    font_run,
                    11,
                    0,
                    ptr::null(),
                    0,
                    out.as_mut_ptr(),
                    16
                ),
                OK
            );
            assert_eq!(xy_vsfilter_document_destroy_v1(font_owner), OK);
            assert_eq!(
                xy_vsfilter_document_ass_font_part_v1(font_run, out.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(read_snapshot(font_snapshot), [1, 0, 0, 0, 0, 0, 0, 0, 0]);
            assert_eq!(xy_vsfilter_document_ass_drop_v1(font_run), OK);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(font_snapshot), OK);
            let xowner = create();
            out.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_xombie_begin_v1(xowner, out.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(out, [0xa5; 16]);
            assert_eq!(
                xy_vsfilter_document_xombie_begin_v1(xowner, out.as_mut_ptr(), 8),
                OK
            );
            let xrun = u64::from_le_bytes(out[..8].try_into().unwrap());
            let mut line: Vec<u8> = "version=1"
                .encode_utf16()
                .flat_map(u16::to_le_bytes)
                .collect();
            assert_eq!(
                xy_vsfilter_document_xombie_read_v1(
                    xrun,
                    2,
                    line.as_ptr(),
                    line.len(),
                    [0; 9].as_ptr(),
                    9
                ),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_xombie_read_v1(
                    xrun,
                    0,
                    line.as_ptr(),
                    line.len(),
                    [0; 8].as_ptr(),
                    8
                ),
                INVALID_INPUT
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_xombie_read_v1(
                        xrun,
                        0,
                        line.as_ptr(),
                        line.len(),
                        [0; 9].as_ptr(),
                        9
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                xy_vsfilter_document_xombie_read_v1(
                    xowner,
                    0,
                    line.as_ptr(),
                    line.len(),
                    [0; 9].as_ptr(),
                    9
                ),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_xombie_read_v1(
                    xrun,
                    0,
                    line.as_ptr(),
                    line.len(),
                    [0; 9].as_ptr(),
                    9
                ),
                OK
            );
            line.fill(0);
            assert_eq!(
                xy_vsfilter_document_xombie_part_v1(xrun, 0, out.as_mut_ptr(), 8),
                OK
            );
            let xsnapshot = u64::from_le_bytes(out[..8].try_into().unwrap());
            let retained = read_snapshot(xsnapshot);
            assert_eq!(
                &retained[16..],
                &"version"
                    .encode_utf16()
                    .flat_map(u16::to_le_bytes)
                    .collect::<Vec<_>>()
            );
            assert_eq!(
                xy_vsfilter_document_xombie_part_v1(xrun, 1, out.as_mut_ptr(), 8),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_xombie_step_v1(xrun, 4, 0, ptr::null(), 0, ptr::null(), 0),
                INVALID_INPUT
            );
            let key: Vec<u8> = "version"
                .encode_utf16()
                .flat_map(u16::to_le_bytes)
                .collect();
            assert_eq!(
                xy_vsfilter_document_xombie_step_v1(
                    xrun,
                    0,
                    0,
                    key.as_ptr(),
                    key.len(),
                    ptr::null(),
                    0
                ),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_xombie_step_v1(xrun, 2, 0, ptr::null(), 0, ptr::null(), 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_xombie_step_v1(
                    xrun,
                    3,
                    1f64.to_bits(),
                    ptr::null(),
                    0,
                    ptr::null(),
                    0
                ),
                OK
            );
            assert_eq!(xy_vsfilter_document_destroy_v1(xowner), OK);
            assert_eq!(
                xy_vsfilter_document_xombie_part_v1(xrun, 0, out.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(read_snapshot(xsnapshot), retained);
            assert_eq!(xy_vsfilter_document_xombie_drop_v1(xrun), OK);
            assert_eq!(xy_vsfilter_document_xombie_drop_v1(xrun), BAD_HANDLE);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(xsnapshot), OK);
            // Text parser packets copy their input before admission and retain
            // decoded snapshots independently of both session and parent owner.
            let text_owner = create();
            out.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_text_parser_begin_v1(text_owner, 4, out.as_mut_ptr(), 8),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_text_parser_begin_v1(text_owner, 0, out.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(out, [0xa5; 16]);
            assert_eq!(
                xy_vsfilter_document_text_parser_begin_v1(text_owner, 1, out.as_mut_ptr(), 8),
                OK
            );
            let text_run = u64::from_le_bytes(out[..8].try_into().unwrap());
            let mut raw: Vec<u8> = "[1][2]raw\0tail"
                .encode_utf16()
                .flat_map(u16::to_le_bytes)
                .collect();
            let spaces = alloc::vec![0;raw.len()/2];
            assert_eq!(
                xy_vsfilter_document_text_parser_read_v1(
                    text_run,
                    2,
                    raw.as_ptr(),
                    raw.len(),
                    spaces.as_ptr(),
                    spaces.len()
                ),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_text_parser_read_v1(
                    text_run,
                    0,
                    raw.as_ptr(),
                    raw.len(),
                    spaces.as_ptr(),
                    spaces.len() - 1
                ),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_text_parser_read_v1(
                    text_owner,
                    0,
                    raw.as_ptr(),
                    raw.len(),
                    spaces.as_ptr(),
                    spaces.len()
                ),
                BAD_HANDLE
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_text_parser_read_v1(
                        text_run,
                        0,
                        raw.as_ptr(),
                        raw.len(),
                        spaces.as_ptr(),
                        spaces.len()
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                xy_vsfilter_document_text_parser_read_v1(
                    text_run,
                    0,
                    raw.as_ptr(),
                    raw.len(),
                    spaces.as_ptr(),
                    spaces.len()
                ),
                OK
            );
            raw.fill(0);
            let values: Vec<u8> = [1i32, 2, 0, 0, 0, 0, 0, 0]
                .into_iter()
                .flat_map(i32::to_le_bytes)
                .collect();
            assert_eq!(
                xy_vsfilter_document_text_parser_step_v1(text_run, 0, 2, values.as_ptr(), 31),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_text_parser_step_v1(text_run, 1, 1, ptr::null(), 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_text_parser_step_v1(text_run, 0, 3, values.as_ptr(), 32),
                INVALID_INPUT
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_text_parser_step_v1(
                        text_run,
                        0,
                        2,
                        values.as_ptr(),
                        32
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                xy_vsfilter_document_text_parser_step_v1(text_run, 0, 2, values.as_ptr(), 32),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_text_parser_part_v1(text_run, out.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_text_parser_part_v1(text_run, out.as_mut_ptr(), 8),
                OK
            );
            let text_snapshot = u64::from_le_bytes(out[..8].try_into().unwrap());
            let retained = read_snapshot(text_snapshot);
            assert_eq!(
                &retained[24..],
                &"raw\0tail"
                    .encode_utf16()
                    .flat_map(u16::to_le_bytes)
                    .collect::<Vec<_>>()
            );
            assert_eq!(
                i64::from_le_bytes(retained[8..16].try_into().unwrap()),
                1_000_000
            );
            assert_eq!(
                xy_vsfilter_document_text_parser_step_v1(text_run, 2, 1, ptr::null(), 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_text_parser_step_v1(text_run, 2, 0, ptr::null(), 0),
                OK
            );
            assert_eq!(xy_vsfilter_document_destroy_v1(text_owner), OK);
            assert_eq!(
                xy_vsfilter_document_text_parser_part_v1(text_run, out.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(read_snapshot(text_snapshot), retained);
            assert_eq!(xy_vsfilter_document_text_parser_drop_v1(text_run), OK);
            assert_eq!(
                xy_vsfilter_document_text_parser_drop_v1(text_run),
                BAD_HANDLE
            );
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(text_snapshot), OK);
            // FindPipe's tagged payload and reply retain the same checked ID,
            // phase, BUSY, input-copy and independent snapshot guarantees.
            let pipe_owner = create();
            for format in [2, 3] {
                assert_eq!(
                    xy_vsfilter_document_text_parser_begin_v1(
                        pipe_owner,
                        format,
                        out.as_mut_ptr(),
                        8
                    ),
                    OK
                );
                let pipe_run = u64::from_le_bytes(out[..8].try_into().unwrap());
                let mut raw: Vec<u8> = "a|b|".encode_utf16().flat_map(u16::to_le_bytes).collect();
                assert_eq!(
                    xy_vsfilter_document_text_parser_read_v1(
                        pipe_run,
                        0,
                        raw.as_ptr(),
                        raw.len(),
                        [0; 4].as_ptr(),
                        4
                    ),
                    OK
                );
                raw.fill(0);
                assert_eq!(
                    xy_vsfilter_document_text_parser_part_v1(pipe_run, out.as_mut_ptr(), 8),
                    OK
                );
                let snapshot = u64::from_le_bytes(out[..8].try_into().unwrap());
                let retained = read_snapshot(snapshot);
                assert_eq!(retained[0], 7);
                assert_eq!(&retained[24..28], &[0; 4]);
                assert_eq!(&retained[28..], &[97, 0, 124, 0, 98, 0, 124, 0]);
                for bad in [-2, 0, 4] {
                    assert_eq!(
                        xy_vsfilter_document_text_parser_step_v1(pipe_run, 3, bad, ptr::null(), 0),
                        INVALID_INPUT
                    );
                }
                assert_eq!(
                    xy_vsfilter_document_text_parser_step_v1(pipe_run, 3, 1, raw.as_ptr(), 1),
                    INVALID_INPUT
                );
                assert_eq!(
                    DOCUMENTS
                        .try_with(|_| xy_vsfilter_document_text_parser_step_v1(
                            pipe_run,
                            3,
                            1,
                            ptr::null(),
                            0
                        ))
                        .unwrap(),
                    BUSY
                );
                assert_eq!(
                    xy_vsfilter_document_text_parser_step_v1(pipe_run, 3, 1, ptr::null(), 0),
                    OK
                );
                assert_eq!(
                    xy_vsfilter_document_text_parser_step_v1(pipe_run, 3, 1, ptr::null(), 0),
                    INVALID_INPUT
                );
                assert_eq!(
                    xy_vsfilter_document_text_parser_step_v1(pipe_run, 3, 3, ptr::null(), 0),
                    OK
                );
                assert_eq!(
                    xy_vsfilter_document_text_parser_step_v1(pipe_run, 3, -1, ptr::null(), 0),
                    INVALID_INPUT
                );
                assert_eq!(xy_vsfilter_document_text_parser_drop_v1(pipe_run), OK);
                assert_eq!(
                    xy_vsfilter_document_text_parser_step_v1(pipe_run, 3, -1, ptr::null(), 0),
                    BAD_HANDLE
                );
                assert_eq!(read_snapshot(snapshot), retained);
                assert_eq!(xy_vsfilter_document_snapshot_drop_v1(snapshot), OK);
            }
            assert_eq!(xy_vsfilter_document_destroy_v1(pipe_owner), OK);
            let micro_owner = create();
            out.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_microdvd_begin_v1(micro_owner, 128, out.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(out, [0xa5; 16]);
            assert_eq!(
                xy_vsfilter_document_microdvd_begin_v1(micro_owner, 128, out.as_mut_ptr(), 8),
                OK
            );
            let micro_run = u64::from_le_bytes(out[..8].try_into().unwrap());
            let mut raw: Vec<u8> = "{STYLE}{h:0}"
                .encode_utf16()
                .flat_map(u16::to_le_bytes)
                .collect();
            let spaces = alloc::vec![0;raw.len()/2];
            assert_eq!(
                xy_vsfilter_document_microdvd_read_v1(
                    micro_run,
                    2,
                    raw.as_ptr(),
                    raw.len(),
                    spaces.as_ptr(),
                    spaces.len()
                ),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_microdvd_read_v1(
                    micro_owner,
                    0,
                    raw.as_ptr(),
                    raw.len(),
                    spaces.as_ptr(),
                    spaces.len()
                ),
                BAD_HANDLE
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_microdvd_read_v1(
                        micro_run,
                        0,
                        raw.as_ptr(),
                        raw.len(),
                        spaces.as_ptr(),
                        spaces.len()
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                xy_vsfilter_document_microdvd_read_v1(
                    micro_run,
                    0,
                    raw.as_ptr(),
                    raw.len(),
                    spaces.as_ptr(),
                    spaces.len()
                ),
                OK
            );
            raw.fill(0);
            assert_eq!(
                xy_vsfilter_document_microdvd_step_v1(micro_run, 1, 0, 0, 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_microdvd_step_v1(micro_run, 0, 3, 0, 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_microdvd_step_v1(micro_run, 0, 0, 0, 0),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_microdvd_step_v1(micro_run, 1, 0, 0, 1),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_microdvd_step_v1(micro_run, 1, 0, 0, 0),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_microdvd_part_v1(micro_run, 2, out.as_mut_ptr(), 8),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_microdvd_part_v1(micro_run, 1, out.as_mut_ptr(), 8),
                OK
            );
            let name_snapshot = u64::from_le_bytes(out[..8].try_into().unwrap());
            assert_eq!(
                read_snapshot(name_snapshot),
                "STYLE"
                    .encode_utf16()
                    .flat_map(u16::to_le_bytes)
                    .collect::<Vec<_>>()
            );
            assert_eq!(
                xy_vsfilter_document_microdvd_part_v1(micro_run, 0, out.as_mut_ptr(), 8),
                OK
            );
            let micro_snapshot = u64::from_le_bytes(out[..8].try_into().unwrap());
            let retained = read_snapshot(micro_snapshot);
            assert_eq!(retained[0], 3);
            assert_eq!(retained[8], 128);
            assert_eq!(
                &retained[32..],
                &"{h:0}"
                    .encode_utf16()
                    .flat_map(u16::to_le_bytes)
                    .collect::<Vec<_>>()
            );
            let mut name: Vec<u8> = "Style".encode_utf16().flat_map(u16::to_le_bytes).collect();
            assert_eq!(
                xy_vsfilter_document_microdvd_style_v1(micro_run, 2, 0, name.as_ptr(), name.len()),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_microdvd_style_v1(micro_run, 0, 0, name.as_ptr(), name.len()),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_microdvd_style_v1(
                    micro_run,
                    1,
                    0,
                    name.as_ptr(),
                    name.len() - 1
                ),
                INVALID_INPUT
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_microdvd_style_v1(
                        micro_run,
                        1,
                        0,
                        name.as_ptr(),
                        name.len()
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                xy_vsfilter_document_microdvd_style_v1(micro_run, 1, 0, name.as_ptr(), name.len()),
                OK
            );
            name.fill(0);
            assert_eq!(
                xy_vsfilter_document_microdvd_part_v1(micro_run, 1, out.as_mut_ptr(), 8),
                OK
            );
            let current_name = u64::from_le_bytes(out[..8].try_into().unwrap());
            assert_eq!(
                read_snapshot(current_name),
                "Style"
                    .encode_utf16()
                    .flat_map(u16::to_le_bytes)
                    .collect::<Vec<_>>()
            );
            assert_eq!(
                xy_vsfilter_document_microdvd_step_v1(micro_run, 2, 0, 0, 0),
                INVALID_INPUT
            );
            assert_eq!(xy_vsfilter_document_destroy_v1(micro_owner), OK);
            assert_eq!(
                xy_vsfilter_document_microdvd_part_v1(micro_run, 0, out.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(read_snapshot(micro_snapshot), retained);
            assert_eq!(xy_vsfilter_document_microdvd_drop_v1(micro_run), OK);
            assert_eq!(xy_vsfilter_document_microdvd_drop_v1(micro_run), BAD_HANDLE);
            for snapshot in [micro_snapshot, name_snapshot, current_name] {
                assert_eq!(xy_vsfilter_document_snapshot_drop_v1(snapshot), OK);
            }
            // WebVTT: copies, phase errors, typed IDs, BUSY, and detached snapshots.
            let vtt_owner = create();
            out.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_vtt_begin_v1(vtt_owner, out.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert!(out.iter().all(|&b| b == 0xa5));
            assert_eq!(
                xy_vsfilter_document_vtt_begin_v1(vtt_owner, out.as_mut_ptr(), 8),
                OK
            );
            let vtt_run = u64::from_le_bytes(out[..8].try_into().unwrap());
            let magic: Vec<u8> = "WEBVTT".encode_utf16().flat_map(u16::to_le_bytes).collect();
            let spaces = [0u8; 6];
            assert_eq!(
                xy_vsfilter_document_vtt_read_v1(
                    vtt_owner,
                    0,
                    magic.as_ptr(),
                    magic.len(),
                    spaces.as_ptr(),
                    6
                ),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_vtt_read_v1(
                    vtt_run,
                    2,
                    magic.as_ptr(),
                    magic.len(),
                    spaces.as_ptr(),
                    6
                ),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_vtt_read_v1(
                    vtt_run,
                    0,
                    magic.as_ptr(),
                    magic.len() - 1,
                    spaces.as_ptr(),
                    6
                ),
                INVALID_INPUT
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_vtt_read_v1(
                        vtt_run,
                        0,
                        magic.as_ptr(),
                        magic.len(),
                        spaces.as_ptr(),
                        6
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                xy_vsfilter_document_vtt_read_v1(
                    vtt_run,
                    1,
                    magic.as_ptr(),
                    magic.len(),
                    spaces.as_ptr(),
                    6
                ),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_vtt_part_v1(vtt_run, 2, out.as_mut_ptr(), 8),
                OK
            );
            let vtt_colors = u64::from_le_bytes(out[..8].try_into().unwrap());
            let colors_before = read_snapshot(vtt_colors);
            assert_eq!(&colors_before[..4], &16u32.to_le_bytes());
            let vtt_read = |text: &str| {
                let bytes: Vec<u8> = text.encode_utf16().flat_map(u16::to_le_bytes).collect();
                let spaces = vec![0; bytes.len() / 2];
                assert_eq!(
                    xy_vsfilter_document_vtt_read_v1(
                        vtt_run,
                        0,
                        bytes.as_ptr(),
                        bytes.len(),
                        spaces.as_ptr(),
                        spaces.len()
                    ),
                    OK
                );
            };
            vtt_read("STYLE");
            vtt_read("body");
            vtt_read("");
            let three = |a: &str, b: &str, c: &str| {
                let parts = [
                    a.encode_utf16().collect::<Vec<_>>(),
                    b.encode_utf16().collect(),
                    c.encode_utf16().collect(),
                ];
                let mut bytes = Vec::new();
                for p in &parts {
                    bytes.extend_from_slice(&(p.len() as u32).to_le_bytes());
                }
                for p in &parts {
                    bytes.extend(p.iter().flat_map(|u| u.to_le_bytes()));
                }
                bytes
            };
            let mut color = three(".red", "", "ff00ff");
            assert_eq!(
                xy_vsfilter_document_vtt_packet_v1(vtt_run, 2, 0, color.as_ptr(), color.len() - 2),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_vtt_packet_v1(vtt_run, 2, 1, color.as_ptr(), color.len()),
                INVALID_INPUT
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_vtt_packet_v1(
                        vtt_run,
                        2,
                        0,
                        color.as_ptr(),
                        color.len()
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                xy_vsfilter_document_vtt_packet_v1(vtt_run, 2, 0, color.as_ptr(), color.len()),
                OK
            );
            color.fill(0xa5);
            assert_eq!(xy_vsfilter_document_vtt_step_v1(vtt_run, 1), INVALID_INPUT);
            assert_eq!(xy_vsfilter_document_vtt_step_v1(vtt_run, 0), OK);
            vtt_read("header");
            let header = three("1:2.3", "1:3.4", "");
            assert_eq!(
                xy_vsfilter_document_vtt_packet_v1(vtt_run, 0, 0, header.as_ptr(), header.len()),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_vtt_packet_v1(vtt_run, 0, 2, header.as_ptr(), header.len()),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_vtt_time_v1(vtt_run, 5, 1, 2, 3, 4),
                INVALID_INPUT
            );
            assert_eq!(xy_vsfilter_document_vtt_time_v1(vtt_run, 5, 1, 2, 3, 0), OK);
            assert_eq!(xy_vsfilter_document_vtt_time_v1(vtt_run, 5, 1, 3, 4, 0), OK);
            vtt_read("caption");
            let mut caption = [65, 0, 0, 0, 0, 0xd8];
            assert_eq!(
                xy_vsfilter_document_vtt_packet_v1(vtt_run, 1, 0, caption.as_ptr(), caption.len()),
                OK
            );
            caption.fill(0xa5);
            vtt_read("");
            assert_eq!(
                xy_vsfilter_document_vtt_part_v1(vtt_run, 0, out.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_vtt_part_v1(vtt_run, 3, out.as_mut_ptr(), 8),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_vtt_part_v1(vtt_run, 0, out.as_mut_ptr(), 8),
                OK
            );
            let vtt_snapshot = u64::from_le_bytes(out[..8].try_into().unwrap());
            let retained = read_snapshot(vtt_snapshot);
            assert_eq!(&retained[..4], &9u32.to_le_bytes());
            assert_eq!(&retained[24..], &[65, 0]);
            assert_eq!(xy_vsfilter_document_vtt_step_v1(vtt_run, 1), OK);
            assert_eq!(xy_vsfilter_document_destroy_v1(vtt_owner), OK);
            assert_eq!(
                xy_vsfilter_document_vtt_part_v1(vtt_run, 0, out.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_vtt_time_v1(vtt_run, 5, 1, 2, 3, 0),
                BAD_HANDLE
            );
            assert_eq!(read_snapshot(vtt_snapshot), retained);
            assert_eq!(read_snapshot(vtt_colors), colors_before);
            assert_eq!(xy_vsfilter_document_vtt_drop_v1(vtt_run), OK);
            assert_eq!(xy_vsfilter_document_vtt_drop_v1(vtt_run), BAD_HANDLE);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(vtt_snapshot), OK);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(vtt_colors), OK);
            let sami_owner = create();
            out.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_sami_begin_v1(sami_owner, 42, out.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(out, [0xa5; 16]);
            assert_eq!(
                xy_vsfilter_document_sami_begin_v1(sami_owner, 42, out.as_mut_ptr(), 8),
                OK
            );
            let sami_run = u64::from_le_bytes(out[..8].try_into().unwrap());
            let mut raw: Vec<u8> = "<sami>".encode_utf16().flat_map(u16::to_le_bytes).collect();
            let spaces = alloc::vec![0;raw.len()/2];
            assert_eq!(
                xy_vsfilter_document_sami_read_v1(
                    sami_run,
                    2,
                    raw.as_ptr(),
                    raw.len(),
                    spaces.as_ptr(),
                    spaces.len()
                ),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_sami_read_v1(
                    sami_owner,
                    0,
                    raw.as_ptr(),
                    raw.len(),
                    spaces.as_ptr(),
                    spaces.len()
                ),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_sami_read_v1(
                    sami_run,
                    0,
                    raw.as_ptr(),
                    raw.len(),
                    spaces.as_ptr(),
                    spaces.len() - 1
                ),
                INVALID_INPUT
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_sami_read_v1(
                        sami_run,
                        0,
                        raw.as_ptr(),
                        raw.len(),
                        spaces.as_ptr(),
                        spaces.len()
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                xy_vsfilter_document_sami_read_v1(
                    sami_run,
                    0,
                    raw.as_ptr(),
                    raw.len(),
                    spaces.as_ptr(),
                    spaces.len()
                ),
                OK
            );
            raw.fill(0);
            let upper: Vec<u8> = "<SAMI>".encode_utf16().flat_map(u16::to_le_bytes).collect();
            assert_eq!(
                xy_vsfilter_document_sami_upper_v1(sami_run, upper.as_ptr(), upper.len() - 1),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_sami_upper_v1(sami_run, upper.as_ptr(), upper.len() - 2),
                INVALID_INPUT
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_sami_upper_v1(
                        sami_run,
                        upper.as_ptr(),
                        upper.len()
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                xy_vsfilter_document_sami_upper_v1(sami_run, upper.as_ptr(), upper.len()),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_sami_step_v1(sami_run, 0, 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_sami_read_v1(sami_run, 1, ptr::null(), 0, ptr::null(), 0),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_sami_part_v1(sami_run, out.as_mut_ptr(), 8),
                OK
            );
            let rewind_snapshot = u64::from_le_bytes(out[..8].try_into().unwrap());
            let rewind = read_snapshot(rewind_snapshot);
            assert_eq!(rewind[0], 3);
            assert_eq!(u64::from_le_bytes(rewind[16..24].try_into().unwrap()), 42);
            assert_eq!(
                xy_vsfilter_document_sami_step_v1(sami_run, 0, 1),
                INVALID_INPUT
            );
            assert_eq!(xy_vsfilter_document_sami_step_v1(sami_run, 0, 0), OK);
            for text in ["caption\0tail", "<SYNC START=12M3>second"] {
                let raw: Vec<u8> = text.encode_utf16().flat_map(u16::to_le_bytes).collect();
                let spaces = alloc::vec![0;raw.len()/2];
                assert_eq!(
                    xy_vsfilter_document_sami_read_v1(
                        sami_run,
                        0,
                        raw.as_ptr(),
                        raw.len(),
                        spaces.as_ptr(),
                        spaces.len()
                    ),
                    OK
                );
                let upper: Vec<u8> = text
                    .to_ascii_uppercase()
                    .encode_utf16()
                    .flat_map(u16::to_le_bytes)
                    .collect();
                assert_eq!(
                    xy_vsfilter_document_sami_upper_v1(sami_run, upper.as_ptr(), upper.len()),
                    OK
                );
            }
            assert_eq!(
                xy_vsfilter_document_sami_step_v1(sami_run, 1, 2),
                INVALID_INPUT
            );
            assert_eq!(xy_vsfilter_document_sami_step_v1(sami_run, 1, 1), OK);
            assert_eq!(xy_vsfilter_document_sami_step_v1(sami_run, 1, 1), OK);
            assert_eq!(
                xy_vsfilter_document_sami_step_v1(sami_run, 1, 1),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_sami_part_v1(sami_run, out.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_sami_part_v1(sami_run, out.as_mut_ptr(), 8),
                OK
            );
            let sami_snapshot = u64::from_le_bytes(out[..8].try_into().unwrap());
            let retained = read_snapshot(sami_snapshot);
            assert_eq!(retained[0], 7);
            assert_eq!(
                i64::from_le_bytes(retained[32..40].try_into().unwrap()),
                120000
            );
            assert_eq!(
                &retained[40..],
                &"caption"
                    .encode_utf16()
                    .flat_map(u16::to_le_bytes)
                    .collect::<Vec<_>>()
            );
            assert_eq!(xy_vsfilter_document_sami_step_v1(sami_run, 2, 0), OK);
            assert_eq!(
                xy_vsfilter_document_sami_read_v1(sami_run, 1, ptr::null(), 0, ptr::null(), 0),
                OK
            );
            assert_eq!(xy_vsfilter_document_sami_step_v1(sami_run, 2, 0), OK);
            assert_eq!(
                xy_vsfilter_document_sami_part_v1(sami_run, out.as_mut_ptr(), 8),
                OK
            );
            let done_snapshot = u64::from_le_bytes(out[..8].try_into().unwrap());
            let done = read_snapshot(done_snapshot);
            assert_eq!(done[0], 8);
            assert_eq!(done[4], 1);
            assert_eq!(xy_vsfilter_document_destroy_v1(sami_owner), OK);
            assert_eq!(
                xy_vsfilter_document_sami_part_v1(sami_run, out.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(read_snapshot(sami_snapshot), retained);
            assert_eq!(xy_vsfilter_document_sami_drop_v1(sami_run), OK);
            assert_eq!(xy_vsfilter_document_sami_drop_v1(sami_run), BAD_HANDLE);
            for snapshot in [rewind_snapshot, sami_snapshot, done_snapshot] {
                assert_eq!(xy_vsfilter_document_snapshot_drop_v1(snapshot), OK);
            }
            let simple_owner = create();
            let mut simple_out = [0xa5; 4];
            assert_eq!(
                xy_vsfilter_document_get_metadata_v1(simple_owner, 5, simple_out.as_mut_ptr(), 4),
                UNRESOLVED_REFERENCE
            );
            assert_eq!(simple_out, [0xa5; 4]);
            for invalid in [-1, 2, i32::MIN, i32::MAX] {
                assert_eq!(
                    xy_vsfilter_document_set_metadata_v1(simple_owner, 5, invalid),
                    INVALID_INPUT
                );
                assert_eq!(state(simple_owner).simple(), None);
            }
            for value in [1, 0, 1] {
                assert_eq!(
                    xy_vsfilter_document_set_metadata_v1(simple_owner, 5, value),
                    OK
                );
                assert_eq!(
                    xy_vsfilter_document_get_metadata_v1(
                        simple_owner,
                        5,
                        simple_out.as_mut_ptr(),
                        4
                    ),
                    OK
                );
                assert_eq!(simple_out, value.to_le_bytes());
                simple_out.fill(0xa5);
                assert_eq!(
                    xy_vsfilter_document_get_metadata_v1(
                        simple_owner,
                        5,
                        simple_out.as_mut_ptr(),
                        3
                    ),
                    INVALID_INPUT
                );
                assert_eq!(
                    xy_vsfilter_document_get_metadata_v1(simple_owner, 5, core::ptr::null_mut(), 4),
                    INVALID_INPUT
                );
                assert_eq!(
                    DOCUMENTS
                        .try_with(|_| xy_vsfilter_document_set_metadata_v1(
                            simple_owner,
                            5,
                            1 - value
                        ))
                        .unwrap(),
                    BUSY
                );
                assert_eq!(
                    DOCUMENTS
                        .try_with(|_| xy_vsfilter_document_get_metadata_v1(
                            simple_owner,
                            5,
                            simple_out.as_mut_ptr(),
                            4
                        ))
                        .unwrap(),
                    BUSY
                );
                assert_eq!(simple_out, [0xa5; 4]);
                assert_eq!(state(simple_owner).simple(), Some(value != 0));
            }
            let simple_snapshot = open_snapshot(simple_owner, 5);
            assert_eq!(
                xy_vsfilter_document_set_metadata_v1(simple_snapshot, 5, 0),
                BAD_HANDLE
            );
            assert_eq!(xy_vsfilter_document_destroy_v1(simple_owner), OK);
            assert_eq!(
                xy_vsfilter_document_get_metadata_v1(simple_owner, 5, simple_out.as_mut_ptr(), 4),
                BAD_HANDLE
            );
            assert_eq!(simple_out, [0xa5; 4]);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(simple_snapshot), OK);
            let mut suffix_entry = wire::decode_entry(&entry(0, 10)).unwrap();
            suffix_entry.text = vec![0xd800, 13, 10, 0, 10];
            suffix_entry.style = vec![42, 42, 0, 0xdc00, 42];
            suffix_entry.readorder = 17;
            for mode in 0..=5 {
                let owner = create();
                let bytes = wire::encode_entry(&suffix_entry).unwrap();
                assert_eq!(add(owner, mode, &bytes).0, OK);
                let mut expected = suffix_entry.clone();
                if mode >= 3 {
                    expected.text = vec![0xd800, 92, 78, 0, 10, 0];
                    expected.style = vec![0, 0xdc00, 42];
                }
                assert_eq!(state(owner).entries(), &[expected]);
                assert_eq!(xy_vsfilter_document_destroy_v1(owner), OK);
            }
            let suffix_owner = create();
            suffix_entry.text = vec![13];
            suffix_entry.style = vec![42];
            assert_eq!(
                add(suffix_owner, 3, &wire::encode_entry(&suffix_entry).unwrap()).0,
                OK
            );
            assert!(state(suffix_owner).entries()[0].text.is_empty());
            assert!(state(suffix_owner).entries()[0].style.is_empty());
            suffix_entry.style = vec![];
            assert_eq!(
                add(suffix_owner, 4, &wire::encode_entry(&suffix_entry).unwrap()).0,
                OK
            );
            assert_eq!(
                state(suffix_owner).entries()[1].style,
                [68, 101, 102, 97, 117, 108, 116]
            );
            suffix_entry.text = vec![10, 10, 0, 10, 10];
            let before = state(suffix_owner);
            assert_eq!(
                add(suffix_owner, 3, &wire::encode_entry(&suffix_entry).unwrap()),
                (UNRESOLVED_REFERENCE, [0xa5; 8])
            );
            assert_eq!(state(suffix_owner), before);
            assert_eq!(xy_vsfilter_document_destroy_v1(suffix_owner), OK);
            let suffix_owner = create();
            suffix_entry.text = vec![10, 0, 10];
            suffix_entry.style = vec![42, 65];
            let mut overlap = wire::encode_entry(&suffix_entry).unwrap();
            let overlap_length = overlap.len();
            let overlap_pointer = overlap.as_mut_ptr();
            assert_eq!(
                xy_vsfilter_document_add_prepared_v1(
                    suffix_owner,
                    3,
                    overlap_pointer.cast_const(),
                    overlap_length,
                    overlap_pointer,
                    8
                ),
                OK
            );
            assert_eq!(state(suffix_owner).entries()[0].text, [92, 78, 0, 10, 0]);
            assert_eq!(state(suffix_owner).entries()[0].style, [65]);
            assert_eq!(xy_vsfilter_document_destroy_v1(suffix_owner), OK);

            let mut section_output = [0xa5u8; 8];
            for (format, section, scaled, capacity) in
                [(14, 0, 0, 8), (0, 4, 0, 8), (0, 0, 2, 8), (0, 0, 0, 7)]
            {
                assert_eq!(
                    xy_vsfilter_document_format_export_section_v1(
                        format,
                        section,
                        0,
                        0,
                        0,
                        scaled,
                        section_output.as_mut_ptr(),
                        capacity
                    ),
                    INVALID_INPUT
                );
                assert_eq!(section_output, [0xa5; 8]);
            }
            assert_eq!(
                xy_vsfilter_document_format_export_section_v1(0, 0, 0, 0, 0, 0, ptr::null_mut(), 8),
                INVALID_INPUT
            );
            let mut saved_section = None;
            for format in 0..14 {
                for section in 0..4 {
                    assert_eq!(
                        xy_vsfilter_document_format_export_section_v1(
                            format,
                            section,
                            i32::MIN,
                            i32::MAX,
                            -1,
                            1,
                            section_output.as_mut_ptr(),
                            8
                        ),
                        OK
                    );
                    let snapshot = u64::from_le_bytes(section_output);
                    let bytes = read_snapshot(snapshot);
                    assert_eq!(bytes.len() % 2, 0);
                    let units: Vec<u16> = bytes
                        .chunks_exact(2)
                        .map(|b| u16::from_le_bytes([b[0], b[1]]))
                        .collect();
                    let text = alloc::string::String::from_utf16(&units).unwrap();
                    match section {
                        0 if format == 2 => assert!(text.starts_with("<SAMI>\n<HEAD>\n")),
                        0 if format == 4 || format == 5 => {
                            assert!(text.contains("PlayResX: -2147483648\nPlayResY: 2147483647\n"));
                            assert!(text.contains("Collisions: Reverse\n"));
                            assert_eq!(text.contains("ScaledBorderAndShadow: Yes\n"), format == 5);
                        }
                        1 if format == 4 || format == 5 => {
                            assert!(text.starts_with("\n[Events]\nFormat: "))
                        }
                        2 if format == 2 => assert_eq!(text, "</BODY>\n</SAMI>\n"),
                        3 => assert!(text.starts_with(
                            "ScriptType: v4.00+\nPlayResX: -2147483648\nPlayResY: 2147483647\n"
                        )),
                        _ => assert!(text.is_empty()),
                    }
                    if format == 5 && section == 0 {
                        saved_section = Some((snapshot, bytes));
                    } else {
                        assert_eq!(xy_vsfilter_document_snapshot_drop_v1(snapshot), OK);
                    }
                }
            }
            let (snapshot, bytes) = saved_section.unwrap();
            assert_eq!(read_snapshot(snapshot), bytes);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(snapshot), OK);

            let mut export_entry = wire::decode_entry(&entry(0, 10)).unwrap();
            export_entry.text = vec![0xd800, 0, 65];
            export_entry.style = vec![83, 0, 88];
            export_entry.actor = vec![65, 0, 88];
            export_entry.effect = vec![70, 0, 88];
            export_entry.margins = [-1, 2, 3, 4];
            export_entry.layer = -1;
            let encoded = wire::encode_entry(&export_entry).unwrap();
            let mut output = [0xa5; 8];
            for (format, input, length, capacity) in [
                (14, encoded.as_ptr(), encoded.len(), 8),
                (0, encoded.as_ptr(), encoded.len(), 7),
                (0, ptr::null(), 1, 8),
                (0, ptr::null(), 0, 8),
            ] {
                assert_eq!(
                    xy_vsfilter_document_format_export_row_v1(
                        format,
                        12,
                        3723456,
                        0,
                        i32::MIN,
                        i32::MAX,
                        -3,
                        input,
                        length,
                        output.as_mut_ptr(),
                        capacity
                    ),
                    INVALID_INPUT
                );
                assert_eq!(output, [0xa5; 8]);
            }
            let mut malformed = encoded.clone();
            malformed[60] = 1;
            assert_eq!(
                xy_vsfilter_document_format_export_row_v1(
                    0,
                    12,
                    3723456,
                    0,
                    0,
                    0,
                    -3,
                    malformed.as_ptr(),
                    malformed.len(),
                    output.as_mut_ptr(),
                    8
                ),
                INVALID_INPUT
            );
            assert_eq!(output, [0xa5; 8]);
            for format in 0..14 {
                assert_eq!(
                    xy_vsfilter_document_format_export_row_v1(
                        format,
                        12,
                        3723456,
                        0,
                        i32::MIN,
                        i32::MAX,
                        -3,
                        encoded.as_ptr(),
                        encoded.len(),
                        output.as_mut_ptr(),
                        8
                    ),
                    OK
                );
                let snapshot = u64::from_le_bytes(output);
                let bytes = read_snapshot(snapshot);
                let prefix = match format {
                    0 => "12\n01:02:03,456 --> 00:00:00,000\n",
                    1 => "{-2147483648}{2147483647}",
                    2 => "<SYNC Start=3723456><P Class=UNKNOWNCC>\n",
                    3 => "{1:02:03}{0:00:00}",
                    4 => "Dialogue: Marked=0,1:02:03.45,0:00:00.00,S,A,-001,0003,-003,F,",
                    5 => "Dialogue: -1,1:02:03.45,0:00:00.00,S,A,-001,0003,-003,F,",
                    _ => "",
                };
                let mut expected: Vec<u16> = prefix.encode_utf16().collect();
                if format < 6 {
                    expected.push(0xd800);
                    expected.extend(
                        match format {
                            0 => "\n\n",
                            2 => "\n<SYNC Start=0><P Class=UNKNOWNCC>&nbsp;\n",
                            _ => "\n",
                        }
                        .encode_utf16(),
                    );
                }
                assert_eq!(bytes, raw_units_bytes(&expected).unwrap());
                assert_eq!(xy_vsfilter_document_snapshot_drop_v1(snapshot), OK);
            }
            let mut overlapping = encoded.clone();
            let overlapping_length = overlapping.len();
            let overlapping_pointer = overlapping.as_mut_ptr();
            assert_eq!(
                xy_vsfilter_document_format_export_row_v1(
                    1,
                    0,
                    0,
                    0,
                    -2,
                    3,
                    0,
                    overlapping_pointer.cast_const(),
                    overlapping_length,
                    overlapping_pointer,
                    8
                ),
                OK
            );
            let snapshot = u64::from_le_bytes(overlapping[..8].try_into().unwrap());
            overlapping.fill(0);
            let mut expected: Vec<u16> = "{-2}{3}".encode_utf16().collect();
            expected.extend([0xd800, 10]);
            assert_eq!(read_snapshot(snapshot), raw_units_bytes(&expected).unwrap());
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(snapshot), OK);

            let search_owner = create();
            assert_eq!(add(search_owner, 0, &entry(0, 10)).0, OK);
            assert_eq!(add(search_owner, 0, &entry(20, 30)).0, OK);
            let document = state(search_owner);
            let mut begin = [0xa5u8; 32];
            assert_eq!(
                xy_vsfilter_document_search_begin_v1(search_owner, 0, 0, begin.as_mut_ptr(), 31),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_search_begin_v1(search_owner, 3, 0, begin.as_mut_ptr(), 32),
                INVALID_INPUT
            );
            assert_eq!(begin, [0xa5; 32]);
            for mode in 0..3 {
                for time in [-1, 0, 5, 10, 15, 20, 30] {
                    assert_eq!(
                        xy_vsfilter_document_search_begin_v1(
                            search_owner,
                            mode,
                            time,
                            begin.as_mut_ptr(),
                            32
                        ),
                        OK
                    );
                    let id = u64::from_le_bytes(begin[..8].try_into().unwrap());
                    let mut packet = [0u8; 24];
                    packet.copy_from_slice(&begin[8..]);
                    let mut bad = [0xa5; 24];
                    let before = admitted(|registry| {
                        Ok(translated_search_bytes(
                            registry.search_pair(id).map_err(registry_error)?.1.state(),
                        ))
                    })
                    .unwrap();
                    assert_eq!(
                        xy_vsfilter_document_search_supply_v1(id, 999, bad.as_mut_ptr(), 23),
                        INVALID_INPUT
                    );
                    assert_eq!(bad, [0xa5; 24]);
                    assert_eq!(
                        admitted(|registry| Ok(translated_search_bytes(
                            registry.search_pair(id).map_err(registry_error)?.1.state()
                        )))
                        .unwrap(),
                        before
                    );
                    assert_eq!(
                        xy_vsfilter_document_search_supply_v1(
                            search_owner,
                            0,
                            bad.as_mut_ptr(),
                            24
                        ),
                        BAD_HANDLE
                    );
                    while u32::from_le_bytes(packet[..4].try_into().unwrap()) != 0 {
                        let index = i32::from_le_bytes(packet[8..12].try_into().unwrap()) as usize;
                        let start = u32::from_le_bytes(packet[4..8].try_into().unwrap()) != 0;
                        let value = if mode == 0 {
                            document.entries()[index].start
                        } else if start {
                            document.segments()[index].start
                        } else {
                            document.segments()[index].end
                        };
                        assert_eq!(
                            xy_vsfilter_document_search_supply_v1(
                                id,
                                value,
                                packet.as_mut_ptr(),
                                24
                            ),
                            OK
                        );
                    }
                    let query = document
                        .query_time(
                            match mode {
                                0 => Mode::Entry,
                                1 => Mode::NextSegment,
                                _ => Mode::ContainingSegment,
                            },
                            time,
                        )
                        .unwrap();
                    assert_eq!(
                        i32::from_le_bytes(packet[12..16].try_into().unwrap()),
                        query.index
                    );
                    assert_eq!(
                        u32::from_le_bytes(packet[16..20].try_into().unwrap()) != 0,
                        query.found
                    );
                    assert_eq!(
                        u32::from_le_bytes(packet[20..].try_into().unwrap()),
                        query.count as u32
                    );
                    assert_eq!(
                        xy_vsfilter_document_search_supply_v1(id, 0, bad.as_mut_ptr(), 24),
                        INVALID_INPUT
                    );
                    assert_eq!(bad, [0xa5; 24]);
                    assert_eq!(xy_vsfilter_document_search_drop_v1(id), OK);
                }
            }
            assert_eq!(state(search_owner), document);
            let empty_search_owner = create();
            for mode in 0..3 {
                assert_eq!(
                    xy_vsfilter_document_search_begin_v1(
                        empty_search_owner,
                        mode,
                        0,
                        begin.as_mut_ptr(),
                        32
                    ),
                    OK
                );
                let id = u64::from_le_bytes(begin[..8].try_into().unwrap());
                assert_eq!(&begin[8..20], &[0u8; 12]);
                assert_eq!(&begin[24..], &[0u8; 8]);
                assert_eq!(
                    i32::from_le_bytes(begin[20..24].try_into().unwrap()),
                    if mode == 0 { -1 } else { 0 }
                );
                assert_eq!(xy_vsfilter_document_search_drop_v1(id), OK);
            }
            assert_eq!(xy_vsfilter_document_destroy_v1(empty_search_owner), OK);
            assert_eq!(
                xy_vsfilter_document_search_begin_v1(search_owner, 0, 5, begin.as_mut_ptr(), 32),
                OK
            );
            let search_run = u64::from_le_bytes(begin[..8].try_into().unwrap());
            assert_eq!(xy_vsfilter_document_remove_entries_v1(search_owner), OK);
            let frozen = admitted(|registry| {
                Ok(registry
                    .search_pair(search_run)
                    .map_err(registry_error)?
                    .1
                    .state()
                    .count)
            })
            .unwrap();
            assert_eq!(frozen, 2);
            assert_eq!(xy_vsfilter_document_destroy_v1(search_owner), OK);
            let mut bad = [0xa5; 24];
            assert_eq!(
                xy_vsfilter_document_search_supply_v1(search_run, 0, bad.as_mut_ptr(), 24),
                BAD_HANDLE
            );
            assert_eq!(bad, [0xa5; 24]);
            assert_eq!(xy_vsfilter_document_search_drop_v1(search_run), OK);
            assert_eq!(xy_vsfilter_document_search_drop_v1(search_run), BAD_HANDLE);

            let open_owner = create();
            let mut output = [0xa5; 8];
            let before = state(open_owner);
            assert_eq!(
                xy_vsfilter_document_open_lifecycle_begin_v1(open_owner, 1, output.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_open_lifecycle_begin_v1(
                    open_owner,
                    i32::MAX as u32 + 1,
                    output.as_mut_ptr(),
                    8
                ),
                COUNT_OVERFLOW
            );
            assert_eq!(output, [0xa5; 8]);
            assert_eq!(state(open_owner), before);
            assert_eq!(
                xy_vsfilter_document_open_lifecycle_begin_v1(open_owner, 1, output.as_mut_ptr(), 8),
                OK
            );
            let open_run = u64::from_le_bytes(output);
            assert_eq!(state(open_owner), before);
            let peek = |id| {
                let mut bytes = [0u8; 8];
                assert_eq!(
                    xy_vsfilter_document_open_lifecycle_step_v1(id, -2, bytes.as_mut_ptr(), 8),
                    OK
                );
                bytes
            };
            assert_eq!(peek(open_run), [0; 8]);
            let mut sentinel = [0xa5; 8];
            assert_eq!(
                xy_vsfilter_document_open_lifecycle_step_v1(open_run, 2, sentinel.as_mut_ptr(), 8),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_open_lifecycle_step_v1(open_run, 0, sentinel.as_mut_ptr(), 8),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_open_lifecycle_step_v1(open_run, -1, sentinel.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(sentinel, [0xa5; 8]);
            assert_eq!(peek(open_run), [0; 8]);
            assert_eq!(
                xy_vsfilter_document_open_lifecycle_step_v1(
                    open_owner,
                    -2,
                    sentinel.as_mut_ptr(),
                    8
                ),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_open_lifecycle_begin_v1(open_run, 1, sentinel.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            for (ack, kind) in [(-1, 1u32), (-1, 2), (1, 7), (-1, 8), (-1, 9), (-1, 10)] {
                assert_eq!(
                    xy_vsfilter_document_open_lifecycle_step_v1(
                        open_run,
                        ack,
                        output.as_mut_ptr(),
                        8
                    ),
                    OK
                );
                assert_eq!(&output[..4], &kind.to_le_bytes());
                assert_eq!(&output[4..], &0u32.to_le_bytes());
                if kind == 2 {
                    assert_eq!(
                        xy_vsfilter_document_open_lifecycle_step_v1(
                            open_run,
                            -1,
                            sentinel.as_mut_ptr(),
                            8
                        ),
                        INVALID_INPUT
                    );
                }
            }
            assert_eq!(
                xy_vsfilter_document_open_lifecycle_step_v1(open_run, -1, sentinel.as_mut_ptr(), 8),
                UNRESOLVED_REFERENCE
            );
            assert_eq!(sentinel, [0xa5; 8]);
            let mut after_header = before.clone();
            after_header.replace_simple(false);
            assert_eq!(state(open_owner), after_header);
            assert_eq!(&peek(open_run)[..4], &10u32.to_le_bytes());
            assert_eq!(
                xy_vsfilter_document_set_script_size_v1(open_owner, 0, 0),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_open_lifecycle_step_v1(open_run, -1, output.as_mut_ptr(), 8),
                OK
            );
            assert_eq!(&output[..4], &12u32.to_le_bytes());
            assert_eq!(state(open_owner).script_size(), Some([384, 288]));
            assert_eq!(
                xy_vsfilter_document_open_lifecycle_step_v1(open_run, -1, sentinel.as_mut_ptr(), 8),
                INVALID_INPUT
            );
            assert_eq!(&peek(open_run)[..4], &12u32.to_le_bytes());
            assert_eq!(xy_vsfilter_document_destroy_v1(open_owner), OK);
            assert_eq!(
                xy_vsfilter_document_open_lifecycle_step_v1(open_run, -2, sentinel.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(xy_vsfilter_document_open_lifecycle_drop_v1(open_run), OK);
            assert_eq!(
                xy_vsfilter_document_open_lifecycle_drop_v1(open_run),
                BAD_HANDLE
            );

            let write_owner = create();
            assert_eq!(add(write_owner, 0, &entry(0, 10)).0, OK);
            let raw = [13u8, 0, 10, 0, 0, 0, 10, 0];
            let mut output = [0xa5; 8];
            let before = state(write_owner);
            assert_eq!(
                xy_vsfilter_document_set_text_plan_v1(
                    write_owner,
                    0,
                    0,
                    raw.as_ptr(),
                    raw.len(),
                    output.as_mut_ptr(),
                    7
                ),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_set_text_plan_v1(
                    write_owner,
                    0,
                    2,
                    raw.as_ptr(),
                    raw.len(),
                    output.as_mut_ptr(),
                    8
                ),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_set_text_plan_v1(
                    write_owner,
                    0,
                    0,
                    raw.as_ptr(),
                    7,
                    output.as_mut_ptr(),
                    8
                ),
                INVALID_INPUT
            );
            assert_eq!(output, [0xa5; 8]);
            assert_eq!(state(write_owner), before);
            let mut overlapping = raw;
            let overlapping_length = overlapping.len();
            let overlapping_pointer = overlapping.as_mut_ptr();
            assert_eq!(
                xy_vsfilter_document_set_text_plan_v1(
                    write_owner,
                    0,
                    0,
                    overlapping_pointer.cast_const(),
                    overlapping_length,
                    overlapping_pointer,
                    8
                ),
                OK
            );
            let saved = u64::from_le_bytes(overlapping);
            let packet = read_snapshot(saved);
            assert_eq!(&packet[..24], &[0u8; 24]);
            assert_eq!(&packet[24..32], &6u64.to_le_bytes());
            assert_eq!(&packet[32..], &[13, 0, 92, 0, 78, 0, 0, 0, 10, 0, 0, 0]);
            assert_eq!(state(write_owner), before);
            assert_eq!(
                xy_vsfilter_document_set_text_plan_v1(
                    saved,
                    0,
                    0,
                    raw.as_ptr(),
                    raw.len(),
                    output.as_mut_ptr(),
                    8
                ),
                BAD_HANDLE
            );
            let unknown_tail = [10u8, 0, 10, 0, 0, 0, 10, 0, 10, 0];
            assert_eq!(
                xy_vsfilter_document_set_text_plan_v1(
                    write_owner,
                    9,
                    0,
                    unknown_tail.as_ptr(),
                    unknown_tail.len(),
                    output.as_mut_ptr(),
                    8
                ),
                OUT_OF_RANGE
            );
            assert_eq!(
                xy_vsfilter_document_set_text_plan_v1(
                    write_owner,
                    0,
                    0,
                    unknown_tail.as_ptr(),
                    unknown_tail.len(),
                    output.as_mut_ptr(),
                    8
                ),
                UNRESOLVED_REFERENCE
            );
            assert_eq!(state(write_owner), before);
            assert_eq!(output, [0xa5; 8]);
            assert_eq!(
                xy_vsfilter_document_set_text_plan_v1(
                    write_owner,
                    0,
                    1,
                    raw.as_ptr(),
                    raw.len(),
                    output.as_mut_ptr(),
                    8
                ),
                UNRESOLVED_REFERENCE
            );
            assert_eq!(xy_vsfilter_document_set_metadata_v1(write_owner, 3, 0), OK);
            let null_key = [0u8; 2];
            assert_eq!(
                xy_vsfilter_document_named_style_put_v1(
                    write_owner,
                    null_key.as_ptr(),
                    2,
                    ptr::null(),
                    0
                ),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_set_text_plan_v1(
                    write_owner,
                    0,
                    1,
                    raw.as_ptr(),
                    raw.len(),
                    output.as_mut_ptr(),
                    8
                ),
                OK
            );
            let reply = u64::from_le_bytes(output);
            let converted = read_snapshot(reply);
            assert_eq!(&converted[..4], &2u32.to_le_bytes());
            assert_eq!(&converted[4..8], &0u32.to_le_bytes());
            assert_eq!(&converted[16..20], &(-1i32).to_le_bytes());
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(reply), OK);
            assert_eq!(
                xy_vsfilter_document_set_prepared_text_v1(
                    write_owner,
                    0,
                    -1,
                    packet[32..].as_ptr(),
                    packet.len() - 32
                ),
                OK
            );
            assert!(!state(write_owner).entries()[0].unicode);
            assert_eq!(
                xy_vsfilter_document_set_prepared_text_v1(
                    write_owner,
                    0,
                    1,
                    packet[32..].as_ptr(),
                    packet.len() - 32
                ),
                OK
            );
            let surrogate_input = [0u8, 0xd8, 13, 0, 0, 0xdc, 10, 0];
            let before = state(write_owner);
            assert_eq!(
                xy_vsfilter_document_set_text_plan_v1(
                    write_owner,
                    0,
                    0,
                    surrogate_input.as_ptr(),
                    surrogate_input.len(),
                    output.as_mut_ptr(),
                    8
                ),
                OK
            );
            let reply = u64::from_le_bytes(output);
            let wide = read_snapshot(reply);
            assert_eq!(&wide[..4], &1u32.to_le_bytes());
            assert_eq!(&wide[4..8], &1u32.to_le_bytes());
            assert_eq!(&wide[32..], &[0, 0xd8, 13, 0, 0, 0xdc, 92, 0, 78, 0]);
            assert_eq!(state(write_owner), before);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(reply), OK);

            assert_eq!(xy_vsfilter_document_destroy_v1(write_owner), OK);
            assert_eq!(read_snapshot(saved), packet);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(saved), OK);

            let text_owner = create();
            assert_eq!(add(text_owner, 0, &entry(0, 10)).0, OK);
            let mut output = [0xa5u8; 8];
            assert_eq!(
                xy_vsfilter_document_text_plan_v1(text_owner, 0, 0, output.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_text_plan_v1(text_owner, 0, 6, output.as_mut_ptr(), 8),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_text_plan_v1(text_owner, 9, 5, output.as_mut_ptr(), 8),
                OUT_OF_RANGE
            );
            assert_eq!(output, [0xa5; 8]);
            // Same-target conversion never resolves unsupplied style/forced state.
            assert_eq!(
                xy_vsfilter_document_text_plan_v1(text_owner, 0, 5, output.as_mut_ptr(), 8),
                OK
            );
            let no_op = u64::from_le_bytes(output);
            let bytes = read_snapshot(no_op);
            assert_eq!(bytes.len(), 32);
            assert_eq!(&bytes[..4], &3u32.to_le_bytes());
            assert_eq!(&bytes[12..], &[0u8; 20]);
            assert_eq!(
                xy_vsfilter_document_text_plan_v1(no_op, 0, 5, output.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(no_op), OK);
            assert_eq!(xy_vsfilter_document_set_metadata_v1(text_owner, 3, 0), OK);
            let null_key = [0u8; 2];
            assert_eq!(
                xy_vsfilter_document_named_style_put_v1(
                    text_owner,
                    null_key.as_ptr(),
                    2,
                    ptr::null(),
                    0
                ),
                OK
            );
            let before = state(text_owner);
            let mut saved = None;
            for (mode, action) in [(0, 1u32), (1, 1), (2, 0), (3, 0), (4, 1), (5, 3)] {
                assert_eq!(
                    xy_vsfilter_document_text_plan_v1(text_owner, 0, mode, output.as_mut_ptr(), 8),
                    OK
                );
                let reply = u64::from_le_bytes(output);
                let bytes = read_snapshot(reply);
                assert_eq!(&bytes[..4], &action.to_le_bytes());
                assert_eq!(&bytes[12..16], &0u32.to_le_bytes());
                if action != 3 {
                    assert_eq!(&bytes[16..20], &(-1i32).to_le_bytes());
                    assert_eq!(&bytes[32..], &[0, 0, 0, 0xd8, 0xff, 0xff]);
                }
                assert_eq!(state(text_owner), before);
                if mode == 0 {
                    saved = Some((reply, bytes));
                } else {
                    assert_eq!(xy_vsfilter_document_snapshot_drop_v1(reply), OK);
                }
            }
            let replacement = [65u8, 0, 0, 0, 0, 0xdc];
            assert_eq!(
                xy_vsfilter_document_set_prepared_text_v1(
                    text_owner,
                    9,
                    1,
                    replacement.as_ptr(),
                    replacement.len()
                ),
                OUT_OF_RANGE
            );
            assert_eq!(state(text_owner), before);
            assert_eq!(
                xy_vsfilter_document_set_prepared_text_v1(
                    text_owner,
                    0,
                    1,
                    replacement.as_ptr(),
                    replacement.len()
                ),
                OK
            );
            assert_eq!(state(text_owner).entries()[0].text, [65, 0, 0xdc00]);
            assert_eq!(xy_vsfilter_document_destroy_v1(text_owner), OK);
            let (reply, bytes) = saved.unwrap();
            assert_eq!(read_snapshot(reply), bytes);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(reply), OK);

            let copy_source = create();
            let copy_destination = create();
            let key = [83u8, 0];
            let old_key = [79u8, 0];
            let mut style = alloc::vec![0u8;style_wire::HEADER_BYTES];
            assert_eq!(
                xy_vsfilter_document_named_style_put_v1(
                    copy_source,
                    key.as_ptr(),
                    2,
                    style.as_ptr(),
                    style.len()
                ),
                OK
            );
            style[0] = 1;
            assert_eq!(
                xy_vsfilter_document_named_style_put_v1(
                    copy_destination,
                    old_key.as_ptr(),
                    2,
                    style.as_ptr(),
                    style.len()
                ),
                OK
            );
            let mut handles = [0xa5u8; 8];
            let before = state(copy_destination);
            assert_eq!(
                xy_vsfilter_document_copy_styles_open_v1(
                    copy_destination,
                    copy_source,
                    0,
                    handles.as_mut_ptr(),
                    7
                ),
                INVALID_INPUT
            );
            assert_eq!(handles, [0xa5; 8]);
            assert_eq!(
                xy_vsfilter_document_copy_styles_open_v1(
                    copy_destination,
                    copy_source,
                    2,
                    handles.as_mut_ptr(),
                    8
                ),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_copy_styles_open_v1(
                    copy_destination,
                    copy_source,
                    0,
                    handles.as_mut_ptr(),
                    8
                ),
                OK
            );
            let copy_run = u64::from_le_bytes(handles);
            assert_eq!(state(copy_destination), before);
            assert_eq!(
                xy_vsfilter_document_copy_styles_step_v1(copy_run, handles.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(state(copy_destination), before);
            let copy_step = |run| {
                let mut output = [0u8; 8];
                assert_eq!(
                    xy_vsfilter_document_copy_styles_step_v1(run, output.as_mut_ptr(), 8),
                    OK
                );
                let reply = u64::from_le_bytes(output);
                let bytes = read_snapshot(reply);
                assert_eq!(xy_vsfilter_document_snapshot_drop_v1(reply), OK);
                (u32::from_le_bytes(bytes[..4].try_into().unwrap()), bytes)
            };
            assert_eq!(copy_step(copy_run).0, 1);
            assert_eq!(state(copy_destination).named_styles().count(), 0);
            assert_eq!(copy_step(copy_run).0, 1);
            assert_eq!(
                state(copy_destination).named_styles(),
                state(copy_source).named_styles()
            );
            assert_eq!(copy_step(copy_run).0, 0);
            assert_eq!(xy_vsfilter_document_copy_styles_drop_v1(copy_run), OK);
            // Append conflict stays entirely owned; only the native-selected name crosses.
            style[0] = 1;
            assert_eq!(
                xy_vsfilter_document_named_style_put_v1(
                    copy_destination,
                    key.as_ptr(),
                    2,
                    style.as_ptr(),
                    style.len()
                ),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_copy_styles_open_v1(
                    copy_destination,
                    copy_source,
                    1,
                    handles.as_mut_ptr(),
                    8
                ),
                OK
            );
            let copy_run = u64::from_le_bytes(handles);
            assert_eq!(copy_step(copy_run).0, 1);
            let (action, packet) = copy_step(copy_run);
            assert_eq!(action, 2);
            assert_eq!(&packet[16..], &key);
            let before = state(copy_destination);
            assert_eq!(
                xy_vsfilter_document_copy_styles_step_v1(copy_run, handles.as_mut_ptr(), 8),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_copy_styles_submit_v1(
                    copy_run,
                    key.as_ptr(),
                    2,
                    handles.as_mut_ptr(),
                    8
                ),
                OK
            );
            assert_eq!(handles, 0u64.to_le_bytes());
            assert_eq!(state(copy_destination), before);
            let mut overlap = [88u8, 0, 0, 0, 0, 0, 0, 0];
            let overlap_length = overlap.len();
            let overlap_pointer = overlap.as_mut_ptr();
            assert_eq!(
                xy_vsfilter_document_copy_styles_submit_v1(
                    copy_run,
                    overlap_pointer.cast_const(),
                    overlap_length,
                    overlap_pointer,
                    8
                ),
                OK
            );
            assert_eq!(overlap, 1u64.to_le_bytes());
            assert_eq!(
                xy_vsfilter_document_copy_styles_submit_v1(
                    copy_run,
                    key.as_ptr(),
                    2,
                    handles.as_mut_ptr(),
                    8
                ),
                INVALID_INPUT
            );
            assert_eq!(copy_step(copy_run).0, 0);
            assert_eq!(xy_vsfilter_document_copy_styles_drop_v1(copy_run), OK);
            // Self replacement reads source only after clearing the same canonical map.
            assert_eq!(
                xy_vsfilter_document_copy_styles_open_v1(
                    copy_destination,
                    copy_destination,
                    0,
                    handles.as_mut_ptr(),
                    8
                ),
                OK
            );
            let copy_run = u64::from_le_bytes(handles);
            assert_eq!(copy_step(copy_run).0, 1);
            assert_eq!(copy_step(copy_run).0, 0);
            assert_eq!(state(copy_destination).named_styles().count(), 0);
            assert_eq!(
                xy_vsfilter_document_copy_styles_step_v1(copy_destination, handles.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_copy_styles_open_v1(
                    copy_destination,
                    copy_run,
                    0,
                    handles.as_mut_ptr(),
                    8
                ),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_copy_styles_open_v1(
                    copy_destination,
                    copy_source,
                    1,
                    handles.as_mut_ptr(),
                    8
                ),
                OK
            );
            let source_bound_run = u64::from_le_bytes(handles);
            assert_eq!(xy_vsfilter_document_destroy_v1(copy_source), OK);
            assert_eq!(
                xy_vsfilter_document_copy_styles_step_v1(source_bound_run, handles.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_copy_styles_drop_v1(source_bound_run),
                OK
            );
            assert_eq!(xy_vsfilter_document_destroy_v1(copy_destination), OK);
            assert_eq!(
                xy_vsfilter_document_copy_styles_step_v1(copy_run, handles.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(xy_vsfilter_document_copy_styles_drop_v1(copy_run), OK);
            assert_eq!(
                xy_vsfilter_document_copy_styles_drop_v1(copy_run),
                BAD_HANDLE
            );

            let style_owner = create();
            let name = [83u8, 0];
            let mut style = alloc::vec![0u8; style_wire::HEADER_BYTES];
            let mut result = [0xa5; 16];
            let before = state(style_owner);
            assert_eq!(
                xy_vsfilter_document_add_style_begin_v1(
                    style_owner,
                    name.as_ptr(),
                    2,
                    style.as_ptr(),
                    style.len(),
                    result.as_mut_ptr(),
                    15
                ),
                INVALID_INPUT
            );
            assert_eq!(state(style_owner), before);
            assert_eq!(result, [0xa5; 16]);
            for (iteration, expected) in [0u32, 0, 1].into_iter().enumerate() {
                assert_eq!(
                    xy_vsfilter_document_add_style_begin_v1(
                        style_owner,
                        name.as_ptr(),
                        2,
                        style.as_ptr(),
                        style.len(),
                        result.as_mut_ptr(),
                        16
                    ),
                    OK
                );
                let snapshot = u64::from_le_bytes(result[..8].try_into().unwrap());
                let packet = read_snapshot(snapshot);
                assert_eq!(
                    u32::from_le_bytes(packet[..4].try_into().unwrap()),
                    expected
                );
                assert_eq!(&packet[24..], &name);
                let run = u64::from_le_bytes(packet[8..16].try_into().unwrap());
                assert_eq!(&result[8..], &run.to_le_bytes());
                assert_eq!(xy_vsfilter_document_snapshot_drop_v1(snapshot), OK);
                if expected == 1 {
                    let before = state(style_owner);
                    assert_eq!(
                        xy_vsfilter_document_add_style_submit_v1(
                            run,
                            name.as_ptr(),
                            2,
                            result.as_mut_ptr(),
                            7
                        ),
                        INVALID_INPUT
                    );
                    assert_eq!(state(style_owner), before);
                    assert_eq!(
                        xy_vsfilter_document_add_style_submit_v1(
                            run,
                            name.as_ptr(),
                            2,
                            result.as_mut_ptr(),
                            8
                        ),
                        OK
                    );
                    assert_eq!(&result[..8], &0u64.to_le_bytes());
                    assert_eq!(state(style_owner), before);
                    let mut overlapping = [88u8, 0, 0, 0, 0, 0, 0, 0];
                    let overlapping_length = overlapping.len();
                    let overlapping_pointer = overlapping.as_mut_ptr();
                    assert_eq!(
                        xy_vsfilter_document_add_style_submit_v1(
                            run,
                            overlapping_pointer.cast_const(),
                            overlapping_length,
                            overlapping_pointer,
                            8
                        ),
                        OK
                    );
                    assert_eq!(overlapping, 1u64.to_le_bytes());
                    assert_eq!(
                        xy_vsfilter_document_add_style_submit_v1(
                            run,
                            name.as_ptr(),
                            2,
                            result.as_mut_ptr(),
                            8
                        ),
                        INVALID_INPUT
                    );
                    assert_eq!(xy_vsfilter_document_add_style_drop_v1(run), OK);
                }
                if iteration == 1 {
                    style[0] = 1;
                }
            }
            style[0] = 0;
            assert_eq!(
                xy_vsfilter_document_add_style_begin_v1(
                    style_owner,
                    name.as_ptr(),
                    2,
                    style.as_ptr(),
                    style.len(),
                    result.as_mut_ptr(),
                    16
                ),
                OK
            );
            let reply = u64::from_le_bytes(result[..8].try_into().unwrap());
            let stale_run = u64::from_le_bytes(result[8..].try_into().unwrap());
            assert_ne!(stale_run, 0);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(reply), OK);
            assert_eq!(
                xy_vsfilter_document_add_style_submit_v1(
                    style_owner,
                    name.as_ptr(),
                    2,
                    result.as_mut_ptr(),
                    8
                ),
                BAD_HANDLE
            );
            assert_eq!(xy_vsfilter_document_destroy_v1(style_owner), OK);
            assert_eq!(
                xy_vsfilter_document_add_style_submit_v1(
                    stale_run,
                    name.as_ptr(),
                    2,
                    result.as_mut_ptr(),
                    8
                ),
                BAD_HANDLE
            );
            assert_eq!(xy_vsfilter_document_add_style_drop_v1(stale_run), OK);
            assert_eq!(
                xy_vsfilter_document_add_style_drop_v1(stale_run),
                BAD_HANDLE
            );

            let unknown_owner = create();
            assert_eq!(add(unknown_owner, 0, &entry(0, 10)).0, OK);
            let mut result = [0xa5; 8];
            assert_eq!(
                xy_vsfilter_document_unknown_open_v1(unknown_owner, result.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(result, [0xa5; 8]);
            assert_eq!(
                xy_vsfilter_document_unknown_open_v1(unknown_owner, result.as_mut_ptr(), 8),
                OK
            );
            let run = u64::from_le_bytes(result);
            assert_eq!(
                xy_vsfilter_document_unknown_step_v1(run, result.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_unknown_resume_v1(run, 2),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_unknown_step_v1(unknown_owner, result.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_unknown_step_v1(run, result.as_mut_ptr(), 8),
                OK
            );
            let reply = u64::from_le_bytes(result);
            let packet = read_snapshot(reply);
            assert_eq!(u32::from_le_bytes(packet[..4].try_into().unwrap()), 2);
            assert_eq!(
                xy_vsfilter_document_unknown_step_v1(run, result.as_mut_ptr(), 8),
                INVALID_INPUT
            );
            assert_eq!(xy_vsfilter_document_unknown_resume_v1(run, 1), OK);
            assert_eq!(
                xy_vsfilter_document_unknown_resume_v1(run, 1),
                INVALID_INPUT
            );
            assert_eq!(xy_vsfilter_document_destroy_v1(unknown_owner), OK);
            assert_eq!(
                xy_vsfilter_document_unknown_step_v1(run, result.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(read_snapshot(reply), packet);
            assert_eq!(xy_vsfilter_document_unknown_drop_v1(run), OK);
            assert_eq!(xy_vsfilter_document_unknown_drop_v1(run), BAD_HANDLE);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(reply), OK);

            // Coarse time commit and immutable scalar snapshots, serialized with
            // every other global registry export check in this single test.
            let conversion_owner = create();
            let mut applied = [0xa5; 16];
            assert_eq!(
                xy_vsfilter_document_commit_time_conversion_v1(
                    conversion_owner,
                    0,
                    ptr::null(),
                    0,
                    applied.as_mut_ptr(),
                    16
                ),
                UNRESOLVED_REFERENCE
            );
            assert_eq!(applied, [0xa5; 16]);
            assert_eq!(
                xy_vsfilter_document_set_interpretation_v1(conversion_owner, 0, 1),
                OK
            );
            assert_eq!(add(conversion_owner, 0, &entry(0, 10)).0, OK);
            assert_eq!(
                xy_vsfilter_document_animation_or_v1(conversion_owner, 0, 1),
                OK
            );
            let time_snapshot = open_snapshot(conversion_owner, 11);
            let snapshot_bytes = read_snapshot(time_snapshot);
            assert_eq!(
                wire::decode_time_snapshot(&snapshot_bytes).unwrap(),
                [(0, 10)]
            );
            let before = state(conversion_owner);
            let mut pairs = Vec::new();
            pairs.extend_from_slice(&2i64.to_le_bytes());
            pairs.extend_from_slice(&8i64.to_le_bytes());
            for (target, length, capacity, expected) in [
                (0, 16, 7, INVALID_INPUT),
                (2, 16, 16, INVALID_INPUT),
                (0, 15, 16, INVALID_INPUT),
                (0, 0, 16, INVALID_INPUT),
            ] {
                assert_eq!(
                    xy_vsfilter_document_commit_time_conversion_v1(
                        conversion_owner,
                        target,
                        pairs.as_ptr(),
                        length,
                        applied.as_mut_ptr(),
                        capacity
                    ),
                    expected
                );
                assert_eq!(state(conversion_owner), before);
                assert_eq!(applied, [0xa5; 16]);
            }
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_commit_time_conversion_v1(
                        conversion_owner,
                        0,
                        pairs.as_ptr(),
                        16,
                        applied.as_mut_ptr(),
                        16
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                xy_vsfilter_document_commit_time_conversion_v1(
                    time_snapshot,
                    0,
                    pairs.as_ptr(),
                    16,
                    applied.as_mut_ptr(),
                    16
                ),
                BAD_HANDLE
            );
            assert_eq!(state(conversion_owner), before);
            assert_eq!(applied, [0xa5; 16]);
            // Overlapping input/output: input copied completely before flag write.
            let pairs_pointer = pairs.as_mut_ptr();
            assert_eq!(
                xy_vsfilter_document_commit_time_conversion_v1(
                    conversion_owner,
                    0,
                    pairs_pointer.cast_const(),
                    16,
                    pairs_pointer,
                    8
                ),
                OK
            );
            assert_eq!(&pairs[..8], &1u64.to_le_bytes());
            let converted = state(conversion_owner);
            let mut expected = before.clone();
            expected
                .commit_time_conversion(ConversionMode::Time, &[(2, 8)])
                .unwrap();
            assert_eq!(converted, expected);
            assert_eq!(read_snapshot(time_snapshot), snapshot_bytes);
            assert_eq!(
                xy_vsfilter_document_animation_or_v1(conversion_owner, 0, 1),
                OK
            );
            assert_eq!(xy_vsfilter_document_remove_entries_v1(conversion_owner), OK);
            let stale = state(conversion_owner);
            assert_eq!(
                xy_vsfilter_document_commit_time_conversion_v1(
                    conversion_owner,
                    0,
                    ptr::null(),
                    0,
                    applied.as_mut_ptr(),
                    16
                ),
                OK
            );
            assert_eq!(&applied[..8], &0u64.to_le_bytes());
            assert_eq!(&applied[8..], &[0xa5; 8]);
            assert_eq!(state(conversion_owner), stale);
            assert_eq!(
                xy_vsfilter_document_commit_time_conversion_v1(
                    conversion_owner,
                    1,
                    ptr::null(),
                    0,
                    applied.as_mut_ptr(),
                    16
                ),
                OK
            );
            assert_eq!(&applied[..8], &1u64.to_le_bytes());
            assert!(state(conversion_owner).segments().is_empty());
            applied.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(
                    conversion_owner,
                    11,
                    1,
                    applied.as_mut_ptr(),
                    8
                ),
                OUT_OF_RANGE
            );
            assert_eq!(applied, [0xa5; 16]);
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(
                    conversion_owner,
                    11,
                    0,
                    applied.as_mut_ptr(),
                    7
                ),
                INVALID_INPUT
            );
            assert_eq!(applied, [0xa5; 16]);
            assert_eq!(xy_vsfilter_document_destroy_v1(conversion_owner), OK);
            assert_eq!(read_snapshot(time_snapshot), snapshot_bytes);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(time_snapshot), OK);

            // Separate supplied layout axes, atomic pair snapshots and retained ownership.
            let layout_owner = create();
            let layout_destination = create();
            let mut layout_output = [0xa5; 8];
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(
                    layout_owner,
                    10,
                    0,
                    layout_output.as_mut_ptr(),
                    8
                ),
                UNRESOLVED_REFERENCE
            );
            assert_eq!(
                xy_vsfilter_document_get_script_field_v1(
                    layout_owner,
                    5,
                    layout_output.as_mut_ptr(),
                    4
                ),
                UNRESOLVED_REFERENCE
            );
            assert_eq!(
                xy_vsfilter_document_set_script_field_v1(layout_owner, 5, i32::MIN),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(
                    layout_owner,
                    10,
                    0,
                    layout_output.as_mut_ptr(),
                    8
                ),
                UNRESOLVED_REFERENCE
            );
            assert_eq!(layout_output, [0xa5; 8]);
            assert_eq!(
                xy_vsfilter_document_set_script_field_v1(layout_owner, 6, i32::MAX),
                OK
            );
            let layout_snapshot = open_snapshot(layout_owner, 10);
            let layout_bytes = [0, 0, 0, 128, 255, 255, 255, 127];
            assert_eq!(read_snapshot(layout_snapshot), layout_bytes);
            assert_eq!(
                xy_vsfilter_document_get_script_field_v1(
                    layout_owner,
                    6,
                    layout_output.as_mut_ptr(),
                    4
                ),
                OK
            );
            assert_eq!(&layout_output[..4], &i32::MAX.to_le_bytes());
            layout_output.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(
                    layout_owner,
                    10,
                    1,
                    layout_output.as_mut_ptr(),
                    8
                ),
                OUT_OF_RANGE
            );
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(
                    layout_owner,
                    10,
                    0,
                    layout_output.as_mut_ptr(),
                    7
                ),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_set_layout_size_v1(layout_snapshot, 0, 0),
                BAD_HANDLE
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_set_layout_size_v1(layout_owner, 0, 0))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_snapshot_open_v1(
                        layout_owner,
                        10,
                        0,
                        layout_output.as_mut_ptr(),
                        8
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(layout_output, [0xa5; 8]);
            assert_eq!(
                xy_vsfilter_document_set_layout_size_v1(layout_destination, -7, 11),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_copy_selected_state_v1(layout_destination, layout_owner),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_copy_storage_v1(layout_destination, layout_owner),
                OK
            );
            assert_eq!(xy_vsfilter_document_empty_v1(layout_destination), OK);
            assert_eq!(state(layout_destination).layout_size(), Some([-7, 11]));
            assert_eq!(
                xy_vsfilter_document_set_layout_size_v1(layout_owner, 3, -4),
                OK
            );
            assert_eq!(state(layout_owner).layout_size(), Some([3, -4]));
            assert_eq!(xy_vsfilter_document_destroy_v1(layout_owner), OK);
            assert_eq!(read_snapshot(layout_snapshot), layout_bytes);
            assert_eq!(
                xy_vsfilter_document_set_layout_size_v1(layout_owner, 0, 0),
                BAD_HANDLE
            );
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(layout_snapshot), OK);
            assert_eq!(xy_vsfilter_document_destroy_v1(layout_destination), OK);

            // Resolved snapshots own selected values, not live entry/map references.
            let resolved_owner = create();
            let mut resolved_output = [0xa5; 8];
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(
                    resolved_owner,
                    8,
                    0,
                    resolved_output.as_mut_ptr(),
                    8
                ),
                UNRESOLVED_REFERENCE
            );
            assert_eq!(resolved_output, [0xa5; 8]);
            let mut selected = style_wire::decode(&[0; style_wire::HEADER_BYTES]).unwrap();
            selected.char_set = -1;
            DOCUMENTS
                .try_with(|registry| {
                    let d = registry.document_mut(resolved_owner).unwrap();
                    d.replace_forced_default(true);
                    d.replace_default_style(Some(selected.clone()));
                })
                .unwrap();
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(
                    resolved_owner,
                    8,
                    u32::MAX,
                    resolved_output.as_mut_ptr(),
                    8
                ),
                OK
            );
            let resolved_value = u64::from_le_bytes(resolved_output);
            let resolved_charset = open_snapshot(resolved_owner, 9);
            assert_eq!(
                read_snapshot(resolved_charset),
                [2, 0, 0, 0, 255, 255, 255, 255]
            );
            resolved_output.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(
                    resolved_value,
                    8,
                    0,
                    resolved_output.as_mut_ptr(),
                    8
                ),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(
                    resolved_owner,
                    9,
                    0,
                    resolved_output.as_mut_ptr(),
                    7
                ),
                INVALID_INPUT
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_snapshot_open_v1(
                        resolved_owner,
                        9,
                        0,
                        resolved_output.as_mut_ptr(),
                        8
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(resolved_output, [0xa5; 8]);
            DOCUMENTS
                .try_with(|registry| {
                    let d = registry.document_mut(resolved_owner).unwrap();
                    d.replace_forced_default(false);
                    let mut record = wire::decode_entry(&entry(0, 10)).unwrap();
                    record.style = vec![65, 0, 0xd800];
                    d.add_prepared(record, AddMode::Add).unwrap();
                    d.named_styles_mut().insert(vec![65], None);
                })
                .unwrap();
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(
                    resolved_owner,
                    8,
                    0,
                    resolved_output.as_mut_ptr(),
                    8
                ),
                UNRESOLVED_REFERENCE
            );
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(
                    resolved_owner,
                    9,
                    u32::MAX,
                    resolved_output.as_mut_ptr(),
                    8
                ),
                OUT_OF_RANGE
            );
            let null_charset = open_snapshot(resolved_owner, 9);
            assert_eq!(
                read_snapshot(null_charset),
                [1, 0, 0, 0, 255, 255, 255, 255]
            );
            assert_eq!(xy_vsfilter_document_destroy_v1(resolved_owner), OK);
            assert_eq!(
                read_snapshot(resolved_value),
                style_wire::encode(&selected).unwrap()
            );
            assert_eq!(
                read_snapshot(resolved_charset),
                [2, 0, 0, 0, 255, 255, 255, 255]
            );
            for id in [resolved_value, resolved_charset, null_charset] {
                assert_eq!(xy_vsfilter_document_snapshot_drop_v1(id), OK);
            }

            // Default-style transport and ownership are independent of storage.
            let source = create();
            let target = create();
            let mut output = [0xa5; 8];
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(source, 2, 0, output.as_mut_ptr(), 8),
                OUT_OF_RANGE
            );
            assert_eq!(output, [0xa5; 8]);
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(source, 12, 0, output.as_mut_ptr(), 8),
                INVALID_INPUT
            );
            assert_eq!(output, [0xa5; 8]);
            let mut style = vec![0u8; style_wire::HEADER_BYTES + 8];
            style[64..68].copy_from_slice(&4u32.to_le_bytes());
            style[72..80].copy_from_slice(&0x7ff0000000000042u64.to_le_bytes());
            style[192..].copy_from_slice(&[0, 216, 0, 0, 255, 220, 255, 255]);
            assert_eq!(
                xy_vsfilter_document_set_default_style_v1(source, style.as_ptr(), style.len()),
                OK
            );
            let snapshot = open_snapshot(source, 2);
            assert_eq!(read_snapshot(snapshot), style);
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(source, 2, 1, output.as_mut_ptr(), 8),
                OUT_OF_RANGE
            );
            assert_eq!(
                xy_vsfilter_document_set_default_style_v1(snapshot, style.as_ptr(), style.len()),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_set_default_style_v1(source, ptr::null(), 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_copy_selected_state_v1(target, snapshot),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_copy_selected_state_v1(target, source),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_copy_selected_state_v1(source, source),
                OK
            );
            assert_eq!(state(source).default_style(), state(target).default_style());
            let retained = state(target).default_style().cloned();
            style[72..80].copy_from_slice(&(-0.0f64).to_bits().to_le_bytes());
            assert_eq!(
                xy_vsfilter_document_set_default_style_v1(source, style.as_ptr(), style.len()),
                OK
            );
            assert_ne!(state(source).default_style(), retained.as_ref());
            assert_eq!(xy_vsfilter_document_copy_storage_v1(target, source), OK);
            assert_eq!(state(target).default_style(), retained.as_ref());
            assert_eq!(xy_vsfilter_document_clear_storage_v1(target), OK);
            assert_eq!(xy_vsfilter_document_remove_entries_v1(target), OK);
            assert_eq!(state(target).default_style(), retained.as_ref());
            let cloned = state(target).clone();
            assert_eq!(cloned.default_style(), retained.as_ref());
            assert_eq!(xy_vsfilter_document_destroy_v1(source), OK);
            assert_eq!(xy_vsfilter_document_destroy_v1(target), OK);
            assert_ne!(read_snapshot(snapshot), style);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(snapshot), OK);
            // Supplied LCID uses all raw u32 bits; no constructor default.
            let source = create();
            let destination = create();
            let mut output = [0xa5; 4];
            assert_eq!(
                xy_vsfilter_document_get_metadata_v1(source, 4, output.as_mut_ptr(), 4),
                UNRESOLVED_REFERENCE
            );
            assert_eq!(output, [0xa5; 4]);
            assert_eq!(
                xy_vsfilter_document_set_metadata_v1(destination, 4, 123),
                OK
            );
            for bits in [0u32, 1, 0x7fffffff, 0x80000000, u32::MAX] {
                assert_eq!(
                    xy_vsfilter_document_set_metadata_v1(source, 4, bits as i32),
                    OK
                );
                assert_eq!(
                    xy_vsfilter_document_get_metadata_v1(source, 4, output.as_mut_ptr(), 4),
                    OK
                );
                assert_eq!(u32::from_le_bytes(output), bits);
                assert_eq!(state(source).clone().lcid(), Some(bits));
            }
            let before = state(source);
            output.fill(0xa5);
            let reply = open_snapshot(source, 5);
            assert_eq!(
                xy_vsfilter_document_get_metadata_v1(source, 4, output.as_mut_ptr(), 3),
                INVALID_INPUT
            );
            assert_eq!(output, [0xa5; 4]);
            assert_eq!(
                xy_vsfilter_document_set_metadata_v1(reply, 4, 0),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_get_metadata_v1(reply, 4, output.as_mut_ptr(), 4),
                BAD_HANDLE
            );
            assert_eq!(output, [0xa5; 4]);
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_set_metadata_v1(source, 4, 0))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_get_metadata_v1(
                        source,
                        4,
                        output.as_mut_ptr(),
                        4
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(output, [0xa5; 4]);
            assert_eq!(state(source), before);
            assert_eq!(
                xy_vsfilter_document_copy_selected_state_v1(destination, source),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_copy_storage_v1(destination, source),
                OK
            );
            assert_eq!(xy_vsfilter_document_clear_storage_v1(destination), OK);
            assert_eq!(xy_vsfilter_document_remove_entries_v1(destination), OK);
            assert_eq!(xy_vsfilter_document_empty_v1(destination), OK);
            assert_eq!(state(destination).lcid(), Some(123));
            assert_eq!(state(source).lcid(), Some(u32::MAX));
            assert_eq!(xy_vsfilter_document_destroy_v1(source), OK);
            assert_eq!(
                xy_vsfilter_document_get_metadata_v1(source, 4, output.as_mut_ptr(), 4),
                BAD_HANDLE
            );
            assert_eq!(output, [0xa5; 4]);
            assert_eq!(xy_vsfilter_document_destroy_v1(destination), OK);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(reply), OK);
            // Base Empty preserves unrelated state and immutable old replies.
            let owner = create();
            let raw = [65u8, 0, 0, 0, 0, 216];
            assert_eq!(
                xy_vsfilter_document_set_identity_v1(owner, 0, raw.as_ptr(), raw.len()),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_set_identity_v1(owner, 1, raw.as_ptr(), raw.len()),
                OK
            );
            for field in 0..3 {
                assert_eq!(
                    xy_vsfilter_document_set_interpretation_v1(owner, field, -3),
                    OK
                );
                assert_eq!(xy_vsfilter_document_set_metadata_v1(owner, field, -4), OK);
            }
            for field in 0..5 {
                assert_eq!(
                    xy_vsfilter_document_set_script_field_v1(
                        owner,
                        field,
                        if field == 4 { 1 } else { -5 }
                    ),
                    OK
                );
            }
            assert_eq!(xy_vsfilter_document_set_metadata_v1(owner, 3, 1), OK);
            assert_eq!(
                xy_vsfilter_document_set_par_bits_v1(owner, 0x7ff0000000000042),
                OK
            );
            let value = [0u8; style_wire::HEADER_BYTES];
            assert_eq!(
                xy_vsfilter_document_set_default_style_v1(owner, value.as_ptr(), value.len()),
                OK
            );
            admitted(|registry| {
                let d = registry.document_mut(owner).map_err(registry_error)?;
                for key in 0..65 {
                    d.named_styles_mut()
                        .insert(vec![key], Some(style_wire::decode(&value).unwrap()));
                }
                d.add_prepared(wire::decode_entry(&entry(0, 10)).unwrap(), AddMode::Add)
                    .unwrap();
                d.feedback_animation(0, true).unwrap();
                Ok(())
            })
            .unwrap();
            let entry_snapshot = open_snapshot(owner, 0);
            let segment_snapshot = open_snapshot(owner, 1);
            let name_snapshot = open_snapshot(owner, 5);
            let style_snapshot = open_snapshot(owner, 4);
            let sort_snapshot = open_snapshot(owner, 7);
            let snapshots = [
                entry_snapshot,
                segment_snapshot,
                name_snapshot,
                style_snapshot,
                sort_snapshot,
            ];
            let old_bytes: Vec<_> = snapshots.iter().map(|&id| read_snapshot(id)).collect();
            assert_eq!(xy_vsfilter_document_remove_entries_v1(owner), OK);
            let before = state(owner);
            let bins = before.named_styles().bin_count();
            assert_eq!(xy_vsfilter_document_empty_v1(entry_snapshot), BAD_HANDLE);
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_empty_v1(owner))
                    .unwrap(),
                BUSY
            );
            assert_eq!(state(owner), before);
            assert_eq!(xy_vsfilter_document_empty_v1(owner), OK);
            let after = state(owner);
            assert_eq!(after.script_size(), Some([0, 0]));
            assert!(after.entries().is_empty() && after.segments().is_empty());
            assert_eq!(after.named_styles().count(), 0);
            assert_eq!(after.named_styles().bin_count(), bins);
            assert_eq!(
                after.identity(IdentityField::Name),
                before.identity(IdentityField::Name)
            );
            assert_eq!(
                after.identity(IdentityField::Path),
                before.identity(IdentityField::Path)
            );
            assert_eq!(after.default_style(), before.default_style());
            assert_eq!(after.par_bits(), before.par_bits());
            assert_eq!(after.forced_default(), before.forced_default());
            assert_eq!(after.scaled_bas(), before.scaled_bas());
            for field in [
                MetadataField::PARType,
                MetadataField::ColorMatrix,
                MetadataField::ColorRange,
            ] {
                assert_eq!(after.metadata(field), before.metadata(field));
            }
            for field in [
                InterpretationField::Mode,
                InterpretationField::SubtitleType,
                InterpretationField::Encoding,
            ] {
                assert_eq!(after.interpretation(field), before.interpretation(field));
            }
            for field in [ScriptField::Wrap, ScriptField::Collisions] {
                assert_eq!(after.script_field(field), before.script_field(field));
            }
            assert_eq!(xy_vsfilter_document_empty_v1(owner), OK);
            assert_eq!(state(owner), after);
            assert_eq!(
                xy_vsfilter_document_copy_selected_state_v1(owner, owner),
                OK
            );
            assert_eq!(xy_vsfilter_document_copy_storage_v1(owner, owner), OK);
            assert_eq!(state(owner), after);
            assert_eq!(xy_vsfilter_document_destroy_v1(owner), OK);
            assert_eq!(xy_vsfilter_document_empty_v1(owner), BAD_HANDLE);
            for (id, bytes) in snapshots.into_iter().zip(old_bytes) {
                assert_eq!(read_snapshot(id), bytes);
                assert_eq!(xy_vsfilter_document_snapshot_drop_v1(id), OK);
            }
            // Scalar Sort snapshots never borrow strings or live owner storage.
            let owner = create();
            let empty = open_snapshot(owner, 7);
            assert_eq!(read_snapshot(empty), vec![0; 8]);
            admitted(|registry| {
                let d = registry.document_mut(owner).map_err(registry_error)?;
                for (start, layer, order) in [(5, 2, 7), (1, 1, 8)] {
                    let mut e = wire::decode_entry(&entry(start, start + 10)).unwrap();
                    e.layer = layer;
                    e.readorder = order;
                    d.add_prepared(e, AddMode::EntryOnly).unwrap();
                }
                Ok(())
            })
            .unwrap();
            let before = state(owner);
            let snapshot = open_snapshot(owner, 7);
            let bytes = read_snapshot(snapshot);
            assert_eq!(bytes, wire::encode_sort_snapshot(before.entries()).unwrap());
            assert_eq!(
                wire::decode_sort_snapshot(&bytes).unwrap(),
                vec![
                    wire::SortScalar {
                        start: 5,
                        layer: 2,
                        readorder: 7
                    },
                    wire::SortScalar {
                        start: 1,
                        layer: 1,
                        readorder: 8
                    }
                ]
            );
            let mut output = [0xa5; 8];
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(owner, 7, 1, output.as_mut_ptr(), 8),
                OUT_OF_RANGE
            );
            assert_eq!(output, [0xa5; 8]);
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(owner, 7, 0, output.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(output, [0xa5; 8]);
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(snapshot, 7, 0, output.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(output, [0xa5; 8]);
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_snapshot_open_v1(
                        owner,
                        7,
                        0,
                        output.as_mut_ptr(),
                        8
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(output, [0xa5; 8]);
            admitted(|registry| {
                let d = registry.document_mut(owner).map_err(registry_error)?;
                d.set_prepared_text(0, vec![0xd800, 0, 65], Some(false))
                    .unwrap();
                d.set_entry_time(0, true, -10).unwrap();
                d.reorder_entries(&[1, 0]).unwrap();
                Ok(())
            })
            .unwrap();
            assert_eq!(read_snapshot(snapshot), bytes);
            assert_eq!(read_snapshot(empty), vec![0; 8]);
            let changed = open_snapshot(owner, 7);
            let changed_bytes = read_snapshot(changed);
            assert_ne!(changed_bytes, bytes);
            assert_eq!(
                wire::decode_sort_snapshot(&changed_bytes).unwrap()[1].start,
                -10
            );
            assert_eq!(xy_vsfilter_document_destroy_v1(owner), OK);
            assert_eq!(read_snapshot(snapshot), bytes);
            assert_eq!(read_snapshot(changed), changed_bytes);
            for id in [empty, snapshot, changed] {
                assert_eq!(xy_vsfilter_document_snapshot_drop_v1(id), OK);
            }
            // Prepared conflict relocation consumes one fully validated packet.
            let owner = create();
            let mut base = [0u8; style_wire::HEADER_BYTES];
            base[36] = 1;
            let old_value = style_wire::decode(&base).unwrap();
            base[36] = 2;
            let new_value = style_wire::decode(&base).unwrap();
            base[36] = 3;
            let captured = style_wire::decode(&base).unwrap();
            let request = wire::PreparedStyleRename {
                old: vec![65, 0, 0xd800],
                new: vec![66, 0, 0xdcff],
                previous: Some(captured.clone()),
            };
            admitted(|registry| {
                let d = registry.document_mut(owner).map_err(registry_error)?;
                d.named_styles_mut()
                    .insert(request.old.clone(), Some(old_value));
                d.named_styles_mut()
                    .insert(request.new.clone(), Some(new_value));
                let mut e = wire::decode_entry(&entry(0, 10)).unwrap();
                e.style = request.old.clone();
                d.add_prepared(e, AddMode::Add).unwrap();
                d.feedback_animation(0, true).unwrap();
                Ok(())
            })
            .unwrap();
            let before = state(owner);
            let snapshot = open_snapshot(owner, 0);
            let snapshot_bytes = read_snapshot(snapshot);
            let mut packet = wire::encode_style_rename(&request).unwrap();
            for length in 0..packet.len() {
                assert_eq!(
                    xy_vsfilter_document_commit_style_rename_v1(owner, packet.as_ptr(), length),
                    INVALID_INPUT
                );
                assert_eq!(state(owner), before);
            }
            let mut invalid = packet.clone();
            invalid[..4].copy_from_slice(&u32::MAX.to_le_bytes());
            assert_eq!(
                xy_vsfilter_document_commit_style_rename_v1(owner, invalid.as_ptr(), invalid.len()),
                COUNT_OVERFLOW
            );
            assert_eq!(state(owner), before);
            invalid = packet.clone();
            invalid[8..16].copy_from_slice(&u64::MAX.to_le_bytes());
            assert_eq!(
                xy_vsfilter_document_commit_style_rename_v1(owner, invalid.as_ptr(), invalid.len()),
                COUNT_OVERFLOW
            );
            assert_eq!(state(owner), before);
            assert_eq!(
                xy_vsfilter_document_commit_style_rename_v1(
                    snapshot,
                    packet.as_ptr(),
                    packet.len()
                ),
                BAD_HANDLE
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_commit_style_rename_v1(
                        owner,
                        packet.as_ptr(),
                        packet.len()
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(state(owner), before);
            assert_eq!(
                xy_vsfilter_document_commit_style_rename_v1(owner, packet.as_ptr(), packet.len()),
                OK
            );
            packet.fill(0);
            let after = state(owner);
            assert!(after.named_styles().get(&request.old).is_none());
            assert_eq!(
                after.named_styles().get(&request.new),
                Some(&Some(captured))
            );
            assert_eq!(after.entries()[0].style, request.new);
            assert_eq!(after.segments(), before.segments());
            assert_eq!(read_snapshot(snapshot), snapshot_bytes);
            assert_eq!(xy_vsfilter_document_destroy_v1(owner), OK);
            assert_eq!(read_snapshot(snapshot), snapshot_bytes);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(snapshot), OK);
            // Copy's selected header tail preserves every excluded owner field.
            let source = create();
            let destination = create();
            let raw = [65u8, 0, 0, 0, 0, 216, 255, 220];
            assert_eq!(
                xy_vsfilter_document_set_identity_v1(source, 0, raw.as_ptr(), raw.len()),
                OK
            );
            for field in 0..3 {
                assert_eq!(
                    xy_vsfilter_document_set_interpretation_v1(source, field, i32::MIN),
                    OK
                );
            }
            for field in 0..5 {
                assert_eq!(
                    xy_vsfilter_document_set_script_field_v1(
                        source,
                        field,
                        if field == 4 { 1 } else { -5 }
                    ),
                    OK
                );
            }
            assert_eq!(xy_vsfilter_document_set_metadata_v1(source, 3, 1), OK);
            let mut bytes = [0u8; style_wire::HEADER_BYTES];
            bytes[72..80].copy_from_slice(&0x7ff0000000000042u64.to_le_bytes());
            assert_eq!(
                xy_vsfilter_document_set_default_style_v1(source, bytes.as_ptr(), bytes.len()),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_set_identity_v1(destination, 1, raw.as_ptr(), raw.len()),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_set_par_bits_v1(destination, 0x8000000000000000),
                OK
            );
            for field in 0..3 {
                assert_eq!(
                    xy_vsfilter_document_set_metadata_v1(destination, field, i32::MAX),
                    OK
                );
            }
            assert_eq!(
                xy_vsfilter_document_named_style_put_v1(
                    destination,
                    raw.as_ptr(),
                    raw.len(),
                    bytes.as_ptr(),
                    bytes.len()
                ),
                OK
            );
            admitted(|registry| {
                registry
                    .document_mut(destination)
                    .map_err(registry_error)?
                    .add_prepared(wire::decode_entry(&entry(0, 10)).unwrap(), AddMode::Add)
                    .unwrap();
                Ok(())
            })
            .unwrap();
            let before = state(destination);
            let source_before = state(source);
            let reply = open_snapshot(source, 5);
            assert_eq!(
                xy_vsfilter_document_copy_selected_state_v1(reply, source),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_copy_selected_state_v1(destination, reply),
                BAD_HANDLE
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_copy_selected_state_v1(destination, source))
                    .unwrap(),
                BUSY
            );
            assert_eq!(state(destination), before);
            assert_eq!(state(source), source_before);
            assert_eq!(
                xy_vsfilter_document_copy_selected_state_v1(destination, source),
                OK
            );
            let after = state(destination);
            assert_eq!(after.selected_state(), source_before.selected_state());
            assert_eq!(
                after.identity(IdentityField::Path),
                before.identity(IdentityField::Path)
            );
            assert_eq!(after.par_bits(), before.par_bits());
            for field in [
                MetadataField::PARType,
                MetadataField::ColorMatrix,
                MetadataField::ColorRange,
            ] {
                assert_eq!(after.metadata(field), before.metadata(field));
            }
            assert_eq!(after.named_styles(), before.named_styles());
            assert_eq!(after.entries(), before.entries());
            assert_eq!(after.segments(), before.segments());
            assert_eq!(
                xy_vsfilter_document_copy_selected_state_v1(destination, destination),
                OK
            );
            assert_eq!(state(destination), after);
            assert_eq!(
                xy_vsfilter_document_set_identity_v1(source, 0, ptr::null(), 0),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_set_default_style_v1(source, bytes.as_ptr(), bytes.len()),
                OK
            );
            assert_eq!(state(destination), after);
            assert_eq!(xy_vsfilter_document_destroy_v1(source), OK);
            assert_eq!(xy_vsfilter_document_destroy_v1(destination), OK);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(reply), OK);
            // Render metadata transports opaque integer and floating-point bits.
            let owner = create();
            let destination = create();
            let mut output = [0xa5; 8];
            assert_eq!(
                xy_vsfilter_document_get_par_bits_v1(owner, output.as_mut_ptr(), 8),
                UNRESOLVED_REFERENCE
            );
            assert_eq!(output, [0xa5; 8]);
            for field in 0..4 {
                assert_eq!(
                    xy_vsfilter_document_get_metadata_v1(owner, field, output.as_mut_ptr(), 4),
                    UNRESOLVED_REFERENCE
                );
                assert_eq!(output, [0xa5; 8]);
                let value = if field == 3 { 1 } else { i32::MIN };
                assert_eq!(
                    xy_vsfilter_document_set_metadata_v1(owner, field, value),
                    OK
                );
                assert_eq!(
                    xy_vsfilter_document_set_metadata_v1(
                        destination,
                        field,
                        if field == 3 { 0 } else { i32::MAX }
                    ),
                    OK
                );
                assert_eq!(
                    xy_vsfilter_document_get_metadata_v1(owner, field, output.as_mut_ptr(), 4),
                    OK
                );
                assert_eq!(i32::from_le_bytes(output[..4].try_into().unwrap()), value);
                output.fill(0xa5);
            }
            assert_eq!(
                xy_vsfilter_document_set_par_bits_v1(destination, 0x3ff0000000000000),
                OK
            );
            for bits in [
                0u64,
                1 << 63,
                1,
                u64::MAX,
                0x7ff0000000000000,
                0xfff0000000000000,
                0x7ff0000000000001,
                0x7ff8000000000042,
            ] {
                assert_eq!(xy_vsfilter_document_set_par_bits_v1(owner, bits), OK);
                assert_eq!(
                    xy_vsfilter_document_get_par_bits_v1(owner, output.as_mut_ptr(), 8),
                    OK
                );
                assert_eq!(u64::from_le_bytes(output), bits);
            }
            output.fill(0xa5);
            let before = state(owner);
            for invalid in [-1, 2, i32::MIN, i32::MAX] {
                assert_eq!(
                    xy_vsfilter_document_set_metadata_v1(owner, 3, invalid),
                    INVALID_INPUT
                );
                assert_eq!(state(owner), before);
            }
            assert_eq!(
                xy_vsfilter_document_set_metadata_v1(owner, 6, 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_get_metadata_v1(owner, 6, output.as_mut_ptr(), 4),
                INVALID_INPUT
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_get_metadata_v1(
                        owner,
                        6,
                        output.as_mut_ptr(),
                        4
                    ))
                    .unwrap(),
                INVALID_INPUT
            );
            assert_eq!(output, [0xa5; 8]);
            assert_eq!(
                xy_vsfilter_document_get_metadata_v1(0, 6, output.as_mut_ptr(), 4),
                INVALID_INPUT
            );
            assert_eq!(output, [0xa5; 8]);
            assert_eq!(
                xy_vsfilter_document_get_metadata_v1(owner, 0, output.as_mut_ptr(), 3),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_get_par_bits_v1(owner, output.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(output, [0xa5; 8]);
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_set_par_bits_v1(owner, 0))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_get_metadata_v1(
                        owner,
                        0,
                        output.as_mut_ptr(),
                        4
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(output, [0xa5; 8]);
            assert_eq!(state(owner), before);
            let reply = open_snapshot(owner, 5);
            assert_eq!(
                xy_vsfilter_document_set_metadata_v1(reply, 0, 0),
                BAD_HANDLE
            );
            assert_eq!(xy_vsfilter_document_set_par_bits_v1(reply, 0), BAD_HANDLE);
            assert_eq!(
                xy_vsfilter_document_get_par_bits_v1(reply, output.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(output, [0xa5; 8]);
            assert_eq!(xy_vsfilter_document_copy_storage_v1(destination, owner), OK);
            assert_eq!(xy_vsfilter_document_clear_storage_v1(destination), OK);
            assert_eq!(xy_vsfilter_document_remove_entries_v1(destination), OK);
            assert_eq!(state(destination).par_bits(), Some(0x3ff0000000000000));
            assert_eq!(state(destination).forced_default(), Some(false));
            for field in [
                MetadataField::PARType,
                MetadataField::ColorMatrix,
                MetadataField::ColorRange,
            ] {
                assert_eq!(state(destination).metadata(field), Some(i32::MAX));
            }
            assert_eq!(state(owner).clone(), before);
            assert_eq!(xy_vsfilter_document_set_metadata_v1(owner, 0, -1), OK);
            assert_eq!(
                state(destination).metadata(MetadataField::PARType),
                Some(i32::MAX)
            );
            assert_eq!(xy_vsfilter_document_destroy_v1(owner), OK);
            assert_eq!(xy_vsfilter_document_destroy_v1(destination), OK);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(reply), OK);
            // Script values preserve raw integers; only the Boolean is shaped.
            let owner = create();
            let destination = create();
            let mut output = [0xa5; 8];
            assert_eq!(
                xy_vsfilter_document_get_script_size_v1(owner, output.as_mut_ptr(), 8),
                UNRESOLVED_REFERENCE
            );
            assert_eq!(output, [0xa5; 8]);
            for field in 0..5 {
                assert_eq!(
                    xy_vsfilter_document_get_script_field_v1(owner, field, output.as_mut_ptr(), 4),
                    UNRESOLVED_REFERENCE
                );
                assert_eq!(output, [0xa5; 8]);
                let value = if field == 4 { 1 } else { i32::MIN };
                assert_eq!(
                    xy_vsfilter_document_set_script_field_v1(owner, field, value),
                    OK
                );
                if field == 0 {
                    assert_eq!(
                        xy_vsfilter_document_get_script_size_v1(owner, output.as_mut_ptr(), 8),
                        UNRESOLVED_REFERENCE
                    );
                    assert_eq!(output, [0xa5; 8]);
                }

                assert_eq!(
                    xy_vsfilter_document_set_script_field_v1(
                        destination,
                        field,
                        if field == 4 { 0 } else { i32::MAX }
                    ),
                    OK
                );
                assert_eq!(
                    xy_vsfilter_document_get_script_field_v1(owner, field, output.as_mut_ptr(), 4),
                    OK
                );
                assert_eq!(i32::from_le_bytes(output[..4].try_into().unwrap()), value);
                output.fill(0xa5);
            }
            let before = state(owner);
            for invalid in [-1, 2, i32::MIN, i32::MAX] {
                assert_eq!(
                    xy_vsfilter_document_set_script_field_v1(owner, 4, invalid),
                    INVALID_INPUT
                );
                assert_eq!(state(owner), before);
            }
            assert_eq!(
                xy_vsfilter_document_set_script_field_v1(owner, 7, 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_get_script_field_v1(owner, 7, output.as_mut_ptr(), 4),
                INVALID_INPUT
            );
            assert_eq!(output, [0xa5; 8]);
            assert_eq!(
                xy_vsfilter_document_get_script_size_v1(owner, output.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(output, [0xa5; 8]);
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_set_script_size_v1(owner, 5, 6))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_get_script_size_v1(
                        owner,
                        output.as_mut_ptr(),
                        8
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(output, [0xa5; 8]);
            assert_eq!(state(owner), before);
            let reply = open_snapshot(owner, 5);
            assert_eq!(
                xy_vsfilter_document_set_script_field_v1(reply, 0, 2),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_set_script_size_v1(reply, 2, 3),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_get_script_size_v1(reply, output.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(output, [0xa5; 8]);
            assert_eq!(xy_vsfilter_document_set_script_size_v1(owner, -7, 11), OK);
            assert_eq!(
                xy_vsfilter_document_get_script_size_v1(owner, output.as_mut_ptr(), 8),
                OK
            );
            assert_eq!(i32::from_le_bytes(output[..4].try_into().unwrap()), -7);
            assert_eq!(i32::from_le_bytes(output[4..].try_into().unwrap()), 11);
            assert_eq!(xy_vsfilter_document_copy_storage_v1(destination, owner), OK);
            assert_eq!(xy_vsfilter_document_clear_storage_v1(destination), OK);
            assert_eq!(xy_vsfilter_document_remove_entries_v1(destination), OK);
            assert_eq!(state(destination).script_size(), Some([i32::MAX, i32::MAX]));
            assert_eq!(state(destination).scaled_bas(), Some(false));
            assert_eq!(before.clone().script_size(), Some([i32::MIN, i32::MIN]));
            assert_eq!(xy_vsfilter_document_destroy_v1(owner), OK);
            assert_eq!(xy_vsfilter_document_destroy_v1(destination), OK);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(reply), OK);
            // Supplied interpretation values are opaque signed scalar bits.
            let owner = create();
            let destination = create();
            let mut output = [0xa5; 4];
            for field in 0..3 {
                assert_eq!(
                    xy_vsfilter_document_get_interpretation_v1(
                        owner,
                        field,
                        output.as_mut_ptr(),
                        4
                    ),
                    UNRESOLVED_REFERENCE
                );
                assert_eq!(output, [0xa5; 4]);
                assert_eq!(
                    xy_vsfilter_document_set_interpretation_v1(owner, field, i32::MIN),
                    OK
                );
                assert_eq!(
                    xy_vsfilter_document_set_interpretation_v1(destination, field, i32::MAX),
                    OK
                );
                assert_eq!(
                    xy_vsfilter_document_get_interpretation_v1(
                        owner,
                        field,
                        output.as_mut_ptr(),
                        4
                    ),
                    OK
                );
                assert_eq!(i32::from_le_bytes(output), i32::MIN);
                output.fill(0xa5);
            }
            let before = state(owner);
            assert_eq!(
                xy_vsfilter_document_set_interpretation_v1(owner, 3, 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_get_interpretation_v1(owner, 3, output.as_mut_ptr(), 4),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_get_interpretation_v1(owner, 0, output.as_mut_ptr(), 3),
                INVALID_INPUT
            );
            assert_eq!(output, [0xa5; 4]);
            assert_eq!(state(owner), before);
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_set_interpretation_v1(owner, 0, 5))
                    .unwrap(),
                BUSY
            );
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_get_interpretation_v1(
                        owner,
                        0,
                        output.as_mut_ptr(),
                        4
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(output, [0xa5; 4]);
            assert_eq!(state(owner), before);
            let reply = open_snapshot(owner, 5);
            assert_eq!(
                xy_vsfilter_document_set_interpretation_v1(reply, 0, 5),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_get_interpretation_v1(reply, 0, output.as_mut_ptr(), 4),
                BAD_HANDLE
            );
            assert_eq!(output, [0xa5; 4]);
            assert_eq!(xy_vsfilter_document_copy_storage_v1(destination, owner), OK);
            assert_eq!(xy_vsfilter_document_clear_storage_v1(destination), OK);
            assert_eq!(xy_vsfilter_document_remove_entries_v1(destination), OK);
            for field in 0..3 {
                assert_eq!(
                    xy_vsfilter_document_get_interpretation_v1(
                        destination,
                        field,
                        output.as_mut_ptr(),
                        4
                    ),
                    OK
                );
                assert_eq!(i32::from_le_bytes(output), i32::MAX);
            }
            assert_eq!(state(owner).clone(), before);
            assert_eq!(xy_vsfilter_document_set_interpretation_v1(owner, 0, -1), OK);
            assert_eq!(
                xy_vsfilter_document_get_interpretation_v1(destination, 0, output.as_mut_ptr(), 4),
                OK
            );
            assert_eq!(i32::from_le_bytes(output), i32::MAX);
            assert_eq!(xy_vsfilter_document_destroy_v1(owner), OK);
            output.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_get_interpretation_v1(owner, 0, output.as_mut_ptr(), 4),
                BAD_HANDLE
            );
            assert_eq!(output, [0xa5; 4]);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(reply), OK);
            assert_eq!(xy_vsfilter_document_destroy_v1(destination), OK);
            // Identity snapshots own exact raw units and survive owner closure.
            let owner = create();
            let destination = create();
            let empty_name = open_snapshot(owner, 5);
            let empty_path = open_snapshot(owner, 6);
            assert!(read_snapshot(empty_name).is_empty());
            assert!(read_snapshot(empty_path).is_empty());
            let raw = [65u8, 0, 0, 0, 0, 216, 255, 220, 255, 255];
            let path = [80u8, 0, 0, 0, 66, 0];
            assert_eq!(
                xy_vsfilter_document_set_identity_v1(owner, 0, raw.as_ptr(), raw.len()),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_set_identity_v1(owner, 1, path.as_ptr(), path.len()),
                OK
            );
            let name_reply = open_snapshot(owner, 5);
            let path_reply = open_snapshot(owner, 6);
            assert_eq!(read_snapshot(name_reply), raw);
            assert_eq!(read_snapshot(path_reply), path);
            let before = state(owner);
            let mut output = [0xa5; 8];
            assert_eq!(
                xy_vsfilter_document_set_identity_v1(owner, 2, ptr::null(), 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_set_identity_v1(owner, 0, raw.as_ptr(), 1),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_set_identity_v1(name_reply, 0, raw.as_ptr(), raw.len()),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(owner, 5, 1, output.as_mut_ptr(), 8),
                OUT_OF_RANGE
            );
            assert_eq!(output, [0xa5; 8]);
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(owner, 6, 0, output.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(output, [0xa5; 8]);
            assert_eq!(state(owner), before);
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_set_identity_v1(
                        owner,
                        0,
                        path.as_ptr(),
                        path.len()
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(state(owner), before);
            assert_eq!(
                xy_vsfilter_document_set_identity_v1(destination, 0, path.as_ptr(), path.len()),
                OK
            );
            let retained = state(destination);
            assert_eq!(xy_vsfilter_document_copy_storage_v1(destination, owner), OK);
            assert_eq!(
                state(destination).identity(IdentityField::Name),
                retained.identity(IdentityField::Name)
            );
            assert!(state(destination).identity(IdentityField::Path).is_empty());
            assert_eq!(xy_vsfilter_document_clear_storage_v1(owner), OK);
            assert_eq!(xy_vsfilter_document_remove_entries_v1(owner), OK);
            assert_eq!(state(owner), before);
            assert_eq!(state(owner).clone(), before);
            assert_eq!(
                xy_vsfilter_document_set_identity_v1(owner, 0, ptr::null(), 0),
                OK
            );
            assert!(state(owner).identity(IdentityField::Name).is_empty());
            assert_eq!(
                state(owner).identity(IdentityField::Path),
                before.identity(IdentityField::Path)
            );
            assert_eq!(xy_vsfilter_document_destroy_v1(owner), OK);
            assert_eq!(xy_vsfilter_document_destroy_v1(destination), OK);
            assert_eq!(read_snapshot(name_reply), raw);
            assert_eq!(read_snapshot(path_reply), path);
            assert!(read_snapshot(empty_name).is_empty());
            for id in [empty_name, empty_path, name_reply, path_reply] {
                assert_eq!(xy_vsfilter_document_snapshot_drop_v1(id), OK);
            }
            // Named values, raw keys, null presence and reply ownership.
            let owner = create();
            let destination = create();
            let key = [65u8, 0, 0, 0, 0, 216];
            let alias = [65u8, 0, 0, 0, 255, 220];
            let mut value = vec![0u8; style_wire::HEADER_BYTES + 4];
            value[64..68].copy_from_slice(&2u32.to_le_bytes());
            value[192..].copy_from_slice(&[0, 216, 255, 220]);
            value[72..80].copy_from_slice(&0x7ff0000000000042u64.to_le_bytes());
            let mut reply = [0xa5; 16];
            assert_eq!(
                xy_vsfilter_document_named_style_lookup_v1(
                    owner,
                    key.as_ptr(),
                    key.len(),
                    1,
                    reply.as_mut_ptr(),
                    16
                ),
                OK
            );
            assert_eq!(reply, [0; 16]);
            assert_eq!(
                xy_vsfilter_document_named_style_put_v1(
                    owner,
                    key.as_ptr(),
                    key.len(),
                    value.as_ptr(),
                    value.len()
                ),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_named_style_lookup_v1(
                    owner,
                    alias.as_ptr(),
                    alias.len(),
                    0,
                    reply.as_mut_ptr(),
                    16
                ),
                OK
            );
            assert_eq!(u32::from_le_bytes(reply[..4].try_into().unwrap()), 2);
            assert_eq!(&reply[4..], &[0; 12]);
            assert_eq!(
                xy_vsfilter_document_named_style_lookup_v1(
                    owner,
                    alias.as_ptr(),
                    alias.len(),
                    1,
                    reply.as_mut_ptr(),
                    16
                ),
                OK
            );
            let saved = u64::from_le_bytes(reply[8..].try_into().unwrap());
            assert_ne!(saved, 0);
            assert_eq!(read_snapshot(saved), value);
            let key_snapshot = open_snapshot(owner, 3);
            assert_eq!(read_snapshot(key_snapshot), vec![65, 0]);
            let value_snapshot = open_snapshot(owner, 4);
            assert_eq!(read_snapshot(value_snapshot), value);
            assert_eq!(
                xy_vsfilter_document_named_style_put_v1(
                    saved,
                    key.as_ptr(),
                    key.len(),
                    value.as_ptr(),
                    value.len()
                ),
                BAD_HANDLE
            );
            let before = state(owner);
            reply.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_named_style_lookup_v1(
                    owner,
                    key.as_ptr(),
                    key.len(),
                    1,
                    reply.as_mut_ptr(),
                    15
                ),
                INVALID_INPUT
            );
            assert_eq!(reply, [0xa5; 16]);
            assert_eq!(state(owner), before);
            assert_eq!(
                xy_vsfilter_document_named_style_lookup_v1(
                    owner,
                    key.as_ptr(),
                    key.len(),
                    2,
                    reply.as_mut_ptr(),
                    16
                ),
                INVALID_INPUT
            );
            assert_eq!(reply, [0xa5; 16]);
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_named_style_lookup_v1(
                        owner,
                        key.as_ptr(),
                        key.len(),
                        1,
                        reply.as_mut_ptr(),
                        16
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(reply, [0xa5; 16]);
            assert_eq!(state(owner), before);
            assert_eq!(
                DOCUMENTS
                    .try_with(|_| xy_vsfilter_document_named_style_put_v1(
                        owner,
                        key.as_ptr(),
                        key.len(),
                        value.as_ptr(),
                        value.len()
                    ))
                    .unwrap(),
                BUSY
            );
            assert_eq!(state(owner), before);
            let mut small_count = [0xa5; 4];
            assert_eq!(
                xy_vsfilter_document_named_style_count_v1(owner, small_count.as_mut_ptr(), 3),
                INVALID_INPUT
            );
            assert_eq!(small_count, [0xa5; 4]);

            assert_eq!(
                xy_vsfilter_document_named_style_put_v1(
                    owner,
                    key.as_ptr(),
                    1,
                    value.as_ptr(),
                    value.len()
                ),
                INVALID_INPUT
            );
            assert_eq!(state(owner), before);
            assert_eq!(
                xy_vsfilter_document_named_style_put_v1(
                    destination,
                    key.as_ptr(),
                    key.len(),
                    value.as_ptr(),
                    value.len()
                ),
                OK
            );
            let destination_before = state(destination);
            assert_eq!(
                xy_vsfilter_document_named_style_put_v1(
                    owner,
                    key.as_ptr(),
                    key.len(),
                    ptr::null(),
                    0
                ),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_named_style_lookup_v1(
                    owner,
                    key.as_ptr(),
                    key.len(),
                    1,
                    reply.as_mut_ptr(),
                    16
                ),
                OK
            );
            assert_eq!(u32::from_le_bytes(reply[..4].try_into().unwrap()), 1);
            assert_eq!(&reply[4..], &[0; 12]);
            reply.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(owner, 4, 0, reply.as_mut_ptr(), 16),
                UNRESOLVED_REFERENCE
            );
            assert_eq!(reply, [0xa5; 16]);
            assert_eq!(xy_vsfilter_document_copy_storage_v1(destination, owner), OK);
            assert_eq!(
                state(destination).named_styles(),
                destination_before.named_styles()
            );
            assert_eq!(
                state(destination).clone().named_styles(),
                destination_before.named_styles()
            );
            assert_eq!(xy_vsfilter_document_clear_storage_v1(destination), OK);
            assert_eq!(xy_vsfilter_document_remove_entries_v1(destination), OK);
            assert_eq!(
                state(destination).named_styles(),
                destination_before.named_styles()
            );
            let mut count = [0xa5; 4];
            assert_eq!(
                xy_vsfilter_document_named_style_count_v1(owner, count.as_mut_ptr(), 4),
                OK
            );
            assert_eq!(u32::from_le_bytes(count), 1);
            assert_eq!(
                xy_vsfilter_document_named_style_remove_v1(owner, alias.as_ptr(), alias.len()),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_named_style_remove_v1(owner, alias.as_ptr(), alias.len()),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_named_style_lookup_v1(
                    owner,
                    key.as_ptr(),
                    key.len(),
                    1,
                    reply.as_mut_ptr(),
                    16
                ),
                OK
            );
            assert_eq!(reply, [0; 16]);
            assert_eq!(xy_vsfilter_document_named_style_clear_v1(destination), OK);
            assert_eq!(state(destination).named_styles().count(), 0);
            assert_eq!(xy_vsfilter_document_destroy_v1(owner), OK);
            assert_eq!(xy_vsfilter_document_destroy_v1(destination), OK);
            assert_eq!(read_snapshot(saved), value);
            assert_eq!(read_snapshot(value_snapshot), value);
            assert_eq!(read_snapshot(key_snapshot), vec![65, 0]);
            for id in [saved, value_snapshot, key_snapshot] {
                assert_eq!(xy_vsfilter_document_snapshot_drop_v1(id), OK);
            }
            let mut sentinel = [0xa5; 80];
            assert_eq!(
                xy_vsfilter_document_create_v1(ptr::null_mut(), 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_create_v1(ptr::null_mut(), 8),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_create_v1(sentinel.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(sentinel, [0xa5; 80]);
            let owner = create();
            let destination = create();
            assert_ne!(owner, destination);
            let encoded = entry(0, 10);
            let original = state(owner);
            assert_eq!(
                xy_vsfilter_document_add_prepared_v1(
                    owner,
                    0,
                    encoded.as_ptr(),
                    encoded.len(),
                    sentinel.as_mut_ptr(),
                    7
                ),
                INVALID_INPUT
            );
            assert_eq!(state(owner), original);
            assert_eq!(sentinel, [0xa5; 80]);
            assert_eq!(
                xy_vsfilter_document_add_prepared_v1(
                    owner,
                    0,
                    ptr::null(),
                    1,
                    sentinel.as_mut_ptr(),
                    8
                ),
                INVALID_INPUT
            );
            assert_eq!(add(owner, 9, &encoded), (INVALID_INPUT, [0xa5; 8]));
            let mut malformed = encoded.clone();
            malformed[60] = 1;
            assert_eq!(add(owner, 0, &malformed), (INVALID_INPUT, [0xa5; 8]));
            malformed = encoded.clone();
            malformed.push(0);
            assert_eq!(add(owner, 0, &malformed), (INVALID_INPUT, [0xa5; 8]));
            assert_eq!(state(owner), original);

            // Input/output alias is allowed: staging finishes before output.
            let mut overlapping = encoded.clone();
            let overlapping_length = overlapping.len();
            let overlapping_pointer = overlapping.as_mut_ptr();
            let input = overlapping_pointer.cast_const();
            let output = overlapping_pointer;
            assert_eq!(
                xy_vsfilter_document_add_prepared_v1(
                    owner,
                    0,
                    input,
                    overlapping_length,
                    output,
                    8
                ),
                OK
            );
            assert_eq!(&overlapping[..8], &[1, 0, 0, 0, 0, 0, 0, 0]);
            let before = state(owner);
            assert_eq!(before.entries().len(), 1);
            assert_eq!(before.entries()[0].text, [0, 0xd800, 0xffff]);
            assert_eq!(before.entries()[0].readorder, 0);
            assert_eq!(
                xy_vsfilter_document_counts_v1(owner, sentinel.as_mut_ptr(), 8),
                OK
            );
            assert_eq!(
                wire::decode_counts(&sentinel[..8]).unwrap(),
                Counts {
                    entries: 1,
                    segments: 1
                }
            );
            assert_eq!(&sentinel[8..], &[0xa5; 72]);
            sentinel.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_entry_header_v1(owner, 0, sentinel.as_mut_ptr(), 64),
                OK
            );
            assert_eq!(
                wire::decode_entry_header(&sentinel[..64]).unwrap().lengths,
                [3, 2, 1, 0]
            );
            assert_eq!(&sentinel[64..], &[0xa5; 16]);
            sentinel.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_segment_header_v1(owner, 0, sentinel.as_mut_ptr(), 32),
                OK
            );
            assert_eq!(
                wire::decode_segment_header(&sentinel[..32])
                    .unwrap()
                    .member_count,
                1
            );
            sentinel.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_entry_header_v1(owner, 1, sentinel.as_mut_ptr(), 64),
                OUT_OF_RANGE
            );
            assert_eq!(
                xy_vsfilter_document_segment_header_v1(owner, 1, sentinel.as_mut_ptr(), 32),
                OUT_OF_RANGE
            );
            assert_eq!(sentinel, [0xa5; 80]);
            assert_eq!(
                xy_vsfilter_document_query_time_v1(owner, 0, 0, sentinel.as_mut_ptr(), 16),
                OK
            );
            assert_eq!(wire::decode_query(&sentinel[..16]).unwrap().index, 0);
            sentinel.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_query_time_v1(owner, 3, 0, sentinel.as_mut_ptr(), 16),
                INVALID_INPUT
            );
            assert_eq!(sentinel, [0xa5; 80]);

            let entry_snapshot = open_snapshot(owner, 0);
            let segment_snapshot = open_snapshot(owner, 1);
            let entry_bytes = wire::encode_entry(&before.entries()[0]).unwrap();
            let segment_bytes = wire::encode_segment(&before.segments()[0]).unwrap();
            assert_eq!(read_snapshot(entry_snapshot), entry_bytes);
            assert_eq!(read_snapshot(segment_snapshot), segment_bytes);
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(owner, 2, 0, sentinel.as_mut_ptr(), 8),
                OUT_OF_RANGE
            );
            assert_eq!(
                xy_vsfilter_document_snapshot_open_v1(owner, 0, 0, sentinel.as_mut_ptr(), 7),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_snapshot_read_v1(
                    entry_snapshot,
                    entry_bytes.len() as u64,
                    ptr::null_mut(),
                    0
                ),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_snapshot_read_v1(
                    entry_snapshot,
                    entry_bytes.len() as u64 + 1,
                    ptr::null_mut(),
                    0
                ),
                OUT_OF_RANGE
            );
            assert_eq!(
                xy_vsfilter_document_snapshot_read_v1(
                    entry_snapshot,
                    u64::MAX,
                    sentinel.as_mut_ptr(),
                    1
                ),
                OUT_OF_RANGE
            );
            assert_eq!(
                xy_vsfilter_document_snapshot_read_v1(
                    entry_snapshot,
                    entry_bytes.len() as u64 - 1,
                    sentinel.as_mut_ptr(),
                    2
                ),
                OUT_OF_RANGE
            );
            assert_eq!(sentinel, [0xa5; 80]);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(owner), BAD_HANDLE);
            assert_eq!(xy_vsfilter_document_destroy_v1(entry_snapshot), BAD_HANDLE);
            assert_eq!(
                xy_vsfilter_document_counts_v1(entry_snapshot, sentinel.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_snapshot_read_v1(owner, 0, sentinel.as_mut_ptr(), 1),
                BAD_HANDLE
            );
            assert_eq!(sentinel, [0xa5; 80]);

            // Reentry is refused before canonical access or output publication.
            DOCUMENTS
                .try_with(|registry| {
                    let retained = registry.document(owner).unwrap();
                    assert_eq!(
                        xy_vsfilter_document_counts_v1(owner, sentinel.as_mut_ptr(), 8),
                        BUSY
                    );
                    assert_eq!(
                        xy_vsfilter_document_create_v1(sentinel.as_mut_ptr(), 8),
                        BUSY
                    );
                    assert_eq!(
                        xy_vsfilter_document_set_entry_time_v1(owner, 0, 1, 99),
                        BUSY
                    );
                    assert_eq!(sentinel, [0xa5; 80]);
                    assert_eq!(retained, &before);
                })
                .unwrap();

            assert_eq!(xy_vsfilter_document_animation_or_v1(owner, 0, 1), OK);
            assert_eq!(xy_vsfilter_document_animation_or_v1(owner, 0, 0), OK);
            assert!(state(owner).segments()[0].animated);
            let animated = state(owner);
            assert_eq!(
                xy_vsfilter_document_animation_or_v1(owner, 0, 2),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_animation_or_v1(owner, 1, 1),
                OUT_OF_RANGE
            );
            assert_eq!(state(owner), animated);
            assert_eq!(xy_vsfilter_document_copy_storage_v1(destination, owner), OK);
            assert_eq!(state(destination), animated);
            assert_eq!(
                xy_vsfilter_document_copy_storage_v1(destination, destination),
                OK
            );
            assert_eq!(state(destination), animated);
            assert_eq!(
                xy_vsfilter_document_copy_storage_v1(destination, entry_snapshot),
                BAD_HANDLE
            );
            assert_eq!(state(destination), animated);

            // Refusals are statuses, never a native fallback or partial edit.
            assert_eq!(
                add(owner, 2, &entry(20, 30)),
                (UNRESOLVED_REFERENCE, [0xa5; 8])
            );
            assert_eq!(state(owner), animated);
            assert_eq!(
                xy_vsfilter_document_set_prepared_text_v1(owner, 1, -1, ptr::null(), 0),
                OUT_OF_RANGE
            );
            assert_eq!(
                xy_vsfilter_document_set_prepared_text_v1(owner, 0, 2, ptr::null(), 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_set_prepared_style_v1(owner, 0, encoded.as_ptr(), 1),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_set_entry_time_v1(owner, 0, 2, 1),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_set_entry_time_v1(owner, 1, 0, 1),
                OUT_OF_RANGE
            );
            assert_eq!(
                xy_vsfilter_document_reorder_entries_v1(owner, ptr::null(), 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_reorder_entries_v1(owner, 1u32.to_le_bytes().as_ptr(), 4),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_replace_entry_times_v1(owner, ptr::null(), 0),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_replace_entry_times_v1(owner, encoded.as_ptr(), 15),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_correct_last_end_v1(owner, -5, sentinel.as_mut_ptr(), 15),
                INVALID_INPUT
            );
            assert_eq!(state(owner), animated);
            assert_eq!(sentinel, [0xa5; 80]);

            // Full replacement survives CString's NUL-terminated comparison.
            let old_style = [65, 0, 0, 0, 66, 0];
            let equivalent_old = [65, 0, 0, 0, 67, 0];
            let replacement = [90, 0, 0, 0, 0, 0xd8, 0xff, 0xff];
            assert_eq!(
                xy_vsfilter_document_set_prepared_style_v1(
                    owner,
                    0,
                    old_style.as_ptr(),
                    old_style.len()
                ),
                OK
            );
            let before_rename = state(owner);
            assert_eq!(
                xy_vsfilter_document_rename_style_cstring_v1(
                    owner,
                    equivalent_old.as_ptr(),
                    equivalent_old.len(),
                    replacement.as_ptr(),
                    replacement.len()
                ),
                OK
            );
            assert_eq!(state(owner).entries()[0].style, [90, 0, 0xd800, 0xffff]);
            assert_eq!(state(owner).segments(), before_rename.segments());
            let renamed = state(owner);
            assert_eq!(
                xy_vsfilter_document_rename_style_cstring_v1(
                    owner,
                    ptr::null(),
                    1,
                    replacement.as_ptr(),
                    replacement.len()
                ),
                INVALID_INPUT
            );
            assert_eq!(
                xy_vsfilter_document_rename_style_cstring_v1(
                    owner,
                    replacement.as_ptr(),
                    replacement.len(),
                    ptr::null(),
                    2
                ),
                INVALID_INPUT
            );
            assert_eq!(state(owner), renamed);
            assert_eq!(
                xy_vsfilter_document_rename_style_cstring_v1(
                    owner,
                    replacement.as_ptr(),
                    replacement.len(),
                    replacement.as_ptr(),
                    replacement.len()
                ),
                OK
            );
            assert_eq!(state(owner), renamed); // overlapped inputs, same full value
            assert_eq!(
                xy_vsfilter_document_set_prepared_style_v1(owner, 0, ptr::null(), 0),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_rename_style_cstring_v1(
                    owner,
                    ptr::null(),
                    0,
                    replacement.as_ptr(),
                    replacement.len()
                ),
                OK
            );
            assert_eq!(state(owner).entries()[0].style, [90, 0, 0xd800, 0xffff]);

            assert_eq!(
                xy_vsfilter_document_set_prepared_text_v1(owner, 0, 1, ptr::null(), 0),
                OK
            );
            assert!(state(owner).entries()[0].text.is_empty());
            assert!(state(owner).entries()[0].unicode);
            let style = [0, 0, 0, 0xd8];
            assert_eq!(
                xy_vsfilter_document_set_prepared_style_v1(owner, 0, style.as_ptr(), style.len()),
                OK
            );
            assert_eq!(state(owner).entries()[0].style, [0, 0xd800]);
            assert_eq!(
                xy_vsfilter_document_set_entry_time_v1(owner, 0, 1, i64::MAX),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_set_entry_time_v1(owner, 0, 0, i64::MIN),
                OK
            );
            assert_eq!(state(owner).segments(), animated.segments());
            assert_eq!(state(destination), animated); // independent copy payload
            assert_eq!(read_snapshot(entry_snapshot), entry_bytes);
            assert_eq!(read_snapshot(segment_snapshot), segment_bytes);
            assert_eq!(
                xy_vsfilter_document_reorder_entries_v1(owner, 0u32.to_le_bytes().as_ptr(), 4),
                OK
            );
            assert!(state(owner).segments().is_empty()); // reversed stored interval
            let mut times = vec![];
            times.extend_from_slice(&0i64.to_le_bytes());
            times.extend_from_slice(&10i64.to_le_bytes());
            assert_eq!(
                xy_vsfilter_document_replace_entry_times_v1(owner, times.as_ptr(), times.len()),
                OK
            );
            assert_eq!(state(owner).segments()[0].start, 0);
            assert!(!state(owner).segments()[0].animated);
            assert_eq!(
                xy_vsfilter_document_correct_last_end_v1(owner, 4, sentinel.as_mut_ptr(), 16),
                OK
            );
            assert_eq!(&sentinel[..8], &[1, 0, 0, 0, 0, 0, 0, 0]);
            assert_eq!(i64::from_le_bytes(sentinel[8..16].try_into().unwrap()), 4);
            assert_eq!(state(owner).entries()[0].end, 4);
            assert_eq!(state(owner).segments()[0].end, 10); // correction does not rebuild
            assert_eq!(xy_vsfilter_document_rebuild_segments_v1(owner), OK);
            assert_eq!(state(owner).segments()[0].end, 4);

            assert_eq!(xy_vsfilter_document_remove_entries_v1(destination), OK);
            let stale = state(destination);
            assert!(stale.entries().is_empty());
            assert!(!stale.segments().is_empty());
            assert_eq!(
                add(destination, 0, &encoded),
                (UNRESOLVED_REFERENCE, [0xa5; 8])
            );
            assert_eq!(state(destination), stale);
            assert_eq!(xy_vsfilter_document_clear_storage_v1(destination), OK);
            assert_eq!(
                add(destination, 0, &entry(2, 2)),
                (OK, [0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff])
            );
            assert!(state(destination).entries().is_empty());
            assert_eq!(
                add(destination, 1, &entry(2, 2)),
                (OK, [1, 0, 0, 0, 0, 0, 0, 0])
            );
            assert_eq!(state(destination).entries().len(), 1);
            assert!(state(destination).segments().is_empty());
            assert_eq!(xy_vsfilter_document_clear_storage_v1(destination), OK);
            sentinel.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_correct_last_end_v1(destination, 0, sentinel.as_mut_ptr(), 16),
                OK
            );
            assert_eq!(&sentinel[..16], &[0; 16]);
            assert_eq!(&sentinel[16..], &[0xa5; 64]);
            assert_eq!(
                xy_vsfilter_document_reorder_entries_v1(destination, ptr::null(), 0),
                OK
            );
            assert_eq!(
                xy_vsfilter_document_replace_entry_times_v1(destination, ptr::null(), 0),
                OK
            );

            assert_eq!(xy_vsfilter_document_destroy_v1(owner), OK);
            sentinel.fill(0xa5);
            assert_eq!(
                xy_vsfilter_document_counts_v1(owner, sentinel.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(xy_vsfilter_document_destroy_v1(owner), BAD_HANDLE);
            assert_eq!(sentinel, [0xa5; 80]);
            assert_eq!(read_snapshot(entry_snapshot), entry_bytes); // survives owner destruction
            assert_eq!(read_snapshot(segment_snapshot), segment_bytes);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(entry_snapshot), OK);
            assert_eq!(
                xy_vsfilter_document_snapshot_drop_v1(entry_snapshot),
                BAD_HANDLE
            );
            assert_eq!(
                xy_vsfilter_document_snapshot_size_v1(entry_snapshot, sentinel.as_mut_ptr(), 8),
                BAD_HANDLE
            );
            assert_eq!(sentinel, [0xa5; 80]);
            assert_eq!(xy_vsfilter_document_snapshot_drop_v1(segment_snapshot), OK);
            assert_eq!(xy_vsfilter_document_destroy_v1(destination), OK);
            let fresh = create();
            assert!(fresh > owner);
            assert!(fresh > entry_snapshot);
            assert_eq!(xy_vsfilter_document_destroy_v1(fresh), OK);
        }
    }
}
