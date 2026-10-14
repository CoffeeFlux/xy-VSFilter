// SPDX-License-Identifier: GPL-2.0-or-later
//! Behavior-preserving components of xy-VSFilter.
//!
//! The pinned C++ implementation, including its malformed-input behavior,
//! is the reference. These routines are not standards-conformance parsers.
#![forbid(unsafe_code)]
#![no_std]

extern crate alloc;

pub mod ass_tags;
pub mod be_blur;
pub mod be_filter;
pub mod bilinear;
pub mod drawing;
pub mod drawing_tokens;
pub mod dvb;
pub mod fields;
pub mod fill;
pub mod font;
pub mod golomb;
pub mod hdmv;
pub mod mask;
pub mod realtext;
pub mod realtext_attributes;
pub mod realtext_document;
pub mod realtext_format;
pub mod realtext_lists;
pub mod realtext_lower;
pub mod realtext_tag;
pub mod realtext_text;
pub mod realtext_timecode;
pub mod realtext_timeline;
pub mod segment_insert;
pub mod segments;
pub mod subrip;
pub mod text;
pub mod text_reader;
pub mod timeline_search;
pub mod webvtt;

pub mod transpose;

pub mod byte_float;
pub mod cc;

pub mod gauss_integer;

pub mod vobsub_outline;
pub mod vobsub_packet;
pub mod vobsub_path;
pub mod vobsub_pixels;
pub mod vobsub_rle;
pub mod vobsub_scale;
pub mod vobsub_segment;
pub mod vobsub_trim;

pub mod find_char;

pub mod microdvd;

pub mod mpl2;

pub mod remove_tags;

pub mod case_replace;

pub mod sami;

pub mod encoding;

pub mod ass_dialogue;

pub mod ass_style;

pub mod ass_headers;

pub mod font_records;

pub mod microdvd_style;

pub mod sami_file;

pub mod subrip_cue;
