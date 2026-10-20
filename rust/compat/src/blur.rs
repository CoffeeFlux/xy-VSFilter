// SPDX-License-Identifier: GPL-2.0-or-later
//! Complete owned blur dispatch. No-effect and empty-source returns preserve
//! the caller's output; Gaussian precedes BeBlur without a native intermediate.
use crate::{gaussian_cache, gaussian_filter, raster::Image};
pub trait Numeric {
    type Be: crate::be_blur::Numeric;
    fn begin(&mut self) -> bool;
    fn effects(&mut self) -> [bool; 2];
    fn gaussian_args(&mut self) -> [u64; 3];
    fn be(&mut self) -> Self::Be;
}
pub enum Outcome {
    Inactive,
    Unchanged,
    Replace(Image),
}
pub fn run(
    image: Option<&Image>,
    cache: &mut gaussian_cache::Cache,
    n: &mut impl Numeric,
    coefficients: &mut impl gaussian_cache::Numeric,
    filter: &mut impl gaussian_filter::Numeric,
) -> Result<Outcome, u32> {
    if !n.begin() {
        return Ok(Outcome::Inactive);
    }
    let Some(image) = image.filter(|v| v.positive_size()) else {
        return Ok(Outcome::Unchanged);
    };
    let [gaussian, be] = n.effects();
    if gaussian {
        let [strength, sx, sy] = n.gaussian_args();
        let sigma = coefficients.products(strength, sx, sy);
        let intermediate = cache.gaussian(image, sigma, coefficients, filter)?;
        Ok(Outcome::Replace(if be {
            intermediate.be_blur(&mut n.be())?
        } else {
            intermediate
        }))
    } else if be {
        Ok(Outcome::Replace(image.be_blur(&mut n.be())?))
    } else {
        Ok(Outcome::Unchanged)
    }
}
