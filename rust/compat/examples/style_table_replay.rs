// SPDX-License-Identifier: GPL-2.0-or-later
//! Replay actual native inputs through the safe ownership engine. Expected
//! native comparison/state rows are not used to make any algorithm decision.
#![forbid(unsafe_code)]
use std::fmt::Write;
use std::sync::Arc;
use xy_vsfilter_compat::{
    document_style::{self, StyleValue},
    rect_intern,
    style_intern::{self, Table, Value},
};

fn hex(bytes: &[u8]) -> String {
    bytes.iter().fold(String::new(), |mut out, v| {
        write!(out, "{v:02x}").unwrap();
        out
    })
}
fn unhex(text: &str) -> Vec<u8> {
    assert_eq!(text.len() % 2, 0);
    (0..text.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&text[i..i + 2], 16).unwrap())
        .collect()
}
fn equal(a: &StyleValue, b: &StyleValue) -> bool {
    let result = a.native_compatible_eq(b);
    println!(
        "eq {} {} {}",
        a.font_name[2],
        b.font_name[2],
        u8::from(result)
    );
    result
}
fn main() {
    let input = std::fs::read_to_string(std::env::args().nth(1).unwrap()).unwrap();
    let mut table = Table::new();
    let mut rectangles = rect_intern::Table::new();
    let mut slots: Vec<Option<Arc<Value>>> = vec![None; 704];
    let mut next = 1;
    let mut step = 0;
    let edges = input.lines().next().unwrap().ends_with("mode=edges");
    println!(
        "style-table pointer_bits={}{}",
        usize::BITS,
        if edges { " mode=edges" } else { "" }
    );
    for row in input.lines() {
        let words: Vec<_> = row.split_whitespace().collect();
        if !matches!(words[0], "input" | "copy" | "drop") {
            continue;
        }
        assert_eq!(words[1].parse::<usize>().unwrap(), step);
        let slot = words[2].parse::<usize>().unwrap();
        let replacement = match words[0] {
            "input" => {
                let value = document_style::decode(&unhex(words[4])).unwrap();
                println!(
                    "input {step} {slot} {:016x} {}",
                    style_intern::hash(&value),
                    hex(&document_style::encode(&value).unwrap())
                );
                let result = table
                    .intern_with(next, value, &mut rectangles, &mut equal)
                    .unwrap();
                next += 1;
                Some(result)
            }
            "copy" => {
                let source = words[3].parse::<usize>().unwrap();
                println!("copy {step} {slot} {source}");
                slots[source].clone()
            }
            "drop" => {
                println!("drop {step} {slot}");
                None
            }
            _ => unreachable!(),
        };
        if let Some(old) = slots[slot].take() {
            table
                .release_with(&old, &mut rectangles, &mut equal)
                .unwrap();
            drop(old);
        }
        slots[slot] = replacement;
        let value = slots[slot]
            .as_ref()
            .map(|v| hex(&document_style::encode(v.style()).unwrap()))
            .unwrap_or_else(|| "-".into());
        println!(
            "state {step} {slot} {} {} {value}",
            table.count(),
            rectangles.count()
        );
        step += 1;
    }
    if edges {
        assert_eq!(slots.iter().filter(|v| v.is_some()).count(), 90);
    } else {
        assert!(slots.iter().all(Option::is_none));
    }
    println!(
        "style-table-complete steps={step} styles={} rects={}",
        table.count(),
        rectangles.count()
    );
}
