// SPDX-License-Identifier: GPL-2.0-or-later
//! The Win32 renderer uses /arch:IA32 with the CRT's 53-bit x87 precision.
//! Relative font size keeps an extended exponent until the call argument is
//! stored as double. Normalized f64 significands emulate that exponent range
//! safely without prematurely rounding subnormals or overflowing intermediates.
#[derive(Clone, Copy)]
struct Wide {
    mantissa: f64,
    exponent: i32,
}
impl Wide {
    fn from(value: f64) -> Self {
        if value == 0.0 {
            return Self {
                mantissa: value,
                exponent: 0,
            };
        }
        let bits = value.to_bits();
        let sign = bits & 0x8000_0000_0000_0000;
        let field = ((bits >> 52) & 2047) as i32;
        let fraction = bits & 0x000f_ffff_ffff_ffff;
        if field == 0 {
            let shift = fraction.leading_zeros() - 11;
            Self {
                mantissa: f64::from_bits(
                    sign | 0x3ff0_0000_0000_0000 | ((fraction << shift) & 0x000f_ffff_ffff_ffff),
                ),
                exponent: -1022 - shift as i32,
            }
        } else {
            Self {
                mantissa: f64::from_bits(sign | 0x3ff0_0000_0000_0000 | fraction),
                exponent: field - 1023,
            }
        }
    }
    fn scaled(value: f64, exponent: i32) -> Self {
        let mut out = Self::from(value);
        out.exponent += exponent;
        out
    }
    fn mul(self, other: Self) -> Self {
        Self::scaled(
            self.mantissa * other.mantissa,
            self.exponent + other.exponent,
        )
    }
    fn div(self, other: Self) -> Self {
        Self::scaled(
            self.mantissa / other.mantissa,
            self.exponent - other.exponent,
        )
    }
    fn add(self, other: Self) -> Self {
        if self.mantissa == 0.0 {
            return other;
        }
        if other.mantissa == 0.0 {
            return self;
        }
        let (large, small) = if self.exponent >= other.exponent {
            (self, other)
        } else {
            (other, self)
        };
        let difference = small.exponent - large.exponent;
        if difference < -54 {
            return large;
        }
        let scale = f64::from_bits(((1023 + difference) as u64) << 52);
        Self::scaled(large.mantissa + small.mantissa * scale, large.exponent)
    }
    fn value(self) -> f64 {
        if self.mantissa == 0.0 {
            return self.mantissa;
        }
        if self.exponent > 1023 {
            return if self.mantissa < 0.0 {
                f64::NEG_INFINITY
            } else {
                f64::INFINITY
            };
        }
        if self.exponent < -1075 {
            return if self.mantissa < 0.0 { -0.0 } else { 0.0 };
        }
        if self.exponent < -1022 {
            let scale = f64::from_bits(((self.exponent + 1022 + 1023) as u64) << 52);
            return (self.mantissa * f64::MIN_POSITIVE) * scale;
        }
        f64::from_bits(
            (self.mantissa.to_bits() & 0x800f_ffff_ffff_ffff)
                | (((self.exponent + 1023) as u64) << 52),
        )
    }
}
pub(crate) fn relative(size: f64, number: f64) -> f64 {
    if !size.is_finite() || !number.is_finite() || size == 0.0 || number == 0.0 {
        return size + size * number / 10.0;
    }
    let size = Wide::from(size);
    size.add(size.mul(Wide::from(number)).div(Wide::from(10.0)))
        .value()
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn x87_extended_exponent_keeps_subnormal_intermediates_and_cancellation() {
        assert_eq!(
            relative(f64::MIN_POSITIVE, -0.15).to_bits(),
            0x000f_c28f_5c28_f5c2
        );
        assert_eq!(relative(f64::MAX, -10.0).to_bits(), 0x7ca0_0000_0000_0000);
        assert_eq!(relative(23.5, 0.15), 23.8525);
        for bits in [
            1,
            2,
            0x000f_ffff_ffff_ffff,
            0x0010_0000_0000_0000,
            0x7fef_ffff_ffff_ffff,
            0x8000_0000_0000_0001,
        ] {
            assert_eq!(Wide::from(f64::from_bits(bits)).value().to_bits(), bits);
        }
    }
}
