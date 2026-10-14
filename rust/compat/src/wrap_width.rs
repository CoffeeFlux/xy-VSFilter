// SPDX-License-Identifier: GPL-2.0-or-later
//! GetWrapWidth arithmetic. Native code retains word traversal and measurement.
//! State is [accumulated width, last word width, target width, maximum width].
pub fn begin(full: i32, maximum: i32) -> Option<[i32; 4]> {
    if maximum <= 0 {
        return None;
    }
    let lines = (full.checked_abs()? / maximum).checked_add(1)?;
    Some([0, 0, full / lines, maximum])
}

pub fn needs_word(state: &[i32; 4]) -> bool {
    state[0] < state[2]
}

pub fn push(state: &mut [i32; 4], word: i32) -> Option<()> {
    let next = state[0].checked_add(word)?;
    let fits = next.checked_abs()? < state[3].checked_abs()?;
    if fits {
        state[0] = next;
    }
    // Even a rejected word participates in style 3's final adjustment.
    state[1] = word;
    Some(())
}

pub fn finish(state: &[i32; 4], full: i32, style: i32) -> Option<i32> {
    let [width, last, _, maximum] = *state;
    if style == 3 && width < full && full.checked_sub(width)?.checked_add(last)? < maximum {
        width.checked_sub(last)
    } else {
        Some(width)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn strict_boundary_skips_a_word_but_retains_it_for_final_adjustment() {
        let mut state = begin(12, 10).unwrap();
        push(&mut state, 5).unwrap();
        assert!(needs_word(&state));
        push(&mut state, 5).unwrap();
        assert_eq!(state[0], 5);
        assert_eq!(state[1], 5);
        push(&mut state, 2).unwrap();
        assert!(!needs_word(&state));
        assert_eq!(finish(&state, 12, 0), Some(7));
        assert_eq!(finish(&state, 12, 3), Some(5));
    }
    #[test]
    fn negative_full_width_requires_no_words_and_zero_is_not_special() {
        let state = begin(-19, 10).unwrap();
        assert!(!needs_word(&state));
        assert_eq!(finish(&state, -19, 3), Some(0));
        assert_eq!(begin(0, 10), Some([0, 0, 0, 10]));
    }
    #[test]
    fn arithmetic_declines_without_changing_state() {
        assert_eq!(begin(i32::MIN, 1), None);
        assert_eq!(begin(i32::MAX, 1), None);
        assert_eq!(begin(1, 0), None);
        let mut state = begin(10, 20).unwrap();
        let before = state;
        assert_eq!(push(&mut state, i32::MIN), None);
        assert_eq!(state, before);
        state = [1, i32::MAX, 5, 10];
        assert_eq!(finish(&state, 10, 3), None);
        assert_eq!(finish(&state, 10, 0), Some(1));
    }
}
