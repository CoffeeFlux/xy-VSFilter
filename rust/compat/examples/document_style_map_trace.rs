// SPDX-License-Identifier: GPL-2.0-or-later
//! Bounded replay of the successful native ATL contract fixture; no stdin.
use xy_vsfilter_compat::document_style_map::{string_hash, StyleMap};
fn units(key: &[u16]) -> String {
    use std::fmt::Write;
    let mut result = String::new();
    for unit in key {
        write!(&mut result, "{unit:04x}").unwrap();
    }
    result
}
fn state(seq: &mut usize, op: &str, map: &StyleMap<usize>) {
    print!(
        "{{\"event\":\"state\",\"seq\":{},\"op\":\"{}\",\"count\":{},\"bins\":{},\"order\":[",
        seq,
        op,
        map.count(),
        map.bin_count()
    );
    for (i, (key, id)) in map.iter().enumerate() {
        if i != 0 {
            print!(",");
        }
        print!(
            "{{\"id\":{},\"length\":{},\"units\":\"{}\"}}",
            id,
            key.len(),
            units(key)
        );
    }
    println!("]}}");
    *seq += 1;
}
fn key(n: usize) -> Vec<u16> {
    format!("Style{n:03}").encode_utf16().collect()
}
fn main() {
    let mut seq = 0;
    let mut map = StyleMap::new();
    state(&mut seq, "construct", &map);
    for i in 0..128 {
        map.insert(key(i), i);
        state(&mut seq, "insert", &map);
    }
    map.insert(key(0), 128);
    state(&mut seq, "existing-update", &map);
    for i in (0..128).step_by(2) {
        map.remove(&key(i));
        state(&mut seq, "remove", &map);
    }
    for i in (0..128).step_by(2) {
        map.insert(key(i), i);
        state(&mut seq, "reinsert", &map);
    }
    let mut copy = StyleMap::new();
    for (k, v) in map.iter() {
        copy.insert(k.to_vec(), *v);
        state(&mut seq, "ordered-copy", &copy);
    }
    map.clear();
    state(&mut seq, "clear", &map);
    map.insert(key(7), 7);
    state(&mut seq, "reuse", &map);
    let raw: [&[u16]; 10] = [
        &[],
        &[0, 88],
        &[65],
        &[65, 0, 66],
        &[65, 0, 67],
        &[97],
        &[0xd800],
        &[0xffff],
        &[65, 48],
        &[65, 49],
    ];
    let mut names = StyleMap::new();
    for (i, k) in raw.iter().enumerate() {
        let prefix = &k[..k.iter().position(|&u| u == 0).unwrap_or(k.len())];
        println!("{{\"event\":\"key\",\"index\":{},\"length\":{},\"units\":\"{}\",\"hash\":{},\"equal_A\":{}}}",i,k.len(),units(k),string_hash(k),prefix==[65]);
        names.insert(k.to_vec(), i);
        state(&mut seq, "raw-key-insert", &names);
    }
    println!("{{\"event\":\"complete\",\"marker\":\"document-atl-contract-v1 complete\"}}");
}
