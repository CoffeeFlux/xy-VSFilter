// SPDX-License-Identifier: GPL-2.0-or-later
//! CText::GetTextInfo sequencing over raw UTF16. Numeric conversion and Windows
//! calls are supplied by the target boundary; failure retains partial width.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Info {
    pub width: i32,
    pub ascent: i32,
    pub descent: i32,
}
#[derive(Clone, Copy)]
pub struct Style {
    pub scale_x: u64,
    pub scale_y: u64,
    pub spacing: u64,
}
pub trait Backend {
    type Selection;
    fn metrics(&mut self, scale: u64, ascent: i32, descent: i32) -> [i32; 2];
    fn select(&mut self) -> Self::Selection;
    fn nonzero(&mut self, bits: u64) -> bool;
    fn legacy_windows(&mut self) -> bool;
    fn extent(&mut self, units: &[u16]) -> Option<i32>;
    fn spacing(&mut self, bits: u64) -> i32;
    fn width(&mut self, scale: u64, width: i32) -> i32;
    fn restore(&mut self, selected: Self::Selection);
}
pub fn measure(
    units: &[u16],
    style: Style,
    initial_width: i32,
    metrics: [i32; 2],
    backend: &mut impl Backend,
) -> Info {
    let [ascent, descent] = backend.metrics(style.scale_y, metrics[0], metrics[1]);
    let mut info = Info {
        width: initial_width,
        ascent,
        descent,
    };
    let selected = backend.select();
    let end = units.iter().position(|v| *v == 0).unwrap_or(units.len());
    if backend.nonzero(style.spacing) || backend.legacy_windows() {
        for unit in units[..end].chunks_exact(1) {
            let Some(width) = backend.extent(unit) else {
                backend.restore(selected);
                return info;
            };
            let step = width.wrapping_add(backend.spacing(style.spacing));
            info.width = info.width.wrapping_add(step);
        }
    } else {
        let Some(width) = backend.extent(&units[..end]) else {
            backend.restore(selected);
            return info;
        };
        info.width = info.width.wrapping_add(width);
    }
    info.width = backend.width(style.scale_x, info.width);
    backend.restore(selected);
    info
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec::Vec;
    struct Fake {
        calls: Vec<&'static str>,
        spans: Vec<Vec<u16>>,
        failure: usize,
        spaced: bool,
    }
    impl Backend for Fake {
        type Selection = u32;
        fn metrics(&mut self, _: u64, a: i32, d: i32) -> [i32; 2] {
            self.calls.push("metrics");
            [a, d]
        }
        fn select(&mut self) -> u32 {
            self.calls.push("select");
            17
        }
        fn nonzero(&mut self, _: u64) -> bool {
            self.calls.push("nonzero");
            self.spaced
        }
        fn legacy_windows(&mut self) -> bool {
            self.calls.push("version");
            false
        }
        fn extent(&mut self, s: &[u16]) -> Option<i32> {
            self.calls.push("extent");
            self.spans.push(s.to_vec());
            if self.spans.len() == self.failure {
                None
            } else {
                Some(7)
            }
        }
        fn spacing(&mut self, _: u64) -> i32 {
            self.calls.push("spacing");
            3
        }
        fn width(&mut self, _: u64, w: i32) -> i32 {
            self.calls.push("width");
            w >> 3
        }
        fn restore(&mut self, s: u32) {
            assert_eq!(s, 17);
            self.calls.push("restore");
        }
    }
    fn run(spaced: bool, failure: usize, units: &[u16], width: i32) -> (Info, Fake) {
        let mut fake = Fake {
            calls: Vec::new(),
            spans: Vec::new(),
            failure,
            spaced,
        };
        let info = measure(
            units,
            Style {
                scale_x: 0,
                scale_y: 0,
                spacing: 0,
            },
            width,
            [2, 3],
            &mut fake,
        );
        (info, fake)
    }
    #[test]
    fn failure_preserves_partial_width_and_restores_without_scaling_or_next_conversion() {
        let (info, f) = run(true, 2, &[0xd800, 65, 66], 1);
        assert_eq!(
            info,
            Info {
                width: 11,
                ascent: 2,
                descent: 3
            }
        );
        assert_eq!(
            f.calls,
            ["metrics", "select", "nonzero", "extent", "spacing", "extent", "restore"]
        );
        assert_eq!(f.spans, alloc::vec![alloc::vec![0xd800], alloc::vec![65]]);
    }
    #[test]
    fn nul_and_empty_inputs_keep_branch_specific_extent_calls() {
        for spaced in [false, true] {
            let (_, f) = run(spaced, usize::MAX, &[65, 0, 66], 0);
            assert_eq!(f.spans, alloc::vec![alloc::vec![65]]);
            assert_eq!(f.calls.contains(&"version"), !spaced);
            let (_, f) = run(spaced, usize::MAX, &[], 0);
            assert_eq!(f.spans.len(), usize::from(!spaced));
            assert_eq!(f.calls.last(), Some(&"restore"));
        }
    }
    #[test]
    fn integer_width_additions_preserve_native_wrapping() {
        let (info, _) = run(true, usize::MAX, &[65], i32::MAX - 1);
        assert_eq!(info.width, (i32::MIN + 8) >> 3);
    }
}
