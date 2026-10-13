// SPDX-License-Identifier: GPL-2.0-or-later
//! Behavior-preserving components of xy-VSFilter.
//!
//! The pinned C++ implementation, including its malformed-input behavior,
//! is the reference. These routines are not standards-conformance parsers.
#![forbid(unsafe_code)]
#![no_std]

extern crate alloc;

pub mod ass_tags;
pub mod font;
pub mod text;
pub mod text_reader;
