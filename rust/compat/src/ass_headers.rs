// SPDX-License-Identifier: GPL-2.0-or-later
//! ASS header dispatch after native field extraction/lowercase. CString equality
//! and Find stop at NUL; script versions are supplied by native suffix checks.
pub struct Headers {
    pub version: i32,
    pub style_version: i32,
    pub accepted: bool,
}
fn cstr(input: &[u16]) -> &[u16] {
    &input[..input
        .iter()
        .position(|&unit| unit == 0)
        .unwrap_or(input.len())]
}
fn equals(input: &[u16], text: &[u8]) -> bool {
    input.len() == text.len() && input.iter().zip(text).all(|(&a, &b)| a == u16::from(b))
}
impl Default for Headers {
    fn default() -> Self {
        Self {
            version: 3,
            style_version: 3,
            accepted: false,
        }
    }
}
impl Headers {
    pub fn classify(&mut self, input: &[u16]) -> u32 {
        let input = cstr(input);
        let keys: [&[u8]; 17] = [
            b"dialogue",
            b"style",
            b"[script info]",
            b"playresx",
            b"playresy",
            b"layoutresx",
            b"layoutresy",
            b"wrapstyle",
            b"scripttype",
            b"collisions",
            b"scaledborderandshadow",
            b"[v4 styles]",
            b"[v4+ styles]",
            b"[v4++ styles]",
            b"[events]",
            b"fontname",
            b"ycbcr matrix",
        ];
        let kind = keys
            .iter()
            .position(|key| equals(input, key))
            .map_or(0, |index| index as u32 + 1);
        if matches!(kind, 3 | 12..=15) {
            self.accepted = true;
        }
        if (12..=14).contains(&kind) {
            self.style_version = kind as i32 - 8;
        }
        kind
    }
    pub fn script_version(&mut self, version: i32) {
        if (4..=6).contains(&version) {
            self.version = version;
            self.style_version = version;
        }
    }
}
/// Modes: 0 collision substring, 1 scaled-border substring, 2 matrix prefix.
/// Matrix codes 1..7 are none, tv.601/709/2020, pc.601/709/2020; zero preserves
/// the existing native setting. Input was lowercased by the native caller.
pub fn value(input: &[u16], mode: u32) -> u32 {
    let input = cstr(input);
    match mode {
        0 | 1 => {
            let needle = if mode == 0 {
                &b"reverse"[..]
            } else {
                &b"yes"[..]
            };
            u32::from(input.windows(needle.len()).any(|part| equals(part, needle)))
        }
        2 => {
            let prefixes: [&[u8]; 7] = [
                b"none", b"tv.601", b"tv.709", b"tv.2020", b"pc.601", b"pc.709", b"pc.2020",
            ];
            prefixes
                .iter()
                .position(|prefix| {
                    input
                        .get(..prefix.len())
                        .is_some_and(|part| equals(part, prefix))
                })
                .map_or(0, |index| index as u32 + 1)
        }
        _ => 0,
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    fn units(text: &str) -> alloc::vec::Vec<u16> {
        text.encode_utf16().collect()
    }
    #[test]
    fn style_sections_do_not_change_dialogue_version_and_unknown_script_types_do_not_reset() {
        let mut state = Headers::default();
        state.script_version(5);
        assert_eq!(state.classify(&units("[v4 styles]\0ignored")), 12);
        assert!(state.accepted);
        assert_eq!((state.version, state.style_version), (5, 4));
        state.script_version(0);
        assert_eq!((state.version, state.style_version), (5, 4));
        state.script_version(6);
        assert_eq!((state.version, state.style_version), (6, 6));
        assert_eq!(state.classify(&units("[V4 Styles]")), 0);
    }
    #[test]
    fn substrings_and_prefixes_preserve_nul_and_trailing_junk_semantics() {
        assert_eq!(value(&units("unreversed"), 0), 1);
        assert_eq!(value(&units("notyes"), 1), 1);
        assert_eq!(value(&units("no\0yes"), 1), 0);
        assert_eq!(value(&units("pc.709junk"), 2), 6);
        assert_eq!(value(&units("none\0pc.709"), 2), 1);
        assert_eq!(value(&units(" pc.709"), 2), 0);
    }
}
