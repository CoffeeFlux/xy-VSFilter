// SPDX-License-Identifier: GPL-2.0-or-later
//! Bounded raw-unit input/value observation protocol, no native defaults.
use std::io::{self, BufRead, Read};
use xy_vsfilter_compat::document_prepare::{prepare, Mode};
fn units(length: &str, hex: &str) -> Result<Vec<u16>, String> {
    let n: usize = length.parse().map_err(|_| "invalid length")?;
    if n > 512 {
        return Err("field exceeds bound".into());
    }
    if n == 0 {
        return if hex == "-" {
            Ok(Vec::new())
        } else {
            Err("empty encoding".into())
        };
    }
    if hex.len() != n * 4 || !hex.bytes().all(|b| b.is_ascii_hexdigit()) {
        return Err("invalid raw hex".into());
    }
    hex.as_bytes()
        .chunks_exact(4)
        .map(|b| {
            u16::from_str_radix(std::str::from_utf8(b).map_err(|_| "invalid ASCII")?, 16)
                .map_err(|_| "invalid unit".into())
        })
        .collect()
}
fn encoded(units: &[u16]) -> String {
    if units.is_empty() {
        "-".into()
    } else {
        units
            .iter()
            .flat_map(|unit| {
                const HEX: &[u8] = b"0123456789abcdef";
                [12, 8, 4, 0].map(|shift| HEX[usize::from((unit >> shift) & 15)] as char)
            })
            .collect()
    }
}
fn run() -> Result<(), String> {
    let stdin = io::stdin();
    let mut reader = stdin.lock();
    let mut index = 0;
    loop {
        let mut line = Vec::new();
        (&mut reader)
            .take(8193)
            .read_until(b'\n', &mut line)
            .map_err(|e| e.to_string())?;
        if line.is_empty() {
            break;
        }
        if line.len() > 8192 || index >= 1024 {
            return Err("input bound exceeded".into());
        }
        let raw = std::str::from_utf8(&line).map_err(|_| "invalid ASCII")?;
        let fields: Vec<_> = raw.split_whitespace().collect();
        if fields.len() != 7 || fields[0] != "input" || fields[2] != index.to_string() {
            return Err("invalid record identity".into());
        }
        let mode = match fields[1] {
            "add" => Mode::Add,
            "entry" => Mode::EntryOnly,
            "set" => Mode::SetStr,
            _ => return Err("invalid mode".into()),
        };
        let output = prepare(
            mode,
            &units(fields[3], fields[4])?,
            &units(fields[5], fields[6])?,
        )
        .map_err(|e| format!("preparation refused row {index}: {e:?}"))?;
        println!(
            "value {} {} {} {} {} {}",
            fields[1],
            index,
            output.text.len(),
            encoded(&output.text),
            output.style.len(),
            encoded(&output.style)
        );
        index += 1;
    }
    if index == 0 {
        return Err("empty input".into());
    }
    Ok(())
}
fn main() {
    if let Err(error) = run() {
        eprintln!("{error}");
        std::process::exit(1);
    }
}
