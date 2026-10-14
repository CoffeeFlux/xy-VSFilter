// SPDX-License-Identifier: GPL-2.0-or-later
//! Prepared-record lifecycle trace. No native ABI, string preparation or host claims.
use std::io::{self, Read};
use xy_vsfilter_compat::document::{AddMode, Document, PreparedEntry};
use xy_vsfilter_compat::timeline_search::Mode;
fn prepared(id: i32, start: i64, end: i64, readorder: i32) -> PreparedEntry {
    PreparedEntry {
        text: if id == 0 {
            vec![]
        } else {
            vec![id as u16, 0, 0xd800, 0xdcff, 0xffff]
        },
        unicode: id % 2 != 0,
        style: vec![(id % 3 + 1) as u16],
        actor: vec![0xdc00, id as u16],
        effect: vec![0, 0xdfff],
        margins: [id - 7, id * 2 - 7, id * 3 - 7, id * 4 - 7],
        layer: id - 3,
        start,
        end,
        readorder,
    }
}
fn units(out: &mut Vec<i64>, units: &[u16]) {
    out.push(units.len() as i64);
    out.extend(units.iter().map(|&x| x as i64));
}
fn snapshot(d: &Document) {
    let mut out = vec![d.entries().len() as i64];
    for e in d.entries() {
        units(&mut out, &e.text);
        out.push(e.unicode as i64);
        units(&mut out, &e.style);
        units(&mut out, &e.actor);
        units(&mut out, &e.effect);
        out.extend(e.margins.iter().map(|&x| x as i64));
        out.extend([e.layer as i64, e.start, e.end, e.readorder as i64]);
    }
    out.push(d.segments().len() as i64);
    for s in d.segments() {
        out.extend([s.start, s.end, s.animated as i64, s.entries.len() as i64]);
        out.extend(s.entries.iter().map(|&x| x as i64));
    }
    println!(
        "{}",
        out.iter().map(i64::to_string).collect::<Vec<_>>().join(" ")
    );
}
fn main() {
    let mut input = String::new();
    io::stdin().read_to_string(&mut input).unwrap();
    let mut d = Document::default();
    let mut saved = d.clone();
    for line in input.lines() {
        let p: Vec<_> = line.split_whitespace().collect();
        let n = |i: usize| p[i].parse::<i64>().unwrap();
        match p[0] {
            "A" => {
                let mode = match n(1) {
                    0 => AddMode::Add,
                    1 => AddMode::EntryOnly,
                    2 => AddMode::WebVtt,
                    _ => panic!("mode"),
                };
                let mut entry = prepared(n(2) as i32, n(3), n(4), n(5) as i32);
                if p.len() == 7 {
                    entry.layer = n(6) as i32;
                } else {
                    assert_eq!(p.len(), 6);
                }
                d.add_prepared(entry, mode).unwrap();
            }
            "Q" => {
                let mode = match n(1) {
                    0 => Mode::Entry,
                    1 => Mode::NextSegment,
                    2 => Mode::ContainingSegment,
                    _ => panic!("query mode"),
                };
                let q = d.query_time(mode, n(2)).unwrap();
                print!("Q {} {} {} ", q.index, q.found as i32, q.count);
            }
            "E" => {
                let flag = match n(3) {
                    -1 => None,
                    0 => Some(false),
                    1 => Some(true),
                    _ => panic!("flag"),
                };
                d.set_prepared_text(n(1) as usize, prepared(n(2) as i32, 0, 0, 0).text, flag)
                    .unwrap();
            }
            "O" => {
                let order = (0..n(1) as usize)
                    .map(|i| n(i + 2) as usize)
                    .collect::<Vec<_>>();
                d.reorder_entries(&order).unwrap();
            }
            "T" => {
                let times = (0..n(1) as usize)
                    .map(|i| (n(i * 2 + 2), n(i * 2 + 3)))
                    .collect::<Vec<_>>();
                d.replace_entry_times(&times).unwrap();
            }
            "P" => {}
            "R" => d.rebuild_segments(),
            "D" => d.remove_all_entries(),
            "C" => d.clear_entries_and_segments(),
            "F" => {
                d.feedback_animation(n(1) as usize, n(2) != 0).unwrap();
            }
            "L" => {
                d.correct_last_end(n(1));
            }
            "Y" => {
                let length = n(2) as usize;
                assert_eq!(p.len(), 3 + length);
                let style = (0..length)
                    .map(|i| u16::try_from(n(3 + i)).unwrap())
                    .collect();
                d.set_prepared_style(n(1) as usize, style).unwrap();
            }
            "J" => {
                let old_length = n(1) as usize;
                let new_length_index = 2 + old_length;
                let new_length = n(new_length_index) as usize;
                assert_eq!(p.len(), new_length_index + 1 + new_length);
                let old: Vec<_> = (0..old_length)
                    .map(|i| u16::try_from(n(2 + i)).unwrap())
                    .collect();
                let new: Vec<_> = (0..new_length)
                    .map(|i| u16::try_from(n(new_length_index + 1 + i)).unwrap())
                    .collect();
                d.rename_style_cstring(&old, &new);
            }
            "N" => d.rename_style(&[n(1) as u16], &[n(2) as u16]),
            "K" => saved = d.clone(),
            "B" => d = saved.clone(),
            _ => panic!("operation"),
        }
        snapshot(&d);
    }
}
