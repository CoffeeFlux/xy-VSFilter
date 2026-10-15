#!/usr/bin/env python3
"""Regenerate lexical D0 candidates. Does not prove ownership or reachability.

Reviewed records live in docs/rust-document-inventory.json and are preserved.
Only reads source and git metadata; never executes native reference code.
"""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIELDS = 'm_entries m_segments m_document m_styles m_simple m_name m_lcid m_subtitleType m_mode m_encoding m_path m_dstScreenSize m_defaultWrapStyle m_collisions m_fScaledBAS m_layout_size m_fForcedDefaultStyle m_defaultStyle m_ePARCompensationType m_dPARCompensation m_eYCbCrMatrix m_eYCbCrRange'.split()
METHODS = 'Copy CopyStyles Empty IsEmpty Sort RemoveAllEntries CreateSegments Open SaveAs Add AddSTSEntryOnly CreateDefaultStyle ChangeUnknownStylesToDefault AddStyle SetDefaultStyle GetDefaultStyle SetForceDefaultStyle ConvertToTimeBased ConvertToFrameBased TranslateStart TranslateEnd SearchSub TranslateSegmentStart TranslateSegmentEnd TranslateSegmentStartEnd SearchSubs SearchSubs2 GetSegment GetStyle GetCharSet IsEntryUnicode ConvertUnicode GetStrW GetStrWA SetStr OnChanged GetEntryCount GetEntrySnapshot GetEntryEnd GetEntryLayer GetEntryReadOrder GetEntryEffect GetSegmentEntryCount FindNextSegment SearchSubs2Snapshot IsSegmentAnimated OrSegmentAnimated'.split()
METHODS.extend('GetSegmentCount CopyNamedStylesFrom LookupNamedStyle GetStyleCount HasNamedStyle GetNamedStyleCopy GetEntryStyleCopy SetExistingStyleValue ResetDefaultStyle GetStyleNameAt GetStyleValueAt CopySelectedState CommitStyleRename MaterializeSortRecords EntryTimes CommitTimeConversion NotifySegmentsRebuilt ResolvedStyle ResolvedCharSet DefaultStyle'.split())
METHODS.extend('GetDocumentLayoutSize SetDocumentLayoutSize SetDocumentLayoutWidth SetDocumentLayoutHeight'.split())
METHODS.extend('GetDocumentLCID SetDocumentLCID'.split())
METHODS.extend('GetDocumentName GetDocumentPath SetDocumentName SetDocumentPath'.split())
METHODS.extend('GetDocumentMode GetDocumentType GetDocumentEncoding SetDocumentMode SetDocumentType SetDocumentEncoding'.split())
METHODS.extend('GetDocumentScreenSize SetDocumentScreenSize SetDocumentScreenWidth SetDocumentScreenHeight GetDocumentWrapStyle SetDocumentWrapStyle GetDocumentCollisions SetDocumentCollisions GetDocumentScaledBAS SetDocumentScaledBAS'.split())
METHODS.extend('GetDocumentForcedDefaultStyle SetDocumentForcedDefaultStyle GetDocumentPARType SetDocumentPARType GetDocumentPARCompensation SetDocumentPARCompensation GetDocumentColorMatrix SetDocumentColorMatrix GetDocumentColorRange SetDocumentColorRange'.split())
PATTERN = re.compile(r'\b(' + '|'.join(FIELDS + METHODS + ['CSimpleTextSubtitle', 'STSEntry', 'STSSegment', 'DocumentOwner', 'DocumentReply', 'UnknownStylesPass', 'StyleAddition', 'NamedStyleCopy', 'SubmitStyleSuffix', 'DocumentTextOperation', 'TextOperation', 'ApplyDocumentTextOperation', 'AddFinalSuffix', 'TextWriteOperation', 'DecodeTextOperation', 'OpenLifecycle', 'TranslatedDocumentSearch']) + r')\b')

def scan(reviewed):
    records = []
    files = []
    # Broad scan intentionally retains names belonging to unrelated classes and
    # comments. Reviewed scope records, not lexical matches, establish meaning.
    for path in sorted((ROOT / 'src').rglob('*')):
        if path.suffix not in ('.cpp', '.h'):
            continue
        raw = path.read_bytes()
        matches = []
        for number, line in enumerate(raw.decode('utf-8', errors='replace').splitlines(), 1):
            tokens = sorted(set(PATTERN.findall(line)))
            if tokens:
                matches.append({'line': number, 'tokens': tokens, 'source': line.strip()})
        if matches:
            relative = str(path.relative_to(ROOT))
            files.append({'path': relative, 'sha256': hashlib.sha256(raw).hexdigest()})
            for record in matches:
                review_ids = []
                for review in reviewed:
                    for location in review['locations']:
                        location_path, span = location.rsplit(':', 1)
                        ends = [int(n) for n in span.split('-')]
                        if relative == location_path and ends[0] <= record['line'] <= ends[-1]:
                            review_ids.append(review['id'])
                            break
                records.append({'path': relative, **record, 'reviewed_scope_ids': review_ids,
                                'evidence_status': 'lexical-candidate-in-reviewed-scope' if review_ids else 'lexical-candidate-only'})
    return files, records

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    target = ROOT / 'docs/rust-document-inventory.json'
    data = json.loads(target.read_text())
    files, records = scan(data['reviewed_accesses'])
    generated = {'candidate_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(), 'capture_scope': 'Working-tree source hashes; candidate_commit is capture base HEAD, not a claim that uncommitted source is in that commit.', 'source_files': files, 'candidates': records}
    if args.check:
        if any(data['generated'][key] != generated[key] for key in ('source_files', 'candidates')):
            raise SystemExit('Document lexical inventory is stale; regenerate and review changed source.')
        print('Document lexical source hashes and candidates match; capture commit remains provenance, not a current-HEAD requirement. Semantic classifications require review.')
        return
    data['generated'] = generated
    target.write_text(json.dumps(data, indent=2, ensure_ascii=True) + '\n')
    print(f'Wrote {len(records)} lexical candidate lines; these are not reviewed caller counts.')

if __name__ == '__main__':
    main()
