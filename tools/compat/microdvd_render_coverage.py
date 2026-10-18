"""Validate complete current corpus coverage without relabeling failed controls.

Reuse only unchanged case definitions from the completed full corpus, replacing
all 20 MicroDVD timing cases and their controls with the corrected focused run.
"""
from pathlib import Path
import hashlib,json,sys
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
arch=sys.argv[1];base=Path('artifacts')/('microdvd-csri-'+arch)
canonical=json.loads(Path('test/compat/cases.json').read_text())
by_id={c['id']:c for c in canonical['cases']}
new_manifest=Path('artifacts/microdvd-new-v2-cases.json')
new=json.loads(new_manifest.read_text());replaced={c['id'] for c in new['cases']}
assert len(replaced)==20
selected={};controls={};reports=[];candidate=None;reference=None
def inspect(report_path,manifest_path,replace):
 global candidate,reference
 d=json.loads(report_path.read_text());m=json.loads(manifest_path.read_text())
 assert d['manifest_sha256']==sha(manifest_path)
 assert all(d[key] is True for key in ('complete','environment_unchanged','exports_equal'))
 assert all(c['verdict']['kind']=='equal' and c['candidate_status']==c['reference_status'] and c['candidate_status'] in ('ok','open_rejected') for c in d['cases'])
 outcomes={c['id']:c['candidate_status'] for c in d['cases']}
 assert sorted(c['id'] for c in d['cases'])==sorted(c['id'] for c in m['cases'])
 assert d['rendered_frames']==sum(len(c['times']) for c in m['cases'] if outcomes[c['id']]=='ok')
 if replace:assert d['passed'] and d['controls_passed']
 if candidate is None:candidate=d['candidate_dll'];reference=d['reference_source']
 assert d['candidate_dll']==candidate and d['reference_source']==reference
 assert sha(candidate['path'])==candidate['sha256']
 assert reference==json.loads(Path('tools/compat/reference.json').read_text())
 used=[]
 for case in m['cases']:
  if (case['id'] in replaced)!=replace:continue
  assert case==by_id[case['id']],case['id']
  assert case['id'] not in selected
  assert sha(case['script'])==case['sha256']
  selected[case['id']]=len(case['times']) if outcomes[case['id']]=='ok' else 0;used.append(case['id'])
 for control in d['distinct_frame_controls']:
  pair=tuple(control['cases'])
  if any(c in replaced for c in pair)!=replace:continue
  assert all(control[k] is True for k in control if k!='cases')
  assert pair not in controls
  controls[pair]=str(report_path)
 reports.append({'report':str(report_path),'sha256':sha(report_path),'manifest':str(manifest_path),'manifest_sha256':sha(manifest_path),'original_passed':d['passed'],'reused_cases':used})
for i in range(8):inspect(base/f'shard-{i}/report.json',base/f'shard-{i}.json',False)
inspect(Path(f'artifacts/microdvd-new-v2-{arch}/report.json'),new_manifest,True)
assert sorted(selected)==sorted(by_id)
assert set(controls)=={tuple(p) for p in canonical['expect_distinct_frames']}
result={'schema':1,'complete':True,'passed':True,'partition_exact':True,'cases':len(selected),'frames':sum(selected.values()),'distinct_controls':len(controls),'candidate_dll':candidate,'reference_source':reference,'source_manifest_sha256':sha('test/compat/cases.json'),'strategy':'874 unchanged full-corpus cases plus 20 corrected timing cases. Initial failed style-timing controls are excluded, never relabeled as passing. All selected case definitions, input hashes, binary hashes and controls checked against the current canonical manifest.','reports':reports}
assert result['cases']==894 and result['frames']==9586 and result['distinct_controls']==88
(base/'coverage.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='reports'}))
