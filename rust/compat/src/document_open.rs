// SPDX-License-Identifier: GPL-2.0-or-later
//! Open(CTextFile*) lifecycle control from pinned STS.cpp:3243–3304.
//!
//! Actions are ordered native adapter obligations, not callbacks executed by
//! this core. Acknowledgements hold no file, parser, dialog or document borrow
//! across calls. Native retains successful metadata assignments (and their
//! order), m_simple, parsing, virtual Empty, seeking, dialogs and notifications.
//! Only the final live both-zero screen fallback is performed here. Protocol
//! tests do not establish native parser, allocation or reentry compatibility.
use crate::document::Document;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum ActionKind {
    InitialEmpty = 0,
    CapturePosition = 1,
    Parse = 2,
    SyntaxWarning = 3,
    Seek = 4,
    RetryEmpty = 5,
    AbortEmpty = 6,
    SuccessHeader = 7,
    Sidecar = 8,
    Rebuild = 9,
    UnknownStyles = 10,
    DoneFalse = 11,
    DoneTrue = 12,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Action {
    pub kind: ActionKind,
    /// Nonzero indices are meaningful only for Parse and SuccessHeader.
    pub index: u32,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    MissingParserResult,
    UnexpectedParserResult,
    Finished,
    UnsuppliedScreen,
}
#[derive(Clone, Debug)]
pub struct Run {
    parser_count: u32,
    parser_index: u32,
    kind: ActionKind,
}
impl Run {
    pub fn new(parser_count: u32) -> Self {
        Self {
            parser_count,
            parser_index: 0,
            kind: ActionKind::InitialEmpty,
        }
    }
    pub fn action(&self) -> Action {
        Action {
            kind: self.kind,
            index: if matches!(self.kind, ActionKind::Parse | ActionKind::SuccessHeader) {
                self.parser_index
            } else {
                0
            },
        }
    }
    /// Acknowledge completion of the current adapter action. Only Parse takes
    /// a result. Errors leave both the phase and document unchanged. This does
    /// not validate that an adapter actually fulfilled its obligation.
    pub fn advance(
        &mut self,
        document: &mut Document,
        parser_result: Option<bool>,
    ) -> Result<Action, Error> {
        use ActionKind::*;
        if matches!(self.kind, DoneFalse | DoneTrue) {
            return Err(Error::Finished);
        }
        if self.kind != Parse && parser_result.is_some() {
            return Err(Error::UnexpectedParserResult);
        }
        let next = match self.kind {
            InitialEmpty => CapturePosition,
            CapturePosition => {
                if self.parser_count == 0 {
                    DoneFalse
                } else {
                    Parse
                }
            }
            Parse => match parser_result.ok_or(Error::MissingParserResult)? {
                true => SuccessHeader,
                false if !document.entries().is_empty() => SyntaxWarning,
                false => Seek,
            },
            SyntaxWarning => AbortEmpty,
            AbortEmpty => DoneFalse,
            Seek => RetryEmpty,
            RetryEmpty => {
                // A valid parser index is below parser_count, so this addition
                // remains representable even when parser_count is u32::MAX.
                self.parser_index += 1;
                if self.parser_index < self.parser_count {
                    Parse
                } else {
                    DoneFalse
                }
            }
            SuccessHeader => Sidecar,
            Sidecar => Rebuild,
            Rebuild => UnknownStyles,
            UnknownStyles => {
                let size = document.script_size().ok_or(Error::UnsuppliedScreen)?;
                if size == [0, 0] {
                    document.set_script_size(384, 288);
                }
                DoneTrue
            }
            DoneFalse | DoneTrue => unreachable!(),
        };
        self.kind = next;
        Ok(self.action())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::document::{AddMode, PreparedEntry, ScriptField};
    fn ack(run: &mut Run, doc: &mut Document, kind: ActionKind) {
        assert_eq!(run.advance(doc, None).unwrap().kind, kind);
    }
    fn parsed(run: &mut Run, doc: &mut Document) {
        ack(run, doc, ActionKind::CapturePosition);
        ack(run, doc, ActionKind::Parse);
    }
    fn success(run: &mut Run, doc: &mut Document) {
        parsed(run, doc);
        assert_eq!(
            run.advance(doc, Some(true)).unwrap().kind,
            ActionKind::SuccessHeader
        );
        ack(run, doc, ActionKind::Sidecar);
        ack(run, doc, ActionKind::Rebuild);
        ack(run, doc, ActionKind::UnknownStyles);
    }
    #[test]
    fn retries_seek_then_empty_even_after_last_parser() {
        let mut d = Document::default();
        let mut r = Run::new(2);
        parsed(&mut r, &mut d);
        for index in 0..2 {
            assert_eq!(
                r.action(),
                Action {
                    kind: ActionKind::Parse,
                    index
                }
            );
            assert_eq!(
                r.advance(&mut d, Some(false)).unwrap().kind,
                ActionKind::Seek
            );
            ack(&mut r, &mut d, ActionKind::RetryEmpty);
            ack(
                &mut r,
                &mut d,
                if index == 0 {
                    ActionKind::Parse
                } else {
                    ActionKind::DoneFalse
                },
            );
        }
    }
    #[test]
    fn failed_parser_observes_live_entries_and_aborts_after_dialog_and_empty() {
        let mut d = Document::default();
        let mut r = Run::new(3);
        parsed(&mut r, &mut d);
        d.add_prepared(
            PreparedEntry {
                text: alloc::vec![65],
                style: alloc::vec![],
                actor: alloc::vec![],
                effect: alloc::vec![],
                unicode: true,
                margins: [0; 4],
                layer: 0,
                start: 0,
                end: 1,
                readorder: 0,
            },
            AddMode::Add,
        )
        .unwrap();
        assert_eq!(
            r.advance(&mut d, Some(false)).unwrap().kind,
            ActionKind::SyntaxWarning
        );
        ack(&mut r, &mut d, ActionKind::AbortEmpty);
        // Adapter Empty is deliberately separate from the acknowledgement.
        assert_eq!(d.entries().len(), 1);
        d.remove_all_entries();
        ack(&mut r, &mut d, ActionKind::DoneFalse);
    }
    #[test]
    fn success_order_and_live_callback_screen_edits() {
        let mut d = Document::default();
        d.set_script_size(0, 0);
        let mut r = Run::new(1);
        success(&mut r, &mut d);
        // A callback after rebuild or warning can replace the screen pair.
        d.set_script_size(0, 720);
        ack(&mut r, &mut d, ActionKind::DoneTrue);
        assert_eq!(d.script_size(), Some([0, 720]));
        for size in [[0, 0], [640, 0], [-1, 0], [640, 480]] {
            let mut r = Run::new(1);
            d.set_script_size(size[0], size[1]);
            success(&mut r, &mut d);
            ack(&mut r, &mut d, ActionKind::DoneTrue);
            assert_eq!(
                d.script_size(),
                Some(if size == [0, 0] { [384, 288] } else { size })
            );
        }
    }
    #[test]
    fn protocol_errors_and_unsupplied_screen_do_not_advance() {
        let mut d = Document::default();
        let mut r = Run::new(1);
        assert_eq!(
            r.advance(&mut d, Some(true)),
            Err(Error::UnexpectedParserResult)
        );
        assert_eq!(r.action().kind, ActionKind::InitialEmpty);
        parsed(&mut r, &mut d);
        assert_eq!(r.advance(&mut d, None), Err(Error::MissingParserResult));
        assert_eq!(r.action().kind, ActionKind::Parse);
        assert_eq!(
            r.advance(&mut d, Some(true)).unwrap().kind,
            ActionKind::SuccessHeader
        );
        ack(&mut r, &mut d, ActionKind::Sidecar);
        ack(&mut r, &mut d, ActionKind::Rebuild);
        ack(&mut r, &mut d, ActionKind::UnknownStyles);
        d.replace_script_field(ScriptField::Width, 0);
        assert_eq!(r.advance(&mut d, None), Err(Error::UnsuppliedScreen));
        assert_eq!(r.action().kind, ActionKind::UnknownStyles);
        assert_eq!(d.script_field(ScriptField::Height), None);
        d.replace_script_field(ScriptField::Height, 0);
        ack(&mut r, &mut d, ActionKind::DoneTrue);
        assert_eq!(r.advance(&mut d, None), Err(Error::Finished));
    }
    #[test]
    fn zero_parsers_still_requires_initial_empty_and_position_capture() {
        let mut d = Document::default();
        let mut r = Run::new(0);
        ack(&mut r, &mut d, ActionKind::CapturePosition);
        ack(&mut r, &mut d, ActionKind::DoneFalse);
        assert_eq!(r.action().index, 0);
        assert_eq!(r.advance(&mut d, Some(false)), Err(Error::Finished));
    }
}
