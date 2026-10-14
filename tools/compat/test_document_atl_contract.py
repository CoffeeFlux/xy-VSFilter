import json
import unittest
import document_atl_contract as contract


class AtlContractTests(unittest.TestCase):
    def test_narrow_definition_selection_and_hash(self):
        source = '''// Not retained header material
class CAtlMap {
public:
 CAtlMap(unsigned bins=17, float load=0.75f);
 unsigned GetHashTableSize() const { return m_nBins; }
 void Unrelated() { Lookup(1); }
 typedef const wchar_t* KINARGTYPE;
 unsigned m_nBins;
};
template<class K, class V>
bool CAtlMap<K,V>::Lookup(int key) const {
 const char* text="} fake";
 /* } */ return key != 0;
}
class CStringElementTraits {
public:
 static unsigned Hash(const wchar_t* p) { return *p; }
 static bool CompareElements(int a,int b) { return a==b; }
};
'''
        selected = contract.select_definitions(source, 'atlcoll.h')
        symbols = [r['symbol'] for r in selected]
        self.assertIn('CAtlMap', symbols)
        self.assertIn('GetHashTableSize', symbols)
        self.assertIn('Lookup', symbols)
        self.assertIn('Hash', symbols)
        self.assertNotIn('Unrelated', symbols)
        self.assertEqual(sum(r['symbol'] == 'Lookup' for r in selected), 1)
        for row in selected:
            self.assertEqual(row['sha256_utf8'], contract.sha(row['excerpt'].encode()))
            self.assertGreaterEqual(row['first_line'], 1)
            self.assertGreaterEqual(row['last_line'], row['first_line'])
        self.assertFalse(any('Not retained' in r['excerpt'] for r in selected))

    def test_cstringt_specialization_primes_pair_fields_and_remove_node(self):
        self.assertIn('cstringt.h', contract.SELECTED_HEADERS)
        text = """class CAtlMap {
 class CPair {
 public:
  const K m_key;
  V m_value;
  CPair(KINARGTYPE key) : m_key(key) { }
 };
 void RemoveNode(CNode* node) { FreeNode(node); }
};
static const UINT s_anPrimes[] = {17, 37, UINT_MAX};
class CStringElementTraits< CStringT<BaseType, StringTraits> > {
public:
 static ULONG Hash(_In_ INARGTYPE str) throw() { return 7; }
 static bool CompareElements(
   _In_ INARGTYPE a,
   _In_ INARGTYPE b) throw() { return a == b; }
};
"""
        rows = contract.select_definitions(text, 'cstringt.h')
        symbols = [row['symbol'] for row in rows]
        for name in ('Hash', 'CompareElements', 's_anPrimes', 'CPair field declaration', 'CPair', 'RemoveNode'):
            self.assertIn(name, symbols)
        prime = next(row for row in rows if row['symbol'] == 's_anPrimes')
        self.assertIn('{17, 37, UINT_MAX}', prime['excerpt'])

    def test_native_probe_uses_document_mfc_string_setup(self):
        self.assertLess(contract.PROBE.index('#include <afx.h>'), contract.PROBE.index('#include <atlcoll.h>'))
        self.assertNotIn('#include <windows.h>', contract.PROBE)
        self.assertIn('#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS', contract.PROBE)
        self.assertIn('AfxWinInit', contract.PROBE)
        self.assertIn('typeid(CString).name()', contract.PROBE)

    def test_mask_balanced_literals_comments(self):
        text = '{ "}"; /* } */ { \'{\'; } // }\n}'
        code = contract.masked(text)
        self.assertEqual(contract.closing(code, 0), len(text))
        self.assertEqual(code.count('\n'), text.count('\n'))
        with self.assertRaises(ValueError): contract.closing('{', 0)

    def test_csize_zero_argument_source_only_and_exact_hashes(self):
        text = """#ifndef SYNTHETIC_SIZE_H
#define SYNTHETIC_SIZE_H
class CSize : public SyntheticBase {
public:
 CSize() noexcept;
 CSize(int width, int height);
 void Ignore() { CSize(); }
};
inline CSize::CSize() noexcept { /* } */ }
#endif
"""
        rows = contract.select_csize_definitions(text, 'atltypes.h')
        self.assertEqual([r['kind'] for r in rows], ['declaration', 'declaration', 'definition'])
        self.assertEqual(contract.csize_source_contract(rows)['complete'], True)
        for row in rows:
            self.assertEqual(row['source_utf8_sha256'], contract.sha(text.encode()))
            self.assertEqual(row['sha256_utf8'], contract.sha(row['excerpt'].encode()))
            self.assertTrue(row['conditional_context'][0]['include_guard'])
        self.assertFalse(any('Ignore' in r['excerpt'] for r in rows))
        self.assertFalse(any('CSize(int' in r['excerpt'] for r in rows))

    def test_csize_inline_definition_is_also_its_declaration(self):
        text = 'class CSize : public SyntheticBase { public: CSize(void) noexcept { marker = 42; } int marker; };'
        rows = contract.select_csize_definitions(text, 'atltypes.h')
        self.assertTrue(contract.csize_source_contract(rows)['complete'])
        body = next(r for r in rows if r['kind'] == 'definition')
        self.assertEqual(body['excerpt'], 'CSize(void) noexcept { marker = 42; }')
        self.assertNotIn('int marker', body['excerpt'])
        # Selection records source; it deliberately does not interpret values.
        self.assertNotIn('initial_value', body)

    def test_csize_alternatives_and_unknown_conditions_never_pass(self):
        text = """class CSize { public: CSize(); };
#if SELECTED_BRANCH
inline CSize::CSize() { first(); }
#else
inline CSize::CSize() { second(); }
#endif
"""
        rows = contract.select_csize_definitions(text, 'atltypes.h')
        status = contract.csize_source_contract(rows)
        self.assertFalse(status['complete'])
        self.assertFalse(status['source_capture_complete'])
        self.assertEqual(status['default_definition_count'], 2)
        self.assertFalse(status['active_conditions_resolved'])
        self.assertEqual(status['active_conditions_unresolved'][1]['conditional_context'][0]['branch'], '#else')
        one = contract.select_csize_definitions('class CSize { public: CSize(); };\n#if FEATURE\nCSize::CSize() {}\n#endif', 'afx.inl')
        status = contract.csize_source_contract(one)
        self.assertTrue(status['source_capture_complete'])
        self.assertFalse(status['complete'])
        self.assertFalse(status['active_conditions_resolved'])

    def test_csize_context_ignores_comments_and_retains_continuations(self):
        text = '/*\n#if COMMENT_ONLY\n*/\nclass CSize { public: CSize(); };\n#if FEATURE && \\\n OTHER_FEATURE\nCSize::CSize() {}\n#endif'
        rows = contract.select_csize_definitions(text, 'atltypes.h')
        status = contract.csize_source_contract(rows)
        self.assertFalse(status['complete'])
        context = status['active_conditions_unresolved'][0]['conditional_context']
        self.assertEqual(len(context), 1)
        self.assertIn('OTHER_FEATURE', context[0]['directive'])
        self.assertNotIn('COMMENT_ONLY', context[0]['directive'])

    def test_csize_only_first_header_scaffolding_is_an_include_guard(self):
        genuine = '// header comment\n#pragma once\n#ifndef SYNTHETIC_GUARD\n/* comment */\n#define SYNTHETIC_GUARD\nclass CSize { public: CSize() {} };\n#endif'
        self.assertTrue(contract.csize_source_contract(contract.select_csize_definitions(genuine, 'atltypes.h'))['complete'])
        late = 'void earlier_declaration();\n#ifndef FEATURE_SWITCH\n#define FEATURE_SWITCH\nclass CSize { public: CSize() {} };\n#endif'
        rows = contract.select_csize_definitions(late, 'atltypes.h')
        self.assertFalse(rows[0]['conditional_context'][0]['include_guard'])
        status = contract.csize_source_contract(rows)
        self.assertFalse(status['complete'])
        self.assertFalse(status['active_conditions_resolved'])

    def test_csize_constructor_internal_conditions_remain_unresolved(self):
        text = 'class CSize { public: CSize(); };\nCSize::CSize() {\n#ifdef INTERNAL_FEATURE\n first();\n#else\n second();\n#endif\n}'
        rows = contract.select_csize_definitions(text, 'afx.inl')
        status = contract.csize_source_contract(rows)
        self.assertTrue(status['source_capture_complete'])
        self.assertFalse(status['complete'])
        self.assertFalse(status['active_conditions_resolved'])
        unresolved = status['active_conditions_unresolved'][0]
        self.assertEqual(unresolved['conditional_context'], [])
        self.assertEqual([r['directive'] for r in unresolved['internal_conditions']],
                         ['#ifdef INTERNAL_FEATURE', '#else', '#endif'])
        self.assertEqual(unresolved['internal_conditions'][0]['first_line'], 3)
        deleted = contract.select_csize_definitions('class CSize { public: ~CSize() {} };', 'atltypes.h')
        self.assertFalse(contract.csize_source_contract(deleted)['source_capture_complete'])

    def test_csize_missing_deleted_and_unsupported_source_stay_unresolved(self):
        self.assertFalse(contract.csize_source_contract([])['complete'])
        rows = contract.select_csize_definitions('class CSize { public: CSize() = delete; };', 'atltypes.h')
        self.assertFalse(contract.csize_source_contract(rows)['complete'])
        with self.assertRaises(ValueError):
            contract.select_csize_definitions('class CSize { public: CSize() : base{1,2} {} };', 'atltypes.h')
        text = 'class CSize { public: CSize() { ' + ('do_work();\n' * 50) + '} };'
        with self.assertRaises(ValueError):
            contract.select_csize_definitions(text, 'atltypes.h')

    def test_active_csize_uses_preprocessed_body_not_final_macro_guess(self):
        sdk = 'class CSize : public SyntheticBase { public: CSize(); };\n#ifndef INTERNAL_LIBRARY_BUILD\ninline CSize::CSize() { first = 0; second = 0; }\n#endif'
        rows = contract.select_csize_definitions(sdk, 'atltypes.h')
        self.assertFalse(contract.csize_source_contract(rows)['complete'])
        preprocessed = ('Note: including file: C:\\sdk\\atltypes.h\n'
                        'class CSize : public SyntheticBase { public: CSize(); };\n'
                        'inline CSize::CSize() { first=0; /* whitespace irrelevant */ second=0; }\n'
                        'XY_DOCUMENT_CSIZE_ATL_STATIC_LIB_IMPL_DEFINED\n').encode()
        active, log = contract.select_active_csize(preprocessed, 'synthetic-context', rows)
        self.assertTrue(active['complete'])
        self.assertEqual(active['macro_state_after_includes'], 'defined')
        self.assertTrue(active['active_default_body_matches_selected_sdk_source'])
        self.assertEqual(active['preprocessed_stream_sha256'], contract.sha(preprocessed))
        self.assertFalse(active['full_preprocessed_stream_retained'])
        self.assertNotIn(b'class CSize', log)
        self.assertNotIn(b'first=0', log)
        self.assertIn(b'Note: including file:', log)
        self.assertIn(b'XY_DOCUMENT_CSIZE_ATL_STATIC_LIB_IMPL_DEFINED', log)
        # An apparently favorable final macro state cannot rescue a different body.
        wrong = preprocessed.replace(b'first=0', b'first=9').replace(b'_DEFINED', b'_UNDEFINED')
        active, _ = contract.select_active_csize(wrong, 'synthetic-context', rows)
        self.assertFalse(active['complete'])
        self.assertFalse(active['active_default_body_matches_selected_sdk_source'])
        active, _ = contract.select_active_csize(preprocessed.replace(b'XY_DOCUMENT_CSIZE_ATL_STATIC_LIB_IMPL_DEFINED', b''), 'synthetic-context', rows)
        self.assertFalse(active['complete'])

    def test_csize_context_is_preprocess_only_with_actual_subtitles_first(self):
        for platform in ('Win32', 'x64'):
            for experimental in (False, True):
                command = contract.csize_context_command('cl', 'context.cpp', platform, experimental)
                self.assertIn('/EP', command)
                self.assertNotIn('/c', command)
                self.assertNotIn('/link', command)
                self.assertIn('/MT', command)
                self.assertIn('/fp:precise', command)
                self.assertIn('/DXY_VSFILTER_RUST_TEXT', command)
                self.assertEqual('/DXY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL' in command, experimental)
                self.assertTrue(next(item for item in command if item.startswith('/I')).replace('\\', '/').endswith('/src/subtitles'))
        self.assertIn('#include <stdafx.h>', contract.CSIZE_CONTEXT_SOURCE)
        self.assertIn('#include <STS.h>', contract.CSIZE_CONTEXT_SOURCE)
        self.assertNotIn('CSize', contract.CSIZE_CONTEXT_SOURCE)

    def test_cstring_preparation_exact_overloads_and_inherited_empty(self):
        text = """#pragma once
class CSimpleStringT {
public:
 typedef SyntheticUnit XCHAR;
 typedef const XCHAR* PCXSTR;
 bool IsEmpty() const noexcept { return length == 0; }
 int unrelated;
};
class CStringT : public CSimpleStringT {
public:
 typedef CSimpleStringT CThisSimpleString;
 typedef typename CThisSimpleString::XCHAR XCHAR;
 typedef typename CThisSimpleString::PCXSTR PCXSTR;
 int Remove(_In_ XCHAR value) { const char* text="}"; return selected_remove(); }
 int Remove(XCHAR* other) { return wrong_pointer(); }
 int Replace(_In_z_ PCXSTR old_value, _In_z_ PCXSTR new_value) { return selected_replace(); }
 int Replace(XCHAR old_value, XCHAR new_value) { return wrong_character_pair(); }
 void TrimLeft(_In_ XCHAR value) { selected_trim(); }
 void TrimLeft() { wrong_whitespace(); }
 void TrimLeft(PCXSTR values) { wrong_set(); }
 void Helper() { Remove(XCHAR()); }
};
class Unrelated { int Remove(XCHAR value) { return 99; } };
"""
        rows = contract.select_cstring_preparation(text, 'cstringt.h')
        status = contract.cstring_preparation_contract(rows)
        self.assertTrue(status['complete'])
        self.assertEqual(set(status['definition_counts'].values()), {1})
        methods = [r for r in rows if r['kind'] == 'definition']
        self.assertEqual({r['symbol'] for r in methods},
                         {'CStringT::Remove(XCHAR)', 'CStringT::Replace(PCXSTR,PCXSTR)',
                          'CStringT::TrimLeft(XCHAR)', 'CSimpleStringT::IsEmpty()'})
        excerpts = '\n'.join(r['excerpt'] for r in rows)
        self.assertNotIn('wrong_', excerpts)
        self.assertNotIn('Helper', excerpts)
        self.assertNotIn('int unrelated', excerpts)
        self.assertTrue(any(r['symbol'] == 'CStringT argument/base alias' for r in rows))
        for row in rows:
            self.assertEqual(row['sha256_utf8'], contract.sha(row['excerpt'].encode()))
            self.assertEqual(row['source_utf8_sha256'], contract.sha(text.encode()))

    def test_cstring_preparation_qualified_definitions_and_conditions(self):
        text = """class CStringT { public: int Remove(XCHAR value); };
template<class T>
int CStringT<T>::Remove(XCHAR value) { return 1; }
#if STRING_FEATURE
class CSimpleStringT { public: bool IsEmpty() const { return false; } };
#endif
"""
        rows = contract.select_cstring_preparation(text, 'cstringt.h')
        remove = [r for r in rows if r['symbol'] == 'CStringT::Remove(XCHAR)']
        self.assertEqual([r['kind'] for r in remove], ['declaration', 'definition'])
        status = contract.cstring_preparation_contract(rows)
        self.assertFalse(status['complete'])
        self.assertFalse(status['source_capture_complete'])
        self.assertFalse(status['active_conditions_resolved'])
        body = 'class CStringT { public: void TrimLeft(XCHAR unit) {\n#ifdef BODY_FEATURE\n adjust();\n#endif\n} };'
        rows = contract.select_cstring_preparation(body, 'cstringt.h')
        trim = next(r for r in rows if r['kind'] == 'definition')
        self.assertEqual(trim['internal_conditions'][0]['directive'], '#ifdef BODY_FEATURE')
        self.assertFalse(contract.cstring_preparation_contract(rows)['active_conditions_resolved'])

    def test_cstring_preparation_never_resolves_ambiguous_or_oversized_methods(self):
        text = 'class CStringT { public: int Remove(XCHAR value) { return 1; } int Remove(XCHAR value) { return 2; } };'
        status = contract.cstring_preparation_contract(contract.select_cstring_preparation(text, 'cstringt.h'))
        self.assertEqual(status['definition_counts']['Remove(XCHAR)'], 2)
        self.assertFalse(status['source_capture_complete'])
        oversized = 'class CStringT { public: int Replace(PCXSTR a, PCXSTR b) {\n' + 'statement();\n' * 501 + '} };'
        with self.assertRaises(ValueError):
            contract.select_cstring_preparation(oversized, 'cstringt.h')
        self.assertFalse(contract.cstring_preparation_contract([])['complete'])

    def trace(self):
        rows = [{'event': 'provenance', 'pointer_bits': 64, 'cstring_type': 'ATL::CStringT<wchar_t, StrTraitMFC<wchar_t>>'}]
        seq = 0
        for op, count in [('construct',1),('insert',128),('existing-update',1),('remove',64),('reinsert',64),('ordered-copy',128),('clear',1),('reuse',1),('raw-key-insert',10)]:
            for _ in range(count):
                rows.append({'event':'state','seq':seq,'op':op,'count':0,'bins':17,'order':[]}); seq += 1
        rows += [{'event':'key','index':i} for i in range(10)]
        rows += [{'event':'complete','marker':'document-atl-contract-v1 complete'}]
        return rows

    def encode(self, rows): return ('\n'.join(json.dumps(r) for r in rows)+'\n').encode()

    def test_complete_bounded_trace(self):
        rows = self.trace()
        self.assertEqual(contract.validate_trace(self.encode(rows),64), rows)

    def test_negative_trace_controls(self):
        rows = self.trace()
        cases = [rows[:-1], rows + [rows[-1]], rows[:10]+rows[11:]]
        altered = json.loads(json.dumps(rows));altered[1]['count']=1;cases.append(altered)
        altered = json.loads(json.dumps(rows));altered[1]['bins']=0;cases.append(altered)
        for bad in cases:
            with self.assertRaises(ValueError):contract.validate_trace(self.encode(bad),64)
        with self.assertRaises(ValueError):contract.validate_trace(self.encode(rows),32)
        rows[0]['cstring_type']='ATL::CStringT<wchar_t, StrTraitATL<wchar_t>>'
        with self.assertRaises(ValueError):contract.validate_trace(self.encode(rows),64)


if __name__=='__main__':unittest.main()
