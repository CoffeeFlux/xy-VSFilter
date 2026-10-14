// SPDX-License-Identifier: GPL-2.0-or-later
//! Bounded supplied-value codec/equality trace; no native defaults or owner ABI.
use std::io::{self, BufRead, Read};
use xy_vsfilter_compat::document_style::{decode, encode};
const MAX_VALUES: usize = 256;
const MAX_WIRE: usize = 1024 * 1024;
fn nibble(value: u8) -> Result<u8, &'static str> {
    match value {
        b'0'..=b'9' => Ok(value - b'0'),
        b'a'..=b'f' => Ok(value - b'a' + 10),
        b'A'..=b'F' => Ok(value - b'A' + 10),
        _ => Err("nonhex input"),
    }
}
fn run() -> Result<(), String> {
    let stdin = io::stdin();
    let mut input = stdin.lock();
    let mut values = Vec::new();
    loop {
        let mut line = Vec::new();
        (&mut input)
            .take((MAX_WIRE * 2 + 3) as u64)
            .read_until(b'\n', &mut line)
            .map_err(|e| e.to_string())?;
        if line.is_empty() {
            break;
        }
        if values.len() == MAX_VALUES {
            return Err("too many values".into());
        }
        if line.last() == Some(&b'\n') {
            line.pop();
        }
        if line.last() == Some(&b'\r') {
            line.pop();
        }
        if line.len() > MAX_WIRE * 2 || line.len() % 2 != 0 {
            return Err("wire length limit or odd hex length".into());
        }
        let wire: Result<Vec<u8>, _> = line
            .chunks_exact(2)
            .map(|pair| Ok((nibble(pair[0])? << 4) | nibble(pair[1])?))
            .collect::<Result<_, &'static str>>();
        values.push(decode(&wire.map_err(str::to_owned)?).map_err(|e| format!("decode: {e:?}"))?);
    }
    const HEX: &[u8; 16] = b"0123456789abcdef";
    for (index, value) in values.iter().enumerate() {
        let wire = encode(value).map_err(|e| format!("encode: {e:?}"))?;
        let mut hex = String::with_capacity(wire.len() * 2);
        for byte in wire {
            hex.push(HEX[(byte >> 4) as usize] as char);
            hex.push(HEX[(byte & 15) as usize] as char);
        }
        println!("value {index} {hex}");
    }
    for (i, a) in values.iter().enumerate() {
        for (j, b) in values.iter().enumerate() {
            println!("equal {i} {j} {}", u8::from(a.native_compatible_eq(b)));
        }
    }
    println!("document-style-codec-v1 complete");
    Ok(())
}
fn main() {
    if let Err(error) = run() {
        eprintln!("document-style-codec: {error}");
        std::process::exit(1);
    }
}
