// SPDX-License-Identifier: GPL-2.0-or-later
//! Renderer effect ownership and parser decisions from cleanup 67da07cd.
//! Type is the slot identity; the native ASS allocation's uninitialized type
//! field is never observed. All remaining fields are initialized values.
use alloc::vec::Vec;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    Move,
    Origin,
    Fade,
    Banner,
    Scroll,
}
impl Kind {
    pub fn from_index(index: usize) -> Option<Self> {
        [
            Self::Move,
            Self::Origin,
            Self::Fade,
            Self::Banner,
            Self::Scroll,
        ]
        .get(index)
        .copied()
    }
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Effect {
    pub param: [i32; 8],
    pub time: [i32; 4],
}
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct State {
    slots: [Option<Effect>; 5],
}
impl State {
    pub fn get(&self, kind: Kind) -> Option<Effect> {
        self.slots[kind as usize]
    }
    pub fn clear(&mut self) {
        self.slots.fill(None);
    }
    fn replace(&mut self, kind: Kind, effect: Effect) {
        self.slots[kind as usize] = Some(effect);
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Command {
    Column,
    Fade,
    Move,
    Origin,
    Position,
}
#[derive(Clone, Copy, Debug)]
pub struct Context {
    pub scale_x: f64,
    pub scale_y: f64,
    pub wrap: i32,
    pub hard: i32,
    pub animated: bool,
}
pub trait Services {
    fn space(&mut self, unit: u16) -> bool;
    fn compare(&mut self, a: &[u16], b: &[u16]) -> i32;
    fn scan(&mut self, input: &[u16], count: usize) -> (i32, [i32; 4]);
    fn integer(&mut self, input: &[u16]) -> i32;
    fn float(&mut self, input: &[u16]) -> f64;
}
// MSVC's observed double->int indefinite result, including positive overflow
// and NaN. Rust's default saturating conversion differs for those two cases.
fn native_int(value: f64) -> i32 {
    if value.is_nan() || value >= 2147483648.0 {
        i32::MIN
    } else {
        value as i32
    }
}
fn delay(value: i32, scale: f64) -> i32 {
    let value = f64::from(value) / scale;
    native_int(if value > 1.0 { value } else { 1.0 })
}
fn literal(text: &[u8]) -> Vec<u16> {
    text.iter().map(|b| u16::from(*b)).collect()
}
impl State {
    pub fn apply(
        &mut self,
        command: Command,
        params: &[Vec<u16>],
        ctx: &mut Context,
        svc: &mut impl Services,
    ) {
        if command == Command::Column {
            if let Some(input) = params.first() {
                self.column(input, ctx, svc);
            }
            return;
        }
        let kind = match command {
            Command::Fade => Kind::Fade,
            Command::Origin => Kind::Origin,
            _ => Kind::Move,
        };
        if command == Command::Fade {
            ctx.animated = true;
        }
        if self.get(kind).is_some() {
            return;
        }
        let mut e = Effect::default();
        match (command, params.len()) {
            (Command::Fade, 7) => {
                for (out, input) in e.param[..3].iter_mut().zip(params) {
                    *out = svc.integer(input);
                }
                for (out, input) in e.time.iter_mut().zip(&params[3..]) {
                    *out = svc.integer(input);
                }
            }
            (Command::Fade, 2) => {
                e.param[0] = 255;
                e.param[2] = 255;
                e.time[0] = -1;
                e.time[3] = -1;
                e.time[1] = svc.integer(&params[0]);
                e.time[2] = svc.integer(&params[1]);
            }
            (Command::Move, 4 | 6) => {
                for (i, input) in params[..4].iter().enumerate() {
                    let scale = if i % 2 == 0 { ctx.scale_x } else { ctx.scale_y };
                    e.param[i] = native_int(scale * svc.float(input) * 8.0);
                }
                e.time[0] = -1;
                e.time[1] = -1;
                if params.len() == 6 {
                    e.time[0] = svc.integer(&params[4]);
                    e.time[1] = svc.integer(&params[5]);
                }
                ctx.animated = true;
            }
            (Command::Origin | Command::Position, 2) => {
                e.param[0] = native_int(ctx.scale_x * svc.float(&params[0]) * 8.0);
                e.param[1] = native_int(ctx.scale_y * svc.float(&params[1]) * 8.0);
                if command == Command::Position {
                    e.param[2] = e.param[0];
                    e.param[3] = e.param[1];
                }
            }
            _ => return,
        }
        self.replace(kind, e);
    }
    fn column(&mut self, input: &[u16], ctx: &mut Context, svc: &mut impl Services) {
        let mut start = 0;
        while start < input.len() && svc.space(input[start]) {
            start += 1;
        }
        if input.get(start).copied().unwrap_or(0) == 0 {
            return;
        }
        let mut end = input.len();
        while end > start && svc.space(input[end - 1]) {
            end -= 1;
        }
        let found = (start..end)
            .find(|i| input[*i] == b';' as u16)
            .unwrap_or(end);
        // Native FindChar returns end; it still dereferences that position.
        let split = found + usize::from(input.get(found) == Some(&(b';' as u16)));
        let name = &input[start..split];
        // The scan uses the original terminated CString suffix, not trimmed end.
        let rest = &input[split..];
        if svc.compare(name, &literal(b"Banner;")) == 0 {
            let (count, values) = svc.scan(rest, 3);
            if count < 1 {
                return;
            }
            let mut e = Effect::default();
            e.param[0] = delay(values[0], ctx.scale_x);
            e.param[1] = values[1];
            e.param[2] = native_int(ctx.scale_x * f64::from(values[2]));
            self.replace(Kind::Banner, e);
            ctx.wrap = 2;
            ctx.hard = ctx.hard.max(0);
        } else if svc.compare(name, &literal(b"Scroll up;")) == 0
            || svc.compare(name, &literal(b"Scroll down;")) == 0
        {
            let (count, mut values) = svc.scan(rest, 4);
            if count < 3 {
                return;
            }
            if values[0] > values[1] {
                values.swap(0, 1);
            }
            let mut e = Effect::default();
            e.param[0] = native_int(ctx.scale_y * f64::from(values[0]) * 8.0);
            e.param[1] = native_int(ctx.scale_y * f64::from(values[1]) * 8.0);
            e.param[2] = delay(values[2], ctx.scale_y);
            e.param[3] = i32::from(name.len() == 12);
            e.param[4] = native_int(ctx.scale_y * f64::from(values[3]));
            self.replace(Kind::Scroll, e);
            ctx.hard = 2;
        }
    }
}

#[derive(Debug, PartialEq, Eq)]
pub struct Invalid;
pub struct Request {
    pub command: Command,
    pub context: Context,
    pub params: Vec<Vec<u16>>,
}
impl Request {
    pub fn decode(bytes: &[u8]) -> Result<Self, Invalid> {
        fn take<'a>(b: &mut &'a [u8], count: usize) -> Result<&'a [u8], Invalid> {
            let part = b.get(..count).ok_or(Invalid)?;
            *b = &b[count..];
            Ok(part)
        }
        fn word(b: &mut &[u8]) -> Result<u32, Invalid> {
            Ok(u32::from_le_bytes(
                take(b, 4)?.try_into().map_err(|_| Invalid)?,
            ))
        }
        fn double(b: &mut &[u8]) -> Result<f64, Invalid> {
            Ok(f64::from_bits(u64::from_le_bytes(
                take(b, 8)?.try_into().map_err(|_| Invalid)?,
            )))
        }
        let mut b = bytes;
        let command = match word(&mut b)? {
            0 => Command::Column,
            1 => Command::Fade,
            2 => Command::Move,
            3 => Command::Origin,
            4 => Command::Position,
            _ => return Err(Invalid),
        };
        let scale_x = double(&mut b)?;
        let scale_y = double(&mut b)?;
        let wrap = word(&mut b)? as i32;
        let hard = word(&mut b)? as i32;
        let animated = match word(&mut b)? {
            0 => false,
            1 => true,
            _ => return Err(Invalid),
        };
        let count = word(&mut b)? as usize;
        if count > b.len() / 4 || (command == Command::Column && count != 1) {
            return Err(Invalid);
        }
        let mut params = Vec::new();
        for _ in 0..count {
            let len = word(&mut b)? as usize;
            if len > i32::MAX as usize {
                return Err(Invalid);
            }
            let raw = take(&mut b, len.checked_mul(2).ok_or(Invalid)?)?;
            params.push(
                raw.chunks_exact(2)
                    .map(|c| u16::from_le_bytes([c[0], c[1]]))
                    .collect(),
            );
        }
        if !b.is_empty() {
            return Err(Invalid);
        }
        Ok(Self {
            command,
            context: Context {
                scale_x,
                scale_y,
                wrap,
                hard,
                animated,
            },
            params,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    struct S {
        calls: usize,
    }
    impl Services for S {
        fn space(&mut self, u: u16) -> bool {
            matches!(u, 9..=13 | 32)
        }
        fn compare(&mut self, a: &[u16], b: &[u16]) -> i32 {
            i32::from(a != b)
        }
        fn scan(&mut self, _: &[u16], _: usize) -> (i32, [i32; 4]) {
            (4, [4, 2, 3, 7])
        }
        fn integer(&mut self, v: &[u16]) -> i32 {
            self.calls += 1;
            v.first().copied().unwrap_or(0) as i32
        }
        fn float(&mut self, v: &[u16]) -> f64 {
            self.integer(v) as f64
        }
    }
    fn context() -> Context {
        Context {
            scale_x: 0.5,
            scale_y: 2.0,
            wrap: 3,
            hard: -1,
            animated: false,
        }
    }
    #[test]
    fn first_wins_and_clear_return_detached_values() {
        let mut state = State::default();
        let mut c = context();
        let mut s = S { calls: 0 };
        let p = alloc::vec![alloc::vec![1], alloc::vec![2]];
        state.apply(Command::Position, &p, &mut c, &mut s);
        let value = state.get(Kind::Move).unwrap();
        assert_eq!(value.param[..4], [4, 32, 4, 32]);
        state.apply(
            Command::Move,
            &alloc::vec![alloc::vec![9];4],
            &mut c,
            &mut s,
        );
        assert_eq!(s.calls, 2);
        assert!(!c.animated);
        state.apply(Command::Fade, &[], &mut c, &mut s);
        assert!(c.animated);
        assert!(state.get(Kind::Fade).is_none());
        state.clear();
        assert!(state.get(Kind::Move).is_none());
        assert_eq!(value.param[0], 4);
    }
    #[test]
    fn columns_replace_and_scroll_keeps_banner() {
        let mut state = State::default();
        let mut c = context();
        let mut s = S { calls: 0 };
        state.apply(Command::Column, &[literal(b"Banner;4;2;3")], &mut c, &mut s);
        assert_eq!(state.get(Kind::Banner).unwrap().param[..3], [8, 2, 1]);
        assert_eq!((c.wrap, c.hard), (2, 0));
        c.scale_x = 2.0;
        state.apply(Command::Column, &[literal(b"Banner;4;2;3")], &mut c, &mut s);
        assert_eq!(state.get(Kind::Banner).unwrap().param[..3], [2, 2, 6]);
        state.apply(
            Command::Column,
            &[literal(b"Scroll down;4;2;3;7")],
            &mut c,
            &mut s,
        );
        assert_eq!(
            state.get(Kind::Scroll).unwrap().param[..5],
            [32, 64, 1, 1, 14]
        );
        assert!(state.get(Kind::Banner).is_some());
        assert_eq!(c.hard, 2);
    }
    #[test]
    fn cast_indefinite_and_ieee_delay() {
        for v in [
            f64::NAN,
            f64::INFINITY,
            f64::NEG_INFINITY,
            2147483648.0,
            -2147483649.0,
        ] {
            assert_eq!(native_int(v), i32::MIN);
        }
        assert_eq!(native_int(2147483647.9), i32::MAX);
        assert_eq!(native_int(-1.9), -1);
        assert_eq!(delay(0, 0.0), 1);
        assert_eq!(delay(1, 0.0), i32::MIN);
        assert_eq!(delay(1, f64::NAN), 1);
    }
    #[test]
    fn packet_rejects_counts_trailing_bytes_and_bool() {
        let mut bytes = alloc::vec![0;40];
        bytes[32] = 1;
        assert!(Request::decode(&bytes).is_ok());
        bytes.push(0);
        assert!(Request::decode(&bytes).is_err());
        bytes.pop();
        bytes[28] = 2;
        assert!(Request::decode(&bytes).is_err());
        bytes[28] = 0;
        bytes[36..].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(Request::decode(&bytes).is_err());
    }
}
