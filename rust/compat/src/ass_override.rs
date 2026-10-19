// SPDX-License-Identifier: GPL-2.0-or-later
//! ASS override evaluation from cleanup 67da07cd RTS.cpp. Native services run
//! synchronously on detached values; no registry or foreign resource is borrowed.
use crate::{ass_tree, document_style::StyleValue, document_style_map::StyleMap, effects};
use alloc::vec::Vec;

#[derive(Clone, Debug, Default)]
pub struct State {
    pub alignment: i32,
    pub wrap: i32,
    pub hard: i32,
    pub animated: bool,
    pub animated2: bool,
    pub inverse: bool,
    pub clipper: bool,
    pub clip: [i32; 4],
    pub time: i32,
    pub delay: i32,
    pub start: i32,
    pub end: i32,
    pub kind: i32,
    pub kstart: i32,
    pub kend: i32,
    pub polygon: i32,
    pub baseline: i32,
    pub width: i32,
    pub height: i32,
    pub default_wrap: i32,
    pub scale_x: f64,
    pub scale_y: f64,
    pub accel: f64,
    pub target_x: f64,
    pub target_y: f64,
}
pub struct Clip<'a> {
    pub text: &'a [u16],
    pub scale_x: f64,
    pub scale_y: f64,
    pub inverse: bool,
}
pub trait Services: effects::Services {
    fn integer_base(&mut self, input: &[u16], base: i32) -> i32;
    fn pow(&mut self, base: f64, exponent: f64) -> f64;
    fn checkpoint(
        &mut self,
        state: &State,
        style: &StyleValue,
        effects: &effects::State,
        clip: Option<Clip<'_>>,
    ) -> Result<(), u32>;
}
pub fn native_int(value: f64) -> i32 {
    if value.is_nan() || value >= 2147483648.0 {
        i32::MIN
    } else {
        value as i32
    }
}
pub fn animation(
    state: &State,
    mut dst: f64,
    src: f64,
    animate: bool,
    svc: &mut impl Services,
) -> f64 {
    let start = state.start;
    let end = if state.end == 0 {
        state.delay
    } else {
        state.end
    };
    // Clear the sign bit rather than requiring a platform math implementation.
    let difference = f64::from_bits((dst - src).to_bits() & 0x7fff_ffff_ffff_ffff);
    if difference >= 0.0001 && animate {
        if state.time < start {
            dst = src;
        } else if start <= state.time && state.time < end {
            let t = svc.pow(
                1.0 * f64::from(state.time.wrapping_sub(start))
                    / f64::from(end.wrapping_sub(start)),
                state.accel,
            );
            dst = (1.0 - t) * src + t * dst;
        }
    }
    dst
}
fn nonnegative(value: f64) -> f64 {
    if value < 0.0 {
        0.0
    } else {
        value
    }
}

impl State {
    pub const BYTES: usize = 132;
    pub fn encode(&self) -> Vec<u8> {
        let mut out = Vec::with_capacity(Self::BYTES);
        for value in [
            self.alignment,
            self.wrap,
            self.hard,
            i32::from(self.animated),
            i32::from(self.animated2),
            i32::from(self.inverse),
            i32::from(self.clipper),
            self.clip[0],
            self.clip[1],
            self.clip[2],
            self.clip[3],
            self.time,
            self.delay,
            self.start,
            self.end,
            self.kind,
            self.kstart,
            self.kend,
            self.polygon,
            self.baseline,
            self.width,
            self.height,
            self.default_wrap,
        ] {
            out.extend_from_slice(&value.to_le_bytes());
        }
        for value in [
            self.scale_x,
            self.scale_y,
            self.accel,
            self.target_x,
            self.target_y,
        ] {
            out.extend_from_slice(&value.to_bits().to_le_bytes());
        }
        out
    }
    pub fn decode(bytes: &[u8]) -> Option<Self> {
        if bytes.len() != Self::BYTES {
            return None;
        }
        let mut i = [0i32; 23];
        let mut f = [0f64; 5];
        for (out, chunk) in i.iter_mut().zip(bytes[..92].chunks_exact(4)) {
            *out = i32::from_le_bytes(chunk.try_into().ok()?);
        }
        if i[3..7].iter().any(|v| !matches!(v, 0 | 1)) {
            return None;
        }
        for (out, chunk) in f.iter_mut().zip(bytes[92..].chunks_exact(8)) {
            *out = f64::from_bits(u64::from_le_bytes(chunk.try_into().ok()?));
        }
        Some(Self {
            alignment: i[0],
            wrap: i[1],
            hard: i[2],
            animated: i[3] != 0,
            animated2: i[4] != 0,
            inverse: i[5] != 0,
            clipper: i[6] != 0,
            clip: [i[7], i[8], i[9], i[10]],
            time: i[11],
            delay: i[12],
            start: i[13],
            end: i[14],
            kind: i[15],
            kstart: i[16],
            kend: i[17],
            polygon: i[18],
            baseline: i[19],
            width: i[20],
            height: i[21],
            default_wrap: i[22],
            scale_x: f[0],
            scale_y: f[1],
            accel: f[2],
            target_x: f[3],
            target_y: f[4],
        })
    }
}

