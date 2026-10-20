// SPDX-License-Identifier: GPL-2.0-or-later
//! Behavior-preserving components of xy-VSFilter.
//!
//! The pinned C++ implementation, including its malformed-input behavior,
//! is the reference. These routines are not standards-conformance parsers.
#![forbid(unsafe_code)]
#![no_std]

extern crate alloc;

pub mod ass_document;
pub mod ass_fonts;
pub mod ass_input;
pub mod ass_override;
#[cfg(any(target_arch = "x86", test))]
mod ass_override_float;
pub mod ass_style_value;
pub mod ass_tags;
pub mod ass_tree;
pub mod be_blur;
pub mod be_filter;
pub mod bilinear;
pub mod document;
pub mod document_style;
pub mod document_style_add;
pub mod document_style_copy;
pub mod document_style_map;
pub mod document_text;
pub mod document_unknown;
pub mod document_wire;
pub mod drawing;
pub mod drawing_tokens;
pub mod dvb;
pub mod effects;
pub mod fields;
pub mod fill;
pub mod font;
pub mod gaussian_coefficients;
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
pub mod renderer_dc;
pub mod segment_insert;
pub mod segments;
pub mod subrip;
pub mod text;
pub mod text_measure;
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

pub mod pipe_line;

pub mod text_writer;

pub mod layout_rects;

pub mod text_runs;

pub mod wrap_width;

pub mod line_layout;

pub mod text_seek;

pub mod path_align;

pub mod span_merge;
pub mod wide_region;

pub mod arc_queue;

pub mod ellipse;
pub mod ellipse_arc;

pub mod ellipse_matrix;

pub mod path_buffer;

pub mod edge_storage;
pub mod plane;
pub mod raster;
pub mod scan_row;
pub mod span_coverage;

pub mod ssf_stream;

pub mod ssf_arabic;

pub mod ssf_split;

pub mod ssf_literal;

pub mod document_prepare;

pub mod document_open;

pub mod document_search;

pub mod document_export;

pub mod xombie;

pub mod text_document;

pub mod microdvd_document;

pub mod sami_document;

pub mod vtt_document;

pub mod realtext_session;

pub mod realtext_engine;

pub mod usf_conversion;
pub mod usf_model;
pub mod usf_scalar;

pub mod colors;
pub mod html_tag;

pub mod font_intern;
pub mod font_key_wire;
pub mod path_data;
pub mod rect_intern;
pub mod string_flyweight;
pub mod style_intern;
pub mod text_path;

pub mod gaussian_filter;
pub mod scan_convert;
pub mod scan_line;
pub mod word_transform;
