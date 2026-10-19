// SPDX-License-Identifier: GPL-2.0-or-later
//! Replay native input operations only. Expected output never drives lookup.
#![forbid(unsafe_code)]
use std::fmt::Write;
use std::sync::Arc;
use xy_vsfilter_compat::font_intern::{Key, Table, Value};

fn hex(bytes: &[u8]) -> String {
    bytes.iter().fold(String::new(), |mut out, v| {
        write!(out, "{v:02x}").unwrap();
        out
    })
}
fn unhex(s: &str) -> Vec<u8> {
    (0..s.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&s[i..i + 2], 16).unwrap())
        .collect()
}
fn decode(bytes: &[u8]) -> Key {
    let n = u32::from_le_bytes(bytes[12..16].try_into().unwrap()) as usize;
    assert_eq!(bytes.len(), 24 + 2 * n);
    assert_eq!(bytes[11], 0);
    assert!(bytes[8..11].iter().all(|&v| v <= 1));
    Key {
        char_set: i32::from_le_bytes(bytes[..4].try_into().unwrap()),
        font_weight: i32::from_le_bytes(bytes[4..8].try_into().unwrap()),
        italic: bytes[8] != 0,
        underline: bytes[9] != 0,
        strikeout: bytes[10] != 0,
        font_size: f64::from_bits(u64::from_le_bytes(bytes[16..24].try_into().unwrap())),
        font_name: bytes[24..]
            .chunks_exact(2)
            .map(|v| u16::from_le_bytes(v.try_into().unwrap()))
            .collect(),
    }
}
fn encode(k: &Key) -> Vec<u8> {
    let mut out = Vec::new();
    out.extend(k.char_set.to_le_bytes());
    out.extend(k.font_weight.to_le_bytes());
    out.extend([
        u8::from(k.italic),
        u8::from(k.underline),
        u8::from(k.strikeout),
        0,
    ]);
    out.extend((k.font_name.len() as u32).to_le_bytes());
    out.extend(k.font_size.to_bits().to_le_bytes());
    for v in &k.font_name {
        out.extend(v.to_le_bytes());
    }
    out
}
fn equal(a: &Key, b: &Key) -> bool {
    let r = a.native_eq(b);
    println!("eq {} {} {}", a.font_name[2], b.font_name[2], u8::from(r));
    r
}
struct Payload(u16);
impl Drop for Payload {
    fn drop(&mut self) {
        println!("destroy {}", self.0);
    }
}
fn main() {
    let input = std::fs::read_to_string(std::env::args().nth(1).unwrap()).unwrap();
    let mut table = Table::new();
    let mut slots: Vec<Option<Arc<Value<Payload>>>> = vec![None; 704];
    let mut next = 1;
    let mut step = 0;
    println!("font-table pointer_bits={}", usize::BITS);
    for row in input.lines() {
        let words: Vec<_> = row.split_whitespace().collect();
        if !matches!(words[0], "input" | "copy" | "drop") {
            continue;
        }
        assert_eq!(words[1].parse::<usize>().unwrap(), step);
        let slot = words[2].parse::<usize>().unwrap();
        let replacement = match words[0] {
            "input" => {
                let key = decode(&unhex(words[4]));
                println!(
                    "input {step} {slot} {:016x} {}",
                    key.hash(),
                    hex(&encode(&key))
                );
                let value = table
                    .intern_with(
                        next,
                        key,
                        |k| {
                            println!("create {}", k.font_name[2]);
                            Ok(Payload(k.font_name[2]))
                        },
                        &mut equal,
                    )
                    .unwrap();
                next += 1;
                Some(value)
            }
            "copy" => {
                let from = words[3].parse::<usize>().unwrap();
                println!("copy {step} {slot} {from}");
                slots[from].clone()
            }
            "drop" => {
                println!("drop {step} {slot}");
                None
            }
            _ => unreachable!(),
        };
        if let Some(old) = slots[slot].take() {
            table.release_with(&old, &mut equal);
            drop(old);
        }
        slots[slot] = replacement;
        let mut aliases = [0u8; 88];
        if let Some(current) = &slots[slot] {
            for (i, other) in slots.iter().enumerate() {
                if let Some(other) = other {
                    let same = current.identity() == other.identity();
                    assert_eq!(same, Arc::ptr_eq(current, other));
                    if same {
                        aliases[i / 8] |= 1 << (i % 8);
                    }
                }
            }
        }
        let key = slots[slot]
            .as_ref()
            .map(|v| hex(&encode(v.key())))
            .unwrap_or_else(|| "-".into());
        println!(
            "state {step} {slot} {} {} {key}",
            table.count(),
            hex(&aliases)
        );
        step += 1;
    }
    assert!(slots.iter().all(Option::is_none));
    println!("font-table-complete steps={step} fonts={}", table.count());
}
