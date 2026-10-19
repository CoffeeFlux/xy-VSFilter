// SPDX-License-Identifier: GPL-2.0-or-later
//! Cleanup Release CText path sequencing. Successful calls deliberately leave
//! the font selected; only the checked failure exits restore it.
use crate::text_measure::Backend as Font;
pub trait Backend {
    fn begin(&mut self, clear: bool) -> Result<bool, u32>;
    fn text(&mut self, units: &[u16]) -> bool;
    fn end(&mut self, partial: bool, x: i32) -> Result<bool, u32>;
}
pub fn create(
    units: &[u16],
    spacing: u64,
    font: &mut impl Font,
    path: &mut impl Backend,
) -> Result<bool, u32> {
    let selected = font.select();
    if font.nonzero(spacing) || font.legacy_windows() {
        let end = units.iter().position(|v| *v == 0).unwrap_or(units.len());
        let mut first = true;
        let mut width = 0i32;
        for unit in units[..end].chunks_exact(1) {
            let Some(extent) = font.extent(unit) else {
                font.restore(selected);
                return Ok(false);
            };
            let _ = path.begin(first)?;
            first = false;
            let _ = path.text(unit);
            let _ = path.end(true, width)?;
            width = width.wrapping_add(extent.wrapping_add(font.spacing(spacing)));
        }
    } else if font.extent(units).is_none()
        || !path.begin(true)?
        || !path.text(units)
        || !path.end(false, 0)?
    {
        font.restore(selected);
        return Ok(false);
    }
    Ok(true)
}
#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec::Vec;
    struct FontFake {
        spaced: bool,
        restored: bool,
        spans: Vec<Vec<u16>>,
    }
    impl Font for FontFake {
        type Selection = ();
        fn metrics(&mut self, _: u64, a: i32, d: i32) -> [i32; 2] {
            [a, d]
        }
        fn select(&mut self) {}
        fn nonzero(&mut self, _: u64) -> bool {
            self.spaced
        }
        fn legacy_windows(&mut self) -> bool {
            false
        }
        fn extent(&mut self, u: &[u16]) -> Option<i32> {
            self.spans.push(u.to_vec());
            Some(i32::MAX)
        }
        fn spacing(&mut self, _: u64) -> i32 {
            1
        }
        fn width(&mut self, _: u64, w: i32) -> i32 {
            w
        }
        fn restore(&mut self, _: ()) {
            self.restored = true;
        }
    }
    struct PathFake {
        calls: Vec<(u8, i32)>,
        ok: bool,
    }
    impl Backend for PathFake {
        fn begin(&mut self, c: bool) -> Result<bool, u32> {
            self.calls.push((0, i32::from(c)));
            Ok(self.ok)
        }
        fn text(&mut self, u: &[u16]) -> bool {
            self.calls.push((1, u.len() as i32));
            self.ok
        }
        fn end(&mut self, _: bool, x: i32) -> Result<bool, u32> {
            self.calls.push((2, x));
            Ok(self.ok)
        }
    }
    #[test]
    fn partial_failures_are_ignored_and_offsets_wrap() {
        let mut f = FontFake {
            spaced: true,
            restored: false,
            spans: Vec::new(),
        };
        let mut p = PathFake {
            calls: Vec::new(),
            ok: false,
        };
        assert_eq!(create(&[65, 0xd800, 0, 66], 0, &mut f, &mut p), Ok(true));
        assert!(!f.restored);
        assert_eq!(
            p.calls,
            [(0, 1), (1, 1), (2, 0), (0, 0), (1, 1), (2, i32::MIN)]
        );
        assert_eq!(f.spans.len(), 2);
    }
    #[test]
    fn full_path_preserves_nul_tail_and_short_circuits_failure() {
        let mut f = FontFake {
            spaced: false,
            restored: false,
            spans: Vec::new(),
        };
        let mut p = PathFake {
            calls: Vec::new(),
            ok: false,
        };
        assert_eq!(create(&[65, 0, 66], 0, &mut f, &mut p), Ok(false));
        assert!(f.restored);
        assert_eq!(p.calls, [(0, 1)]);
        assert_eq!(f.spans[0], [65, 0, 66]);
    }
}