pub struct Evaluation {
    pub state: State,
    pub style: StyleValue,
    pub original: StyleValue,
    pub effects: effects::State,
}
impl Evaluation {
    pub fn apply(
        &mut self,
        tree: &ass_tree::Tree,
        list: u32,
        animate: bool,
        names: &StyleMap<Option<StyleValue>>,
        svc: &mut impl Services,
    ) -> Result<(), u32> {
        // A None task marks return from a transform. This preserves the native
        // post-child animation flags without a recursive call stack.
        let mut tasks: Vec<(Option<usize>, bool)> = tree
            .list(list)
            .map_err(|_| 4u32)?
            .iter()
            .rev()
            .map(|i| (Some(*i), animate))
            .collect();
        while let Some((node, animate)) = tasks.pop() {
            let s = &mut self.state;
            let Some(node) = node else {
                s.animated = true;
                s.animated2 = true;
                continue;
            };
            let node = &tree.nodes[node];
            let command = node.command;
            let params = &node.params;
            let p = params.first().map(Vec::as_slice).unwrap_or(&[]);
            let current = &mut self.style;
            let original = &self.original;
            s.hard = s.hard.max(ass_tree::position_level(command));
            macro_rules! anim {
                ($dst:expr,$src:expr) => {
                    animation(s, $dst, $src, animate, svc)
                };
            }
            // Empty parameters still run conversions/animation in several
            // commands; keeping that order preserves CRT errno and pow effects.
            macro_rules! scalar {
                ($field:ident,$clamp:expr,$always:expr) => {{
                    if $always || !p.is_empty() {
                        let dst = svc.float(p);
                        let n = anim!(dst, current.$field);
                        current.$field = if p.is_empty() {
                            original.$field
                        } else if $clamp {
                            nonnegative(n)
                        } else {
                            n
                        };
                    } else {
                        current.$field = original.$field;
                    }
                }};
            }
            match command {
                0..=3 | 17 => {
                    let index = if command == 17 { 0 } else { command as usize };
                    let c = svc.integer_base(p, 16) as u32;
                    if p.is_empty() {
                        current.colors[index] = original.colors[index];
                    } else {
                        let mut color = 0;
                        for mask in [0x0000ff, 0x00ff00, 0xff0000] {
                            color |= (native_int(anim!(
                                f64::from(c & mask),
                                f64::from(current.colors[index] & mask)
                            )) as u32)
                                & mask;
                        }
                        current.colors[index] = color;
                    }
                }
                4..=8 => {
                    let range = if command == 8 {
                        0..4
                    } else {
                        (command - 4) as usize..(command - 3) as usize
                    };
                    for index in range {
                        current.alpha[index] = if p.is_empty() {
                            original.alpha[index]
                        } else {
                            let dst = f64::from(svc.integer_base(p, 16));
                            native_int(anim!(dst, f64::from(current.alpha[index]))) as u8
                        };
                    }
                }
                9 | 10 => {
                    let n = svc.integer(p);
                    if s.alignment < 0 {
                        s.alignment = if command == 9 && (1..10).contains(&n) {
                            n
                        } else if command == 10 && (1..12).contains(&n) {
                            ((n - 1) & 3)
                                + 1
                                + if n & 4 != 0 { 6 } else { 0 }
                                + if n & 8 != 0 { 3 } else { 0 }
                        } else {
                            original.screen_alignment
                        };
                    }
                }
                11 => scalar!(gaussian_blur, true, true),
                12 | 46 => {
                    let dst = svc.float(p);
                    if command == 12 {
                        let nx = anim!(dst, current.outline_width_x);
                        current.outline_width_x = if p.is_empty() {
                            original.outline_width_x
                        } else {
                            nonnegative(nx)
                        };
                        let ny = anim!(dst, current.outline_width_y);
                        current.outline_width_y = if p.is_empty() {
                            original.outline_width_y
                        } else {
                            nonnegative(ny)
                        };
                    } else {
                        let nx = anim!(dst, current.shadow_depth_x);
                        current.shadow_depth_x = if p.is_empty() {
                            original.shadow_depth_x
                        } else {
                            nonnegative(nx)
                        };
                        let ny = anim!(dst, current.shadow_depth_y);
                        current.shadow_depth_y = if p.is_empty() {
                            original.shadow_depth_y
                        } else {
                            nonnegative(ny)
                        };
                    }
                }
                13 => scalar!(blur, false, true),
                14 => {
                    let n = svc.integer(p);
                    current.font_weight = if p.is_empty() {
                        original.font_weight
                    } else {
                        match n {
                            0 => 400,
                            1 => 700,
                            100.. => n,
                            _ => original.font_weight,
                        }
                    };
                }
                15 | 16 => {
                    let inverse = command == 16;
                    if matches!(params.len(), 1 | 2) && !s.clipper {
                        let divisor = if params.len() == 2 {
                            1i32.wrapping_shl((svc.integer(p).max(1) - 1) as u32)
                        } else {
                            1
                        };
                        svc.checkpoint(
                            s,
                            current,
                            &self.effects,
                            Some(Clip {
                                text: &params[params.len() - 1],
                                scale_x: s.scale_x / f64::from(divisor),
                                scale_y: s.scale_y / f64::from(divisor),
                                inverse,
                            }),
                        )?;
                        s.clipper = true;
                    } else if params.len() == 4 {
                        s.inverse = inverse;
                        let mut rect = [0; 4];
                        for i in (0..4).rev() {
                            rect[i] = native_int(svc.float(&params[i]) + 0.5);
                        }
                        for i in (0..4).rev() {
                            s.clip[i] = native_int(anim!(
                                (if i % 2 == 0 { s.scale_x } else { s.scale_y })
                                    * f64::from(rect[i]),
                                f64::from(s.clip[i])
                            ));
                        }
                    }
                }
                18 | 19 | 39 | 40 | 42 => {
                    let command = match command {
                        18 | 19 => effects::Command::Fade,
                        39 => effects::Command::Move,
                        40 => effects::Command::Origin,
                        _ => effects::Command::Position,
                    };
                    let mut context = effects::Context {
                        scale_x: s.scale_x,
                        scale_y: s.scale_y,
                        wrap: s.wrap,
                        hard: s.hard,
                        animated: s.animated2,
                    };
                    self.effects.apply(command, params, &mut context, svc);
                    s.wrap = context.wrap;
                    s.hard = context.hard;
                    s.animated2 = context.animated;
                }
                20 => scalar!(font_shift_x, false, false),
                21 => scalar!(font_shift_y, false, false),
                22 => {
                    let n = svc.integer(p);
                    current.char_set = if p.is_empty() {
                        original.char_set
                    } else if n < 0 {
                        1
                    } else {
                        n
                    };
                }
                23 => {
                    // CString comparison stops at NUL; Trim stops scanning at
                    // NUL but retains the remaining stored code units.
                    let nul = p.iter().position(|u| *u == 0).unwrap_or(p.len());
                    if p.is_empty() || &p[..nul] == [b'0' as u16] {
                        current.font_name = original.font_name.clone();
                    } else {
                        current.font_name = p.to_vec();
                        crate::html_tag::trim(&mut current.font_name, |u| svc.space(u));
                    }
                }
                24 => scalar!(font_angle_x, false, false),
                25 => scalar!(font_angle_y, false, false),
                26 | 27 => scalar!(font_angle_z, false, false),
                28 => scalar!(font_scale_x, true, true),
                29 => scalar!(font_scale_y, true, true),
                30 => {
                    current.font_scale_x = original.font_scale_x;
                    current.font_scale_y = original.font_scale_y;
                }
                31 => scalar!(font_spacing, false, false),
                32 => {
                    if p.is_empty() {
                        current.font_size = original.font_size;
                    } else {
                        let number = svc.float(p);
                        let dst = if matches!(p[0], 45 | 43) {
                            current.font_size + current.font_size * number / 10.0
                        } else {
                            number
                        };
                        let n = anim!(dst, current.font_size);
                        current.font_size = if n > 0.0 { n } else { original.font_size };
                    }
                }
                33 | 47 | 49 => {
                    let n = svc.integer(p);
                    let (field, original) = match command {
                        33 => (&mut current.italic, original.italic),
                        47 => (&mut current.strikeout, original.strikeout),
                        _ => (&mut current.underline, original.underline),
                    };
                    *field = if p.is_empty() {
                        original
                    } else {
                        match n {
                            0 => false,
                            1 => true,
                            _ => original,
                        }
                    };
                }
                34 => {
                    s.kstart = if p.is_empty() {
                        0
                    } else {
                        native_int(svc.float(p) * 10.0)
                    };
                    s.kend = s.kstart;
                    s.animated2 = true;
                }
                35..=38 => {
                    s.kind = match command {
                        35 | 36 => 1,
                        37 => 2,
                        _ => 0,
                    };
                    s.kstart = s.kend;
                    s.kend = native_int(
                        f64::from(s.kend)
                            + if p.is_empty() {
                                1000.0
                            } else {
                                svc.float(p) * 10.0
                            },
                    );
                    s.animated2 = true;
                }
                41 => s.baseline = svc.integer(p),
                43 => s.polygon = svc.integer(p).max(0),
                44 => {
                    let n = svc.integer(p);
                    s.wrap = if !p.is_empty() && (0..=3).contains(&n) {
                        n
                    } else {
                        s.default_wrap
                    };
                }
                45 => {
                    *current = if p.is_empty() {
                        None
                    } else {
                        names.get(p).and_then(Option::as_ref)
                    }
                    .unwrap_or(original)
                    .clone()
                }
                48 => {
                    s.start = 0;
                    s.end = 0;
                    s.accel = 1.0;
                    match params.len() {
                        2 => s.accel = svc.float(&params[0]),
                        3 => {
                            s.start = native_int(svc.float(&params[0]));
                            s.end = native_int(svc.float(&params[1]));
                        }
                        4 => {
                            s.start = svc.integer(&params[0]);
                            s.end = svc.integer(&params[1]);
                            s.accel = svc.float(&params[2]);
                        }
                        _ => {}
                    }
                    tasks.push((None, true));
                    tasks.extend(node.children.iter().rev().map(|i| (Some(*i), true)));
                }
                50 => scalar!(outline_width_x, true, true),
                51 => scalar!(shadow_depth_x, false, true),
                52 => scalar!(outline_width_y, true, true),
                53 => scalar!(shadow_depth_y, false, true),
                _ => {}
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use alloc::vec;
    #[derive(Default)]
    struct Native {
        clips: usize,
        fail: bool,
    }
    fn text(input: &[u16]) -> std::string::String {
        std::string::String::from_utf16_lossy(input)
    }
    impl effects::Services for Native {
        fn space(&mut self, u: u16) -> bool {
            matches!(u, 9..=13 | 32)
        }
        fn compare(&mut self, _: &[u16], _: &[u16]) -> i32 {
            panic!("unexpected column service")
        }
        fn scan(&mut self, _: &[u16], _: usize) -> (i32, [i32; 4]) {
            panic!("unexpected column service")
        }
        fn integer(&mut self, p: &[u16]) -> i32 {
            text(p).parse().unwrap_or(0)
        }
        fn float(&mut self, p: &[u16]) -> f64 {
            text(p).parse().unwrap_or(0.0)
        }
    }
    impl Services for Native {
        fn integer_base(&mut self, p: &[u16], base: i32) -> i32 {
            i32::from_str_radix(&text(p), base as u32).unwrap_or(0)
        }
        fn pow(&mut self, b: f64, e: f64) -> f64 {
            b.powf(e)
        }
        fn checkpoint(
            &mut self,
            s: &State,
            style: &StyleValue,
            _: &effects::State,
            c: Option<Clip<'_>>,
        ) -> Result<(), u32> {
            assert!(!s.clipper);
            assert_eq!(style.font_weight, 700);
            assert!(c.is_some());
            self.clips += 1;
            if self.fail {
                Err(6)
            } else {
                Ok(())
            }
        }
    }
    fn eval() -> Evaluation {
        let mut style = crate::document_style::decode(&[0; 192]).unwrap();
        style.font_size = 20.0;
        style.outline_width_x = 2.0;
        Evaluation {
            state: State {
                delay: 1000,
                time: 500,
                scale_x: 1.0,
                scale_y: 1.0,
                ..State::default()
            },
            original: style.clone(),
            style,
            effects: effects::State::default(),
        }
    }
    fn tree(s: &str) -> ass_tree::Tree {
        let p: Vec<u16> = s.encode_utf16().collect();
        ass_tree::parse(&p, &vec![0; p.len()]).unwrap()
    }
    #[test]
    fn transforms_keep_inner_timing_and_post_child_flags() {
        let mut e = eval();
        let mut svc = Native::default();
        e.apply(
            &tree(r"\t(0,1000,\fs40\t(100,900,\bord6))\fs+1"),
            0,
            false,
            &StyleMap::default(),
            &mut svc,
        )
        .unwrap();
        assert_eq!(e.style.font_size, 33.0);
        assert_eq!(e.style.outline_width_x, 4.0);
        assert_eq!((e.state.start, e.state.end), (100, 900));
        assert!(e.state.animated && e.state.animated2);
        assert_eq!(e.state.hard, 1);
    }
    #[test]
    fn clip_checkpoint_preserves_order_first_wins_and_failure() {
        let input = tree(r"\b1\clip(m 0 0 l 2 2)\b0\iclip(m 1 1 l 3 3)");
        let mut e = eval();
        let mut svc = Native::default();
        e.apply(&input, 0, false, &StyleMap::default(), &mut svc)
            .unwrap();
        assert_eq!(svc.clips, 1);
        assert_eq!(e.style.font_weight, 400);
        assert!(e.state.clipper);
        let mut e = eval();
        svc.fail = true;
        assert_eq!(
            e.apply(&input, 0, false, &StyleMap::default(), &mut svc),
            Err(6)
        );
        assert_eq!(e.style.font_weight, 700);
        assert!(!e.state.clipper);
    }
    #[test]
    fn fractional_karaoke_sums_before_conversion_and_named_reset_copies_bits() {
        let mut e = eval();
        e.state.kend = -9;
        let mut names = StyleMap::default();
        let mut named = e.original.clone();
        named.font_size = f64::from_bits(0x7ff8123456789abc);
        names.insert(vec![78], Some(named.clone()));
        e.apply(
            &tree(r"\k0.15\rN"),
            0,
            false,
            &names,
            &mut Native::default(),
        )
        .unwrap();
        assert_eq!((e.state.kstart, e.state.kend), (-9, -7));
        assert_eq!(e.style, named);
        e.apply(&tree(r"\rn"), 0, false, &names, &mut Native::default())
            .unwrap();
        assert_eq!(e.style, e.original);
    }
    #[test]
    fn wire_rejects_bad_flags_and_retains_nan_signed_zero() {
        let s = State {
            scale_x: -0.0,
            accel: f64::from_bits(0x7ff8123456789abc),
            ..State::default()
        };
        let mut bytes = s.encode();
        assert_eq!(State::decode(&bytes).unwrap().encode(), bytes);
        assert!(State::decode(&bytes[..131]).is_none());
        bytes[12] = 2;
        assert!(State::decode(&bytes).is_none());
    }
    #[test]
    fn animation_threshold_before_start_and_nan_are_observable() {
        let mut svc = Native::default();
        let state = State {
            time: -1,
            start: 0,
            delay: 1000,
            accel: 1.0,
            ..State::default()
        };
        assert_eq!(animation(&state, 5.0, 2.0, true, &mut svc), 2.0);
        assert_eq!(animation(&state, 0.00001, 0.0, true, &mut svc), 0.00001);
        let nan = f64::from_bits(0x7ff8123456789abc);
        assert_eq!(
            animation(&state, nan, 0.0, true, &mut svc).to_bits(),
            nan.to_bits()
        );
    }
}
