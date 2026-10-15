import unittest
import hashlib
import json
import tempfile
from pathlib import Path
from unittest.mock import patch

from summarize_evidence import normalize_windows_source, windows_source_bytes, production_dll, document_lifecycle_requirements, verify_document_dialogs


class WindowsHeaderProvenance(unittest.TestCase):
    def test_lifecycle_archive_requirements_follow_verified_invocations(self):
        self.assertEqual(document_lifecycle_requirements(b"old probe"),
                         {"require_named_stages": False, "require_identity_stages": False, "require_interpretation_stages": False, "require_layout_stages": False, "require_metadata_stages": False, "require_selected_copy_stages": False, "require_style_rename_stages": False, "require_sort_stages": False, "require_base_empty_stages": False, "require_lcid_stages": False, "require_resolution_stages": False, "require_layout_size_stages": False, "require_conversion_stages": False, "require_unknown_stages": False, "require_add_style_stages": False, "require_copy_styles_stages": False, "require_text_plan_stages": False, "require_preparation_stages": False, "require_setstr_stages": False, "require_open_order_stages": False, "require_frame_search_stages": False, "require_save_body_stages": False, "require_save_sections_stages": False})
        source=b"int main() {\n    named_style_lifecycle();\n    identity_lifecycle();\n}"
        self.assertEqual(document_lifecycle_requirements(source),
                         {"require_named_stages": True, "require_identity_stages": True, "require_interpretation_stages": False, "require_layout_stages": False, "require_metadata_stages": False, "require_selected_copy_stages": False, "require_style_rename_stages": False, "require_sort_stages": False, "require_base_empty_stages": False, "require_lcid_stages": False, "require_resolution_stages": False, "require_layout_size_stages": False, "require_conversion_stages": False, "require_unknown_stages": False, "require_add_style_stages": False, "require_copy_styles_stages": False, "require_text_plan_stages": False, "require_preparation_stages": False, "require_setstr_stages": False, "require_open_order_stages": False, "require_frame_search_stages": False, "require_save_body_stages": False, "require_save_sections_stages": False})
        self.assertTrue(document_lifecycle_requirements(b"    copy_selected_lifecycle();")["require_selected_copy_stages"])
        self.assertTrue(document_lifecycle_requirements(b"    prepared_style_conflicts();")["require_style_rename_stages"])
        self.assertTrue(document_lifecycle_requirements(b"    sort_lifecycle();")["require_sort_stages"])
        self.assertTrue(document_lifecycle_requirements(b"    base_empty_lifecycle();")["require_base_empty_stages"])
        self.assertTrue(document_lifecycle_requirements(b"    lcid_lifecycle();")["require_lcid_stages"])
        self.assertTrue(document_lifecycle_requirements(b"    resolution_lifecycle();")["require_resolution_stages"])
        self.assertTrue(document_lifecycle_requirements(b"    layout_size_lifecycle();")["require_layout_size_stages"])
        self.assertTrue(document_lifecycle_requirements(b"    conversion_lifecycle();")["require_conversion_stages"])
        self.assertTrue(document_lifecycle_requirements(b"    unknown_style_lifecycle();")["require_unknown_stages"])
        self.assertTrue(document_lifecycle_requirements(b"    add_style_session_lifecycle();")["require_add_style_stages"])
        self.assertTrue(document_lifecycle_requirements(b"    copy_styles_session_lifecycle();")["require_copy_styles_stages"])
        self.assertTrue(document_lifecycle_requirements(b"    text_plan_lifecycle();")["require_text_plan_stages"])
        self.assertTrue(document_lifecycle_requirements(b"    preparation_lifecycle();")["require_preparation_stages"])
        self.assertTrue(document_lifecycle_requirements(b"    setstr_plan_lifecycle();")["require_setstr_stages"])
        self.assertTrue(document_lifecycle_requirements(b"    open_order_lifecycle();")["require_open_order_stages"])
        self.assertTrue(document_lifecycle_requirements(b"    frame_search_lifecycle();")["require_frame_search_stages"])
        self.assertTrue(document_lifecycle_requirements(b"    save_body_lifecycle();")["require_save_body_stages"])
        self.assertTrue(document_lifecycle_requirements(b"    save_sections_lifecycle();")["require_save_sections_stages"])
        self.assertTrue(document_lifecycle_requirements(b"    metadata_lifecycle();")["require_metadata_stages"])
        self.assertTrue(document_lifecycle_requirements(b"    layout_lifecycle();")["require_layout_stages"])
        self.assertTrue(document_lifecycle_requirements(b'    interpretation_lifecycle();')['require_interpretation_stages'])
        self.assertFalse(document_lifecycle_requirements(b'// identity_lifecycle();\n"identity_lifecycle();"')['require_identity_stages'])

    def test_dialog_proof_requires_exact_observer_and_all_retained_observations(self):
        from document_native_lifecycle import unknown_dialog_actions
        events=[{"title":"Warning","class":"#32770","response":action["button"],"controls":[
            {"class":"Button","id":1,"text":"OK","style":0},
            {"class":"Button","id":2,"text":"Cancel","style":0},
            {"class":"Static","id":65535,"text":action["text_prefix"],"style":0}]}
            for action in unknown_dialog_actions()]
        observer=b"exact candidate observer\n"
        digest=lambda value:hashlib.sha256(value).hexdigest()
        canonical=lambda rows:digest(json.dumps(rows,sort_keys=True).encode())
        report={"dialogs_equal":True,"dialog_count":3,"dialog_sha256":[canonical(events)]*4,
                "dialog_observer_sha256":digest(observer)}
        with tempfile.TemporaryDirectory() as temporary,patch("summarize_evidence.windows_source_bytes",return_value=observer) as source:
            directory=Path(temporary);paths=[]
            for side in ("reference","candidate"):
                for repeat in (1,2):
                    path=directory/f"{side}-trace-{repeat}-dialogs"/"dialogs.json"
                    path.parent.mkdir();path.write_text(json.dumps(events)+"\n");paths.append(path)
            verified=verify_document_dialogs(directory,report,"exact-commit")
            source.assert_called_with("exact-commit","tools/compat/dialog_capture.py")
            self.assertEqual(verified["count"],3);self.assertEqual(len(verified["retained_file_sha256"]),4)
            for field,value in (("dialog_observer_sha256","0"*64),("dialogs_equal",False),("dialog_count",2),("dialog_sha256",["0"*64]*4)):
                bad=dict(report);bad[field]=value
                with self.assertRaises(AssertionError):verify_document_dialogs(directory,bad,"exact-commit")
            paths[-1].unlink()
            with self.assertRaises(FileNotFoundError):verify_document_dialogs(directory,report,"exact-commit")
            paths[-1].write_text(json.dumps(events))
            changed=json.loads(json.dumps(events));changed[0]["controls"][0]["style"]=1
            paths[-1].write_text(json.dumps(changed))
            with self.assertRaises(AssertionError):verify_document_dialogs(directory,report,"exact-commit")
            changed=json.loads(json.dumps(events));changed[0]["controls"][2]["text"]+="suffix"
            for path in paths:path.write_text(json.dumps(changed))
            altered=dict(report);altered["dialog_sha256"]=[canonical(changed)]*4
            with self.assertRaises(ValueError):verify_document_dialogs(directory,altered,"exact-commit")

    def test_production_dll_excludes_instrumented_copies(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            production = root / 'candidate/bin/lib_17.0/x64/Release/VSFilter.dll'
            helper = root / 'candidate/artifacts/native/x64/document-style-api/candidate/VSFilter/VSFilter.dll'
            for path in (production, helper):
                path.parent.mkdir(parents=True)
                path.write_bytes(b'fixture')
            self.assertEqual(production_dll(root, 'candidate', 'VSFilter.dll'), production)
            production.unlink()
            with self.assertRaises(AssertionError):
                production_dll(root, 'candidate', 'VSFilter.dll')

    def test_compiler_spelling_resolves_to_tracked_path(self):
        self.assertEqual(normalize_windows_source(r"src\subtitles\..\DSUtil\DSUtil.h"),
                         "src/dsutil/dsutil.h")
        with patch("summarize_evidence.source_paths", return_value={"src/dsutil/dsutil.h": "src/dsutil/DSUtil.h"}), \
             patch("summarize_evidence.subprocess.run") as run:
            run.return_value.stdout = b"source"
            self.assertEqual(windows_source_bytes("commit", "src/subtitles/../DSUtil/DSUtil.h"), b"source")
            self.assertEqual(run.call_args.args[0], ["git", "show", "commit:src/dsutil/DSUtil.h"])

    def test_escape_and_untracked_headers_are_refused(self):
        for name in ("../outside.h", "src/../../outside.h", "/outside.h", r"C:\outside.h"):
            with self.assertRaises(ValueError):
                normalize_windows_source(name)
        with patch("summarize_evidence.source_paths", return_value={}):
            with self.assertRaises(KeyError):
                windows_source_bytes("commit", "src/untracked.h")


if __name__ == "__main__":
    unittest.main()
