// SPDX-License-Identifier: GPL-2.0-or-later
//! Bounded v1 document value encoding. Explicit little-endian integers and raw
//! UTF-16 units; no native/Rust struct layout, pointers, Unicode interpretation,
//! ownership handles, or callbacks. Exact lengths and reserved-zero bytes are
//! required. Allocation failure is outside this codec's compatibility domain.
use crate::document::{DocumentSegment, PreparedEntry, QueryResult};
use alloc::vec::Vec;

pub const ENTRY_HEADER_BYTES: usize = 64;
pub const SEGMENT_HEADER_BYTES: usize = 32;
pub const QUERY_BYTES: usize = 16;
pub const COUNTS_BYTES: usize = 8;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    InvalidShape,
    InvalidBool,
    NonzeroReserved,
    CountOverflow,
    LengthOverflow,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct EntryHeader {
    pub start: i64,
    pub end: i64,
    pub margins: [i32; 4],
    pub layer: i32,
    pub readorder: i32,
    pub unicode: bool,
    /// Text, style, actor, effect lengths in u16 code units.
    pub lengths: [u32; 4],
}
impl EntryHeader {
    pub fn from_entry(entry: &PreparedEntry) -> Result<Self, Error> {
        Ok(Self {
            start: entry.start,
            end: entry.end,
            margins: entry.margins,
            layer: entry.layer,
            readorder: entry.readorder,
            unicode: entry.unicode,
            lengths: [
                count(entry.text.len())?,
                count(entry.style.len())?,
                count(entry.actor.len())?,
                count(entry.effect.len())?,
            ],
        })
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SegmentHeader {
    pub start: i64,
    pub end: i64,
    pub animated: bool,
    pub member_count: u32,
}
impl SegmentHeader {
    pub fn from_segment(segment: &DocumentSegment) -> Result<Self, Error> {
        Ok(Self {
            start: segment.start,
            end: segment.end,
            animated: segment.animated,
            member_count: count(segment.entries.len())?,
        })
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Counts {
    pub entries: u32,
    pub segments: u32,
}

fn count(value: usize) -> Result<u32, Error> {
    if value > i32::MAX as usize {
        Err(Error::CountOverflow)
    } else {
        Ok(value as u32)
    }
}
fn checked_count(value: u32) -> Result<(), Error> {
    if value > i32::MAX as u32 {
        Err(Error::CountOverflow)
    } else {
        Ok(())
    }
}
fn exact(bytes: &[u8], length: usize) -> Result<(), Error> {
    if bytes.len() == length {
        Ok(())
    } else {
        Err(Error::InvalidShape)
    }
}
fn read<const N: usize>(bytes: &[u8], offset: usize) -> Result<[u8; N], Error> {
    bytes
        .get(offset..offset.checked_add(N).ok_or(Error::LengthOverflow)?)
        .ok_or(Error::InvalidShape)?
        .try_into()
        .map_err(|_| Error::InvalidShape)
}
fn u32_at(bytes: &[u8], offset: usize) -> Result<u32, Error> {
    Ok(u32::from_le_bytes(read(bytes, offset)?))
}
fn bool_at(bytes: &[u8], offset: usize) -> Result<bool, Error> {
    match u32_at(bytes, offset)? {
        0 => Ok(false),
        1 => Ok(true),
        _ => Err(Error::InvalidBool),
    }
}
fn total(header: usize, lengths: &[u32], width: usize) -> Result<usize, Error> {
    let mut size = header;
    for &length in lengths {
        checked_count(length)?;
        size = size
            .checked_add(
                (length as usize)
                    .checked_mul(width)
                    .ok_or(Error::LengthOverflow)?,
            )
            .ok_or(Error::LengthOverflow)?;
    }
    if size > isize::MAX as usize {
        return Err(Error::LengthOverflow);
    }
    Ok(size)
}

pub fn encode_entry_header(h: &EntryHeader) -> Result<[u8; ENTRY_HEADER_BYTES], Error> {
    for &length in &h.lengths {
        checked_count(length)?;
    }
    let mut bytes = [0; ENTRY_HEADER_BYTES];
    bytes[0..8].copy_from_slice(&h.start.to_le_bytes());
    bytes[8..16].copy_from_slice(&h.end.to_le_bytes());
    for (i, value) in h.margins.iter().enumerate() {
        bytes[16 + i * 4..20 + i * 4].copy_from_slice(&value.to_le_bytes());
    }
    bytes[32..36].copy_from_slice(&h.layer.to_le_bytes());
    bytes[36..40].copy_from_slice(&h.readorder.to_le_bytes());
    bytes[40..44].copy_from_slice(&u32::from(h.unicode).to_le_bytes());
    for (i, value) in h.lengths.iter().enumerate() {
        bytes[44 + i * 4..48 + i * 4].copy_from_slice(&value.to_le_bytes());
    }
    Ok(bytes)
}
pub fn decode_entry_header(bytes: &[u8]) -> Result<EntryHeader, Error> {
    exact(bytes, ENTRY_HEADER_BYTES)?;
    if u32_at(bytes, 60)? != 0 {
        return Err(Error::NonzeroReserved);
    }
    let mut lengths = [0; 4];
    let mut margins = [0; 4];
    for i in 0..4 {
        lengths[i] = u32_at(bytes, 44 + i * 4)?;
        checked_count(lengths[i])?;
        margins[i] = i32::from_le_bytes(read(bytes, 16 + i * 4)?);
    }
    Ok(EntryHeader {
        start: i64::from_le_bytes(read(bytes, 0)?),
        end: i64::from_le_bytes(read(bytes, 8)?),
        margins,
        layer: i32::from_le_bytes(read(bytes, 32)?),
        readorder: i32::from_le_bytes(read(bytes, 36)?),
        unicode: bool_at(bytes, 40)?,
        lengths,
    })
}
pub fn encode_entry(entry: &PreparedEntry) -> Result<Vec<u8>, Error> {
    let h = EntryHeader::from_entry(entry)?;
    let size = total(ENTRY_HEADER_BYTES, &h.lengths, 2)?;
    let mut bytes = Vec::with_capacity(size);
    bytes.extend_from_slice(&encode_entry_header(&h)?);
    for field in [&entry.text, &entry.style, &entry.actor, &entry.effect] {
        for unit in field {
            bytes.extend_from_slice(&unit.to_le_bytes());
        }
    }
    Ok(bytes)
}
pub fn decode_entry(bytes: &[u8]) -> Result<PreparedEntry, Error> {
    let h = decode_entry_header(bytes.get(..ENTRY_HEADER_BYTES).ok_or(Error::InvalidShape)?)?;
    exact(bytes, total(ENTRY_HEADER_BYTES, &h.lengths, 2)?)?;
    let mut offset = ENTRY_HEADER_BYTES;
    let mut field = |length: u32| {
        let end = offset + length as usize * 2;
        let units = bytes[offset..end]
            .chunks_exact(2)
            .map(|b| u16::from_le_bytes([b[0], b[1]]))
            .collect();
        offset = end;
        units
    };
    Ok(PreparedEntry {
        text: field(h.lengths[0]),
        style: field(h.lengths[1]),
        actor: field(h.lengths[2]),
        effect: field(h.lengths[3]),
        unicode: h.unicode,
        margins: h.margins,
        layer: h.layer,
        start: h.start,
        end: h.end,
        readorder: h.readorder,
    })
}
pub fn encode_segment_header(h: &SegmentHeader) -> Result<[u8; SEGMENT_HEADER_BYTES], Error> {
    checked_count(h.member_count)?;
    let mut bytes = [0; SEGMENT_HEADER_BYTES];
    bytes[0..8].copy_from_slice(&h.start.to_le_bytes());
    bytes[8..16].copy_from_slice(&h.end.to_le_bytes());
    bytes[16..20].copy_from_slice(&u32::from(h.animated).to_le_bytes());
    bytes[20..24].copy_from_slice(&h.member_count.to_le_bytes());
    Ok(bytes)
}
pub fn decode_segment_header(bytes: &[u8]) -> Result<SegmentHeader, Error> {
    exact(bytes, SEGMENT_HEADER_BYTES)?;
    if u32_at(bytes, 24)? != 0 || u32_at(bytes, 28)? != 0 {
        return Err(Error::NonzeroReserved);
    }
    let member_count = u32_at(bytes, 20)?;
    checked_count(member_count)?;
    Ok(SegmentHeader {
        start: i64::from_le_bytes(read(bytes, 0)?),
        end: i64::from_le_bytes(read(bytes, 8)?),
        animated: bool_at(bytes, 16)?,
        member_count,
    })
}
pub fn encode_segment(segment: &DocumentSegment) -> Result<Vec<u8>, Error> {
    let h = SegmentHeader::from_segment(segment)?;
    let size = total(SEGMENT_HEADER_BYTES, &[h.member_count], 4)?;
    // Validate every index before any output allocation; stale in-range members
    // remain encodable regardless of the current document's entry count.
    for &index in &segment.entries {
        count(index)?;
    }
    let mut bytes = Vec::with_capacity(size);
    bytes.extend_from_slice(&encode_segment_header(&h)?);
    for &index in &segment.entries {
        bytes.extend_from_slice(&(index as u32).to_le_bytes());
    }
    Ok(bytes)
}
pub fn decode_segment(bytes: &[u8]) -> Result<DocumentSegment, Error> {
    let h = decode_segment_header(
        bytes
            .get(..SEGMENT_HEADER_BYTES)
            .ok_or(Error::InvalidShape)?,
    )?;
    exact(bytes, total(SEGMENT_HEADER_BYTES, &[h.member_count], 4)?)?;
    for unit in bytes[SEGMENT_HEADER_BYTES..].chunks_exact(4) {
        checked_count(u32::from_le_bytes([unit[0], unit[1], unit[2], unit[3]]))?;
    }
    let entries = bytes[SEGMENT_HEADER_BYTES..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes([b[0], b[1], b[2], b[3]]) as usize)
        .collect();
    Ok(DocumentSegment {
        start: h.start,
        end: h.end,
        animated: h.animated,
        entries,
    })
}
pub fn encode_query(query: &QueryResult) -> Result<[u8; QUERY_BYTES], Error> {
    if query.count < 0 {
        return Err(Error::CountOverflow);
    }
    let mut bytes = [0; QUERY_BYTES];
    bytes[..4].copy_from_slice(&query.index.to_le_bytes());
    bytes[4..8].copy_from_slice(&u32::from(query.found).to_le_bytes());
    bytes[8..12].copy_from_slice(&(query.count as u32).to_le_bytes());
    Ok(bytes)
}
pub fn decode_query(bytes: &[u8]) -> Result<QueryResult, Error> {
    exact(bytes, QUERY_BYTES)?;
    if u32_at(bytes, 12)? != 0 {
        return Err(Error::NonzeroReserved);
    }
    let count = u32_at(bytes, 8)?;
    checked_count(count)?;
    Ok(QueryResult {
        index: i32::from_le_bytes(read(bytes, 0)?),
        found: bool_at(bytes, 4)?,
        count: count as i32,
    })
}
pub fn encode_counts(counts: &Counts) -> Result<[u8; COUNTS_BYTES], Error> {
    checked_count(counts.entries)?;
    checked_count(counts.segments)?;
    let mut bytes = [0; COUNTS_BYTES];
    bytes[..4].copy_from_slice(&counts.entries.to_le_bytes());
    bytes[4..].copy_from_slice(&counts.segments.to_le_bytes());
    Ok(bytes)
}
pub fn decode_counts(bytes: &[u8]) -> Result<Counts, Error> {
    exact(bytes, COUNTS_BYTES)?;
    let counts = Counts {
        entries: u32_at(bytes, 0)?,
        segments: u32_at(bytes, 4)?,
    };
    checked_count(counts.entries)?;
    checked_count(counts.segments)?;
    Ok(counts)
}

/// Native Sort comparator inputs only; row ordinal supplies original identity.
/// Native adapters retain actual STSEntry width and CRT comparators. This codec
/// neither sorts nor widens/emulates native comparator arithmetic.
pub const SORT_HEADER_BYTES: usize = 8;
pub const SORT_RECORD_BYTES: usize = 16;
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SortScalar {
    pub start: i64,
    pub layer: i32,
    pub readorder: i32,
}
pub fn encode_sort_snapshot(entries: &[PreparedEntry]) -> Result<Vec<u8>, Error> {
    let count = count(entries.len())?;
    let length = total(SORT_HEADER_BYTES, &[count], SORT_RECORD_BYTES)?;
    let mut bytes = Vec::with_capacity(length);
    bytes.extend_from_slice(&count.to_le_bytes());
    bytes.extend_from_slice(&0u32.to_le_bytes());
    for entry in entries {
        bytes.extend_from_slice(&entry.start.to_le_bytes());
        bytes.extend_from_slice(&entry.layer.to_le_bytes());
        bytes.extend_from_slice(&entry.readorder.to_le_bytes());
    }
    Ok(bytes)
}
pub fn decode_sort_snapshot(bytes: &[u8]) -> Result<Vec<SortScalar>, Error> {
    let count = u32_at(bytes, 0)?;
    if u32_at(bytes, 4)? != 0 {
        return Err(Error::NonzeroReserved);
    }
    exact(
        bytes,
        total(SORT_HEADER_BYTES, &[count], SORT_RECORD_BYTES)?,
    )?;
    let mut records = Vec::with_capacity(count as usize);
    for row in bytes[SORT_HEADER_BYTES..].chunks_exact(SORT_RECORD_BYTES) {
        records.push(SortScalar {
            start: i64::from_le_bytes(read(row, 0)?),
            layer: i32::from_le_bytes(read(row, 8)?),
            readorder: i32::from_le_bytes(read(row, 12)?),
        });
    }
    Ok(records)
}

/// Immutable scalar time pairs in storage order. No payload string cloning.
pub const TIME_HEADER_BYTES: usize = 8;
pub const TIME_RECORD_BYTES: usize = 16;
pub fn encode_time_snapshot(entries: &[PreparedEntry]) -> Result<Vec<u8>, Error> {
    let count = count(entries.len())?;
    let mut bytes = Vec::with_capacity(total(TIME_HEADER_BYTES, &[count], TIME_RECORD_BYTES)?);
    bytes.extend_from_slice(&count.to_le_bytes());
    bytes.extend_from_slice(&0u32.to_le_bytes());
    for entry in entries {
        bytes.extend_from_slice(&entry.start.to_le_bytes());
        bytes.extend_from_slice(&entry.end.to_le_bytes());
    }
    Ok(bytes)
}
pub fn decode_time_snapshot(bytes: &[u8]) -> Result<Vec<(i64, i64)>, Error> {
    let count = u32_at(bytes, 0)?;
    if u32_at(bytes, 4)? != 0 {
        return Err(Error::NonzeroReserved);
    }
    exact(
        bytes,
        total(TIME_HEADER_BYTES, &[count], TIME_RECORD_BYTES)?,
    )?;
    bytes[TIME_HEADER_BYTES..]
        .chunks_exact(TIME_RECORD_BYTES)
        .map(|row| {
            Ok((
                i64::from_le_bytes(read(row, 0)?),
                i64::from_le_bytes(read(row, 8)?),
            ))
        })
        .collect()
}

/// Conflict relocation packet, not a complete AddStyle request. Names remain
/// full raw units; map-key truncation and entry-name comparison are core choices.
/// Header16: old units:u32@0, new units:u32@4, style bytes:u64@8, then both
/// UTF16LE arrays and exactly one existing non-null style wire.
pub const STYLE_RENAME_HEADER_BYTES: usize = 16;
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PreparedStyleRename {
    pub old: Vec<u16>,
    pub new: Vec<u16>,
    /// Pending owned input; consumed only after core preflight succeeds.
    pub previous: Option<crate::document_style::StyleValue>,
}
fn style_error(error: crate::document_style::Error) -> Error {
    use crate::document_style::Error as S;
    match error {
        S::InvalidShape => Error::InvalidShape,
        S::InvalidBool => Error::InvalidBool,
        S::NonzeroReserved => Error::NonzeroReserved,
        S::CountOverflow => Error::CountOverflow,
        S::LengthOverflow => Error::LengthOverflow,
    }
}
fn style_rename_shape(old: u32, new: u32, style: u64) -> Result<(usize, usize), Error> {
    let names_end = total(STYLE_RENAME_HEADER_BYTES, &[old, new], 2)?;
    let style = usize::try_from(style).map_err(|_| Error::LengthOverflow)?;
    let end = names_end
        .checked_add(style)
        .filter(|&n| n <= isize::MAX as usize)
        .ok_or(Error::LengthOverflow)?;
    Ok((names_end, end))
}
pub fn encode_style_rename(value: &PreparedStyleRename) -> Result<Vec<u8>, Error> {
    let old = count(value.old.len())?;
    let new = count(value.new.len())?;
    let style = crate::document_style::encode(value.previous.as_ref().ok_or(Error::InvalidShape)?)
        .map_err(style_error)?;
    let (_, length) = style_rename_shape(old, new, style.len() as u64)?;
    let mut bytes = Vec::with_capacity(length);
    bytes.extend_from_slice(&old.to_le_bytes());
    bytes.extend_from_slice(&new.to_le_bytes());
    bytes.extend_from_slice(&(style.len() as u64).to_le_bytes());
    for unit in value.old.iter().chain(&value.new) {
        bytes.extend_from_slice(&unit.to_le_bytes());
    }
    bytes.extend_from_slice(&style);
    Ok(bytes)
}
pub fn decode_style_rename(bytes: &[u8]) -> Result<PreparedStyleRename, Error> {
    let old = u32_at(bytes, 0)?;
    let new = u32_at(bytes, 4)?;
    let style = u64::from_le_bytes(read(bytes, 8)?);
    let (names_end, length) = style_rename_shape(old, new, style)?;
    exact(bytes, length)?;
    let old_end = STYLE_RENAME_HEADER_BYTES + old as usize * 2;
    let units = |part: &[u8]| {
        part.chunks_exact(2)
            .map(|b| u16::from_le_bytes([b[0], b[1]]))
            .collect()
    };
    // Validate the entire style before allocating name arrays.
    let previous = crate::document_style::decode(&bytes[names_end..]).map_err(style_error)?;
    Ok(PreparedStyleRename {
        old: units(&bytes[STYLE_RENAME_HEADER_BYTES..old_end]),
        new: units(&bytes[old_end..names_end]),
        previous: Some(previous),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;
    fn entry() -> PreparedEntry {
        PreparedEntry {
            text: vec![0, 0xd800, 0xdc00, 0xffff],
            style: vec![0xdc00, 0],
            actor: vec![],
            effect: vec![10, 13],
            unicode: true,
            margins: [i32::MIN, -1, 0, i32::MAX],
            layer: i32::MIN,
            readorder: i32::MAX,
            start: i64::MAX,
            end: i64::MIN,
        }
    }
    #[test]
    fn entry_wire_has_exact_offsets_and_retains_raw_units_and_signed_extremes() {
        let e = entry();
        let bytes = encode_entry(&e).unwrap();
        assert_eq!(bytes.len(), 64 + 16);
        assert_eq!(&bytes[..8], &i64::MAX.to_le_bytes());
        assert_eq!(&bytes[8..16], &i64::MIN.to_le_bytes());
        assert_eq!(&bytes[16..20], &i32::MIN.to_le_bytes());
        assert_eq!(&bytes[32..36], &i32::MIN.to_le_bytes());
        assert_eq!(&bytes[36..40], &i32::MAX.to_le_bytes());
        assert_eq!(&bytes[40..44], &1u32.to_le_bytes());
        assert_eq!(
            &bytes[44..60],
            &[4u32, 2, 0, 2]
                .into_iter()
                .flat_map(u32::to_le_bytes)
                .collect::<Vec<_>>()
        );
        assert_eq!(&bytes[60..64], &[0; 4]);
        assert_eq!(&bytes[64..72], &[0, 0, 0, 0xd8, 0, 0xdc, 0xff, 0xff]);
        assert_eq!(decode_entry(&bytes).unwrap(), e);
        let h = EntryHeader::from_entry(&e).unwrap();
        assert_eq!(
            decode_entry_header(&encode_entry_header(&h).unwrap()).unwrap(),
            h
        );
    }
    #[test]
    fn entry_wire_rejects_every_truncation_and_extra_payload_before_decoding() {
        let bytes = encode_entry(&entry()).unwrap();
        for length in 0..bytes.len() {
            assert!(decode_entry(&bytes[..length]).is_err(), "prefix {length}");
        }
        let mut extra = bytes.clone();
        extra.push(0);
        assert_eq!(decode_entry(&extra), Err(Error::InvalidShape));
        let mut bad = bytes.clone();
        bad[40..44].copy_from_slice(&2u32.to_le_bytes());
        assert_eq!(decode_entry(&bad), Err(Error::InvalidBool));
        let mut bad = bytes.clone();
        bad[60] = 1;
        assert_eq!(decode_entry(&bad), Err(Error::NonzeroReserved));
        let mut bad = bytes.clone();
        bad[44..48].copy_from_slice(&u32::MAX.to_le_bytes());
        assert_eq!(decode_entry(&bad), Err(Error::CountOverflow));
        let mut bad = bytes.clone();
        bad[44..48].copy_from_slice(&3u32.to_le_bytes());
        assert_eq!(decode_entry(&bad), Err(Error::InvalidShape));
        assert!(decode_entry_header(&bytes).is_err());
    }
    #[test]
    fn segment_wire_preserves_stale_members_and_checks_all_shape_fields() {
        let segment = DocumentSegment {
            start: i64::MIN,
            end: i64::MAX,
            animated: true,
            entries: vec![0, 3, i32::MAX as usize],
        };
        let bytes = encode_segment(&segment).unwrap();
        assert_eq!(bytes.len(), 44);
        assert_eq!(&bytes[16..20], &1u32.to_le_bytes());
        assert_eq!(&bytes[20..24], &3u32.to_le_bytes());
        assert_eq!(&bytes[24..32], &[0; 8]);
        assert_eq!(decode_segment(&bytes).unwrap(), segment);
        let header = SegmentHeader::from_segment(&segment).unwrap();
        assert_eq!(
            decode_segment_header(&encode_segment_header(&header).unwrap()).unwrap(),
            header
        );
        for length in 0..bytes.len() {
            assert!(decode_segment(&bytes[..length]).is_err());
        }
        let mut bad = bytes.clone();
        bad.push(0);
        assert_eq!(decode_segment(&bad), Err(Error::InvalidShape));
        for offset in [24, 28] {
            let mut bad = bytes.clone();
            bad[offset] = 1;
            assert_eq!(decode_segment(&bad), Err(Error::NonzeroReserved));
        }
        let mut bad = bytes.clone();
        bad[16] = 2;
        assert_eq!(decode_segment(&bad), Err(Error::InvalidBool));
        let mut bad = bytes.clone();
        bad[20..24].copy_from_slice(&u32::MAX.to_le_bytes());
        assert_eq!(decode_segment(&bad), Err(Error::CountOverflow));
        let mut bad = bytes.clone();
        bad[32..36].copy_from_slice(&u32::MAX.to_le_bytes());
        assert_eq!(decode_segment(&bad), Err(Error::CountOverflow));
        assert_eq!(
            encode_segment(&DocumentSegment {
                entries: vec![usize::MAX],
                ..segment
            }),
            Err(Error::CountOverflow)
        );
    }
    #[test]
    fn empty_dynamic_records_and_query_output_distinctions_roundtrip() {
        let mut empty = entry();
        empty.text.clear();
        empty.style.clear();
        empty.actor.clear();
        empty.effect.clear();
        empty.unicode = false;
        assert_eq!(encode_entry(&empty).unwrap().len(), ENTRY_HEADER_BYTES);
        assert_eq!(decode_entry(&encode_entry(&empty).unwrap()).unwrap(), empty);
        let segment = DocumentSegment {
            start: 1,
            end: 1,
            animated: false,
            entries: vec![],
        };
        assert_eq!(
            encode_segment(&segment).unwrap().len(),
            SEGMENT_HEADER_BYTES
        );
        assert_eq!(
            decode_segment(&encode_segment(&segment).unwrap()).unwrap(),
            segment
        );
        for query in [
            QueryResult {
                index: -1,
                found: false,
                count: 0,
            },
            QueryResult {
                index: 0,
                found: false,
                count: 0,
            },
            QueryResult {
                index: 2,
                found: false,
                count: 2,
            },
            QueryResult {
                index: 1,
                found: true,
                count: 2,
            },
            QueryResult {
                index: i32::MIN,
                found: false,
                count: i32::MAX,
            },
        ] {
            let bytes = encode_query(&query).unwrap();
            assert_eq!(decode_query(&bytes).unwrap(), query);
            for length in 0..QUERY_BYTES {
                assert!(decode_query(&bytes[..length]).is_err());
            }
            let mut bad = bytes;
            bad[4] = 2;
            assert_eq!(decode_query(&bad), Err(Error::InvalidBool));
            let mut bad = bytes;
            bad[12] = 1;
            assert_eq!(decode_query(&bad), Err(Error::NonzeroReserved));
        }
        assert_eq!(
            encode_query(&QueryResult {
                index: 0,
                found: false,
                count: -1
            }),
            Err(Error::CountOverflow)
        );
    }
    #[test]
    fn native_count_bounds_and_combined_allocation_overflow_are_explicit() {
        let counts = Counts {
            entries: i32::MAX as u32,
            segments: 0,
        };
        let bytes = encode_counts(&counts).unwrap();
        assert_eq!(decode_counts(&bytes).unwrap(), counts);
        for length in 0..COUNTS_BYTES {
            assert!(decode_counts(&bytes[..length]).is_err());
        }
        assert_eq!(
            encode_counts(&Counts {
                entries: u32::MAX,
                segments: 0
            }),
            Err(Error::CountOverflow)
        );
        assert_eq!(decode_counts(&[255; 8]), Err(Error::CountOverflow));
        assert_eq!(total(64, &[u32::MAX], 2), Err(Error::CountOverflow));
        assert_eq!(total(usize::MAX, &[1], 2), Err(Error::LengthOverflow));
        assert_eq!(
            total(isize::MAX as usize, &[1], 2),
            Err(Error::LengthOverflow)
        );
        let mut h = EntryHeader::from_entry(&entry()).unwrap();
        h.lengths[0] = u32::MAX;
        assert_eq!(encode_entry_header(&h), Err(Error::CountOverflow));
        let h = SegmentHeader {
            start: 0,
            end: 0,
            animated: false,
            member_count: u32::MAX,
        };
        assert_eq!(encode_segment_header(&h), Err(Error::CountOverflow));
    }
    #[test]
    fn prepared_style_rename_packet_exact_shape_and_raw_units() {
        let mut style = alloc::vec![0u8;crate::document_style::HEADER_BYTES+4];
        style[64..68].copy_from_slice(&2u32.to_le_bytes());
        style[192..].copy_from_slice(&[0, 216, 255, 220]);
        style[72..80].copy_from_slice(&0x7ff0000000000042u64.to_le_bytes());
        let request = PreparedStyleRename {
            old: alloc::vec![65, 0, 0xd800],
            new: alloc::vec![66, 0, 0xdcff],
            previous: Some(crate::document_style::decode(&style).unwrap()),
        };
        let bytes = encode_style_rename(&request).unwrap();
        assert_eq!(&bytes[..4], &3u32.to_le_bytes());
        assert_eq!(&bytes[4..8], &3u32.to_le_bytes());
        assert_eq!(&bytes[8..16], &(style.len() as u64).to_le_bytes());
        assert_eq!(decode_style_rename(&bytes).unwrap(), request);
        for length in 0..bytes.len() {
            assert!(
                decode_style_rename(&bytes[..length]).is_err(),
                "prefix {length}"
            );
        }
        let mut extra = bytes.clone();
        extra.push(0);
        assert_eq!(decode_style_rename(&extra), Err(Error::InvalidShape));
        let mut huge = bytes.clone();
        huge[..4].copy_from_slice(&u32::MAX.to_le_bytes());
        assert_eq!(decode_style_rename(&huge), Err(Error::CountOverflow));
        huge = bytes.clone();
        huge[8..16].copy_from_slice(&u64::MAX.to_le_bytes());
        assert_eq!(decode_style_rename(&huge), Err(Error::LengthOverflow));
        let mut invalid = bytes;
        invalid[16 + 12 + 56] = 2;
        assert_eq!(decode_style_rename(&invalid), Err(Error::InvalidBool));
        let mut missing = request;
        missing.previous = None;
        assert_eq!(encode_style_rename(&missing), Err(Error::InvalidShape));
    }
    #[test]
    fn sort_snapshot_exact_shape_and_extreme_scalar_bits() {
        let empty = encode_sort_snapshot(&[]).unwrap();
        assert_eq!(empty, alloc::vec![0;8]);
        assert!(decode_sort_snapshot(&empty).unwrap().is_empty());
        let mut bytes = alloc::vec![];
        bytes.extend_from_slice(&2u32.to_le_bytes());
        bytes.extend_from_slice(&0u32.to_le_bytes());
        for (start, layer, readorder) in [
            (i64::MIN, i32::MAX, i32::MIN),
            (i64::MAX, i32::MIN, i32::MAX),
        ] {
            bytes.extend_from_slice(&start.to_le_bytes());
            bytes.extend_from_slice(&layer.to_le_bytes());
            bytes.extend_from_slice(&readorder.to_le_bytes());
        }
        let decoded = decode_sort_snapshot(&bytes).unwrap();
        assert_eq!(
            decoded,
            alloc::vec![
                SortScalar {
                    start: i64::MIN,
                    layer: i32::MAX,
                    readorder: i32::MIN
                },
                SortScalar {
                    start: i64::MAX,
                    layer: i32::MIN,
                    readorder: i32::MAX
                }
            ]
        );
        let entries: Vec<_> = decoded
            .iter()
            .map(|row| PreparedEntry {
                text: alloc::vec![0xd800, 0, 0xdcff],
                style: alloc::vec![],
                actor: alloc::vec![],
                effect: alloc::vec![],
                unicode: true,
                margins: [0; 4],
                layer: row.layer,
                start: row.start,
                end: row.start,
                readorder: row.readorder,
            })
            .collect();
        assert_eq!(encode_sort_snapshot(&entries).unwrap(), bytes);
        for prefix in 0..bytes.len() {
            assert!(
                decode_sort_snapshot(&bytes[..prefix]).is_err(),
                "prefix {prefix}"
            );
        }
        let mut extra = bytes.clone();
        extra.push(0);
        assert_eq!(decode_sort_snapshot(&extra), Err(Error::InvalidShape));
        for index in 4..8 {
            let mut bad = bytes.clone();
            bad[index] = 1;
            assert_eq!(decode_sort_snapshot(&bad), Err(Error::NonzeroReserved));
        }
        let mut bad = bytes;
        bad[..4].copy_from_slice(&u32::MAX.to_le_bytes());
        assert_eq!(decode_sort_snapshot(&bad), Err(Error::CountOverflow));
        #[cfg(target_pointer_width = "32")]
        assert_eq!(
            total(SORT_HEADER_BYTES, &[i32::MAX as u32], SORT_RECORD_BYTES),
            Err(Error::LengthOverflow)
        );
        #[cfg(target_pointer_width = "64")]
        assert_eq!(
            total(SORT_HEADER_BYTES, &[i32::MAX as u32], SORT_RECORD_BYTES),
            Ok(34_359_738_360)
        );
    }
    #[test]
    fn scalar_time_snapshot_bounds_and_raw_endpoints() {
        let empty = encode_time_snapshot(&[]).unwrap();
        assert_eq!(empty, [0; 8]);
        let mut bytes = vec![2, 0, 0, 0, 0, 0, 0, 0];
        for pair in [(i64::MIN, i64::MAX), (8, -2)] {
            bytes.extend_from_slice(&pair.0.to_le_bytes());
            bytes.extend_from_slice(&pair.1.to_le_bytes());
        }
        assert_eq!(
            decode_time_snapshot(&bytes).unwrap(),
            [(i64::MIN, i64::MAX), (8, -2)]
        );
        let entries: Vec<_> = [(i64::MIN, i64::MAX), (8, -2)]
            .into_iter()
            .map(|(start, end)| {
                let mut value = entry();
                value.start = start;
                value.end = end;
                value
            })
            .collect();
        assert_eq!(encode_time_snapshot(&entries).unwrap(), bytes);
        for end in 0..bytes.len() {
            assert!(decode_time_snapshot(&bytes[..end]).is_err());
        }
        let mut bad = bytes.clone();
        bad[4] = 1;
        assert_eq!(decode_time_snapshot(&bad), Err(Error::NonzeroReserved));
        bad = bytes.clone();
        bad.push(0);
        assert_eq!(decode_time_snapshot(&bad), Err(Error::InvalidShape));
        bad[..4].copy_from_slice(&u32::MAX.to_le_bytes());
        assert_eq!(decode_time_snapshot(&bad), Err(Error::CountOverflow));
    }
}
