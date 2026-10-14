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
