import unittest
from unittest.mock import patch
from tempfile import TemporaryDirectory
from pathlib import Path
import subprocess
import document_native_lifecycle as lifecycle

from document_native_lifecycle import compare_traces, conversion_rows, unknown_rows


STYLE_SCALARS = b" ".join(b"0000000000000000" if i in (0,11,12,13,14,23,24,25,26,27,28,29,30,31,32) else b"00000000" if i in (15,17,19,21) else b"0" for i in range(34))
STYLE_VALUE = b"style 0 1 0041 " + STYLE_SCALARS

TRACE = (b"provenance pointer_bits=64\nstate copy\nentry 0 raw=0041,0000,0042\n"
         b"segment 0 10 20\ncallback changed\ntext 0041\n"
         + b"".join(b"named-stage " + stage + b"\n" for stage in (
             b"named-initial", b"named-equal-duplicate", b"named-conflict-collision",
             b"named-numeric-collision", b"named-copy-replace", b"named-copy-independent",
             b"named-copy-append", b"named-self-clear", b"named-empty"))
         + b"".join(b"identity-stage " + stage + b" name 4 004e 0000 d800 0058 path 4 0050 0000 dcff 0059\n" for stage in (
             b"identity-initial", b"identity-read-independent", b"identity-copy-asymmetric-path",
             b"identity-copy-independent-source", b"identity-callback", b"identity-remove-entries",
             b"identity-empty", b"identity-self-copy", b"identity-open-success"))
         + b"".join(b"interpretation-stage " + stage + b" mode=0 type=1 encoding=0\n" for stage in (
             b"interpretation-constructed", b"interpretation-supplied", b"interpretation-copy",
             b"interpretation-copy-independent", b"interpretation-empty", b"interpretation-self-copy",
             b"interpretation-microdvd-open", b"interpretation-frame", b"interpretation-frame-noop",
             b"interpretation-time", b"interpretation-time-noop"))
         + b"".join(b"layout-stage " + stage + b" width=0 height=0 wrap=0 collisions=0 scaled=0\n" for stage in (
             b"layout-constructed", b"layout-supplied", b"layout-copy", b"layout-copy-independent",
             b"layout-empty", b"layout-self-copy", b"layout-open-fallback", b"layout-open-x1280",
             b"layout-open-y1024", b"layout-open-repeated"))
         + b"".join(b"layout-callback " + stage + b" width=0 height=0\n" for stage in (
             b"layout-open-fallback", b"layout-open-x1280", b"layout-open-y1024", b"layout-open-repeated"))
         + b"".join(b"metadata-stage " + stage + b" forced=0 par-type=0 par-bits 3ff0000000000000 matrix=3 range=2\n" for stage in (
             b"metadata-constructed", b"metadata-supplied", b"metadata-copy-forced-only",
             b"metadata-copy-independent", b"metadata-empty", b"metadata-self-copy",
             b"metadata-forced-style", b"metadata-unforced-style", b"metadata-color-none",
             b"metadata-color-tv601", b"metadata-color-tv709", b"metadata-color-tv2020",
             b"metadata-color-pc601", b"metadata-color-pc709", b"metadata-color-pc2020",
             b"metadata-color-unrecognized", b"metadata-color-repeated", b"metadata-fallback-srt",
             b"metadata-fallback-microdvd"))
         + b"".join(b"metadata-callback " + stage + b" forced=0 par-type=0 par-bits 3ff0000000000000 matrix=3 range=2\n" for stage in (
             b"metadata-color-none", b"metadata-color-tv601", b"metadata-color-tv709", b"metadata-color-tv2020",
             b"metadata-color-pc601", b"metadata-color-pc709", b"metadata-color-pc2020", b"metadata-color-unrecognized",
             b"metadata-color-repeated", b"metadata-fallback-srt", b"metadata-fallback-microdvd"))
         + b"".join(b"copy-state-stage " + stage + b"\n" for stage in (
             b"copy-source-before", b"copy-source-mutated", b"copy-destination-after", b"copy-source-after",
             b"copy-empty-key-source", b"copy-empty-key-result", b"copy-empty-key-source-after"))
         + b"".join(b"style-rename-stage " + stage + b"\n" for stage in (
             b"numeric-repeat-12", b"numeric-repeat-13", b"raw-nul-before", b"raw-nul-to12",
             b"raw-nul-to2", b"shrink-post-empty", b"shrink-before", b"shrink-after"))
         + b"".join(b"style-rename-segments " + stage + b" count=2 animated=1\n" for stage in (
             b"numeric-original", b"numeric-repeat-12", b"numeric-repeat-13", b"named-final",
             b"raw-nul-before", b"raw-nul-to12", b"raw-nul-to2", b"shrink-before", b"shrink-after"))
         + b"sort-stage sort-input\n"
         + b"".join(b"sort-callback " + stage + b" entries=12 segments=4 0 1 2 3 4 5 6 7 8 9 10 11\nsort-stage " + stage + b"\n" for stage in (
             b"sort-time", b"sort-time-repeat", b"sort-readorder", b"sort-readorder-repeat"))
         + b"".join(b"empty-stage " + stage + b"\n" for stage in (
             b"empty-populated", b"empty-cleared", b"empty-repeated", b"empty-reused", b"empty-final"))
         + b"".join(f"lcid-stage {stage} source={source} destination={destination}\n".encode() for stage,source,destination in (
             ("lcid-supplied",1031,0),("lcid-copy-retained",1031,0),("lcid-source-independent",1033,0),
             ("lcid-repeated-empty",1033,0),("lcid-reused-empty",1033,1031)))
         + b"".join(b"resolution-stage " + stage + b"\nresolved-style " + stage + b" charset=0 " + STYLE_VALUE + b"\n" for stage in (
             b"resolution-raw-prefix",b"resolution-lowercase",b"resolution-case-fallback",b"resolution-empty-prefix",
             b"resolution-forced",b"resolution-forced-replaced",b"resolution-output-independent"))
         + b"resolution-mutated-output style 0 raw-font-mutated\n"
         + b"".join((f"layout-size-callback {stage} width={dx} height={dy}\n".encode() if "parser" in stage else b"") + f"layout-size-stage {stage} source={sx},{sy} destination={dx},{dy}\n".encode() for stage,sx,sy,dx,dy in (
             ("layout-size-supplied",320,240,800,600),("layout-size-copy-retained",320,240,800,600),
             ("layout-size-source-independent",352,288,800,600),("layout-size-empty-retained",352,288,800,600),
             ("layout-size-self-copy-retained",352,288,800,600),("layout-size-parser-x",352,288,640,600),
             ("layout-size-parser-y",352,288,640,360),("layout-size-parser-repeated",352,288,720,600),
             ("layout-size-parser-nonpositive",352,288,-2,0)))
         + b"".join((b"conversion-callback " + row + b"\n" if row.split(b" ",1)[0] in (b"conversion-frame-half",b"conversion-time-half",b"conversion-empty-frame",b"conversion-empty-time") else b"") + b"conversion-stage " + row + b"\n" for row in conversion_rows())
         + b"unknown-open-callback mode=0 entries=1 segments=1 style 11 0055 006e 006b 006e 006f 0077 006e 004f 0070 0065 006e\n"
         + b"\n".join(unknown_rows()) + b"\n"
         + b"add-style-stage add-style-equal-before\n"
         + b"add-style-retained ZeroNeg bits=8000000000000000 font 4 004e 0000 d801 005a\n"
         + b"add-style-retained ZeroPos bits=0000000000000000 font 4 004e 0000 d802 005a\n"
         + b"add-style-stage add-style-equal-after\nadd-style-stage add-style-name-classes\n"
         + b"add-style-name-classes count=2 default=1 empty=1 default-size=21 empty-size=22 entry 2 0000 004b\n"
         + b"saved-file ssa exists=1 bytes=1 41\nsaved-file ass exists=1 bytes=1 41\n"
         + b"saved-file srt exists=1 bytes=1 41\nsaved-file srt-sidecar exists=1 bytes=1 41\n"
         + b"saved-file ssa-sidecar exists=0\nsaved-file ass-sidecar exists=0\n"
         + b"saved-file srt-no-map exists=1 bytes=1 41\nsaved-file srt-no-map-sidecar exists=0\n"
         + b"document-native-lifecycle-v1 complete\n")


class NativeLifecycleEvidenceTests(unittest.TestCase):
    def test_add_style_retains_signedzero_fullfont_and_name_classes(self):
        for old,new in ((b"bits=8000000000000000",b"bits=0000000000000000"),
                        (b"font 4 004e 0000 d801 005a",b"font 4 004e 0000 dcff 005a"),
                        (b"default=1 empty=1",b"default=1 empty=0")):
            changed=TRACE.replace(old,new)
            with self.assertRaises(ValueError):compare_traces([changed]*4,64)
        legacy=b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith((b"add-style-stage ",b"add-style-retained ",b"add-style-name-classes ")))
        with self.assertRaises(ValueError):compare_traces([legacy]*4,64)
        compare_traces([legacy]*4,64,require_add_style_stages=False)

    def test_unknown_rows_and_prefallback_callback_are_required(self):
        for prefix in (b"unknown-row unknown-after 0 ",b"unknown-open-callback "):
            truncated=b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith(prefix))
            with self.assertRaises(ValueError):compare_traces([truncated]*4,64)
        changed=TRACE.replace(b"unknown-row unknown-after 3 5 004b 006e 006f 0077 006e",b"unknown-row unknown-after 3 7 0044 0065 0066 0061 0075 006c 0074")
        with self.assertRaises(ValueError):compare_traces([changed]*4,64)
        legacy=b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith((b"unknown-stage ",b"unknown-row ",b"unknown-open-callback ")))
        with self.assertRaises(ValueError):compare_traces([legacy]*4,64)
        compare_traces([legacy]*4,64,require_unknown_stages=False)

    def test_dialogs_require_order_full_text_buttons_and_responses(self):
        def events():
            return [{"title":"Warning","class":"#32770","response":a["button"],"controls":[
                {"class":"Button","id":1,"text":"OK","style":0},
                {"class":"Button","id":2,"text":"Cancel","style":0},
                {"class":"Static","id":65535,"text":a["text_prefix"],"style":0}]} for a in lifecycle.unknown_dialog_actions()]
        lifecycle.validate_dialogs(events())
        for mutation in (lambda rows:rows.reverse(),lambda rows:rows.pop(),
                         lambda rows:rows[0].update(response="cancel"),
                         lambda rows:rows[0]["controls"].pop(),
                         lambda rows:rows[0]["controls"][1].update(id=3),
                         lambda rows:rows[0]["controls"][2].update(text=lifecycle.warning_text("UnknownA")+"suffix")):
            rows=events();mutation(rows)
            with self.assertRaises(ValueError):lifecycle.validate_dialogs(rows)

    def test_timed_out_dialog_child_is_killed_reaped_and_output_recorded(self):
        class Process:
            pid=42
            def __init__(self):self.killed=False;self.calls=0
            def communicate(self,timeout=None):
                self.calls+=1
                if timeout is not None:raise subprocess.TimeoutExpired("fixture",timeout)
                return b"partial trace",b""
            def kill(self):self.killed=True
            def poll(self):return -9 if self.killed else None
        class Capture:
            def __init__(self,actions,destination,process_id=None):self.process_id=process_id
            def __enter__(self):return self
            def __exit__(self,*args):pass
        process=Process()
        with TemporaryDirectory() as temp,patch.object(lifecycle.subprocess,"Popen",return_value=process),patch.object(lifecycle,"DialogCapture",Capture):
            stem=Path(temp)/"fixture"
            with self.assertRaisesRegex(RuntimeError,"timed out"):
                lifecycle.run_dialog_process(["fixture"],Path(temp),stem,20)
            self.assertTrue(process.killed);self.assertEqual(process.calls,2)
            self.assertEqual(Path(str(stem)+".stdout").read_bytes(),b"partial trace")

    def test_conversion_requires_complete_callbacks_and_exact_states(self):
        for marker in (b"conversion-stage conversion-time-half ",b"conversion-callback conversion-frame-half "):
            missing=b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith(marker))
            with self.assertRaises(ValueError): compare_traces([missing]*4,64)
        for before,after in [(b"times=-2,-1;1,2",b"times=-1,-1;1,2"),
                             (b"conversion-frame-noop-zero mode=1 entries=2 segments=2 animated=1",b"conversion-frame-noop-zero mode=1 entries=2 segments=2 animated=0")]:
            changed=TRACE.replace(before,after)
            with self.assertRaises(ValueError): compare_traces([changed]*4,64)
        callback=b"conversion-callback " + conversion_rows()[1] + b"\n"
        duplicate=TRACE.replace(callback,callback+callback)
        with self.assertRaises(ValueError): compare_traces([duplicate]*4,64)
        stage=b"conversion-stage " + conversion_rows()[1] + b"\n"
        late=TRACE.replace(callback,b"").replace(stage,stage+callback)
        with self.assertRaises(ValueError): compare_traces([late]*4,64)
        legacy=b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith((b"conversion-stage ",b"conversion-callback ")))
        with self.assertRaises(ValueError): compare_traces([legacy]*4,64)
        compare_traces([legacy]*4,64,require_conversion_stages=False)

    def test_requires_two_complete_equal_executions_per_side(self):
        compare_traces([TRACE] * 4, 64)
        with self.assertRaises(ValueError):
            compare_traces([TRACE] * 3, 64)
        with self.assertRaises(ValueError):
            compare_traces([TRACE, TRACE, TRACE, TRACE.rsplit(b"\n", 2)[0]], 64)

    def test_rejects_architecture_and_missing_observations(self):
        with self.assertRaises(ValueError):
            compare_traces([TRACE] * 4, 32)
        for section in (b"entry ", b"segment ", b"callback ", b"text ", b"state "):
            incomplete = b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith(section))
            with self.assertRaises(ValueError):
                compare_traces([incomplete] * 4, 64)

    def test_rejects_suffix_changes_after_embedded_nul(self):
        changed = TRACE.replace(b"0041,0000,0042", b"0041,0000,0043")
        with self.assertRaises(ValueError):
            compare_traces([TRACE, TRACE, changed, changed], 64)

    def test_rejects_missing_named_stage_even_when_both_sides_match(self):
        truncated = TRACE.replace(b"named-stage named-copy-append\n", b"")
        with self.assertRaises(ValueError):
            compare_traces([truncated] * 4, 64)

    def test_legacy_trace_requires_explicit_named_stage_opt_out(self):
        legacy = b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith((b"named-stage ", b"saved-file ")))
        with self.assertRaises(ValueError):
            compare_traces([legacy] * 4, 64)
        compare_traces([legacy] * 4, 64, require_named_stages=False)
        changed = legacy.replace(b"0041,0000,0042", b"0041,0000,0043")
        with self.assertRaises(ValueError):
            compare_traces([legacy, legacy, changed, changed], 64, require_named_stages=False)

    def test_rejects_missing_or_changed_saved_bytes(self):
        missing = TRACE.replace(b"saved-file srt-sidecar exists=1 bytes=1 41\n", b"")
        with self.assertRaises(ValueError):
            compare_traces([missing] * 4, 64)
        changed = TRACE.replace(b"saved-file ass exists=1 bytes=1 41", b"saved-file ass exists=1 bytes=1 42")
        with self.assertRaises(ValueError):
            compare_traces([TRACE, TRACE, changed, changed], 64)

    def test_rejects_changed_named_order_and_value(self):
        observed = TRACE.replace(b"named-stage named-initial\n", b"named-stage named-initial\nname Alpha\nstyle 1\nname Beta\nstyle 2\n")
        for changed in (observed.replace(b"name Alpha\nstyle 1\nname Beta\nstyle 2", b"name Beta\nstyle 2\nname Alpha\nstyle 1"),
                        observed.replace(b"style 2", b"style 3")):
            with self.assertRaises(ValueError):
                compare_traces([observed, observed, changed, changed], 64)

    def test_identity_stages_required_with_order_and_complete_values(self):
        removed = b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith(b"identity-stage identity-empty "))
        with self.assertRaises(ValueError): compare_traces([removed]*4,64)
        reordered = TRACE.replace(b"identity-stage identity-empty ", b"identity-stage temporary ").replace(b"identity-stage identity-self-copy ",b"identity-stage identity-empty ").replace(b"identity-stage temporary ",b"identity-stage identity-self-copy ")
        with self.assertRaises(ValueError): compare_traces([reordered]*4,64)
        changed = TRACE.replace(b"d800 0058",b"d800 005a")
        with self.assertRaises(ValueError): compare_traces([TRACE,TRACE,changed,changed],64)

    def test_archival_identity_opt_out_is_explicit(self):
        legacy=b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith(b"identity-stage "))
        with self.assertRaises(ValueError): compare_traces([legacy]*4,64)
        compare_traces([legacy]*4,64,require_identity_stages=False)

    def test_interpretation_stages_and_archival_opt_out(self):
        legacy=b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith(b"interpretation-stage "))
        with self.assertRaises(ValueError):compare_traces([legacy]*4,64)
        compare_traces([legacy]*4,64,require_interpretation_stages=False)
        changed=TRACE.replace(b"interpretation-stage interpretation-copy mode=0",b"interpretation-stage interpretation-copy mode=1")
        with self.assertRaises(ValueError):compare_traces([TRACE,TRACE,changed,changed],64)
        truncated=TRACE.replace(b"interpretation-stage interpretation-empty",b"missing-stage interpretation-empty")
        with self.assertRaises(ValueError):compare_traces([truncated]*4,64)

    def test_layout_stages_callbacks_and_archival_opt_out(self):
        legacy=b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith((b"layout-stage ",b"layout-callback ")))
        with self.assertRaises(ValueError):compare_traces([legacy]*4,64)
        compare_traces([legacy]*4,64,require_layout_stages=False)
        missing=TRACE.replace(b"layout-callback layout-open-fallback",b"missing-callback layout-open-fallback")
        with self.assertRaises(ValueError):compare_traces([missing]*4,64)
        changed=TRACE.replace(b"layout-callback layout-open-fallback width=0",b"layout-callback layout-open-fallback width=384")
        with self.assertRaises(ValueError):compare_traces([TRACE,TRACE,changed,changed],64)
        changed=TRACE.replace(b"layout-stage layout-self-copy width=0",b"layout-stage layout-self-copy width=32")
        with self.assertRaises(ValueError):compare_traces([TRACE,TRACE,changed,changed],64)

    def test_initialized_metadata_gate_and_archival_opt_out(self):
        legacy=b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith((b"metadata-stage ",b"metadata-callback ")))
        with self.assertRaises(ValueError):compare_traces([legacy]*4,64)
        compare_traces([legacy]*4,64,require_metadata_stages=False)
        missing=TRACE.replace(b"metadata-callback metadata-fallback-srt",b"missing-callback metadata-fallback-srt")
        with self.assertRaises(ValueError):compare_traces([missing]*4,64)
        invalid=TRACE.replace(b"par-bits 3ff0000000000000",b"par-bits 3ff000000000000")
        with self.assertRaises(ValueError):compare_traces([invalid]*4,64)
        changed=TRACE.replace(b"metadata-stage metadata-copy-forced-only forced=0",b"metadata-stage metadata-copy-forced-only forced=1")
        with self.assertRaises(ValueError):compare_traces([TRACE,TRACE,changed,changed],64)
        changed=TRACE.replace(b"metadata-callback metadata-color-pc709 forced=0 par-type=0 par-bits 3ff0000000000000 matrix=3",b"metadata-callback metadata-color-pc709 forced=0 par-type=0 par-bits 3ff0000000000000 matrix=1")
        with self.assertRaises(ValueError):compare_traces([TRACE,TRACE,changed,changed],64)

    def test_selected_copy_stages_order_and_archival_opt_out(self):
        legacy=b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith(b"copy-state-stage "))
        with self.assertRaises(ValueError):compare_traces([legacy]*4,64)
        compare_traces([legacy]*4,64,require_selected_copy_stages=False)
        missing=TRACE.replace(b"copy-state-stage copy-source-mutated\n",b"")
        with self.assertRaises(ValueError):compare_traces([missing]*4,64)
        reordered=TRACE.replace(b"copy-state-stage copy-source-mutated",b"copy-state-stage temporary").replace(b"copy-state-stage copy-destination-after",b"copy-state-stage copy-source-mutated").replace(b"copy-state-stage temporary",b"copy-state-stage copy-destination-after")
        with self.assertRaises(ValueError):compare_traces([reordered]*4,64)
        changed=TRACE.replace(b"copy-state-stage copy-empty-key-result\n",b"copy-state-stage copy-empty-key-result\nname Default2\n")
        with self.assertRaises(ValueError):compare_traces([TRACE,TRACE,changed,changed],64)

    def test_style_rename_stages_and_segment_invariants(self):
        legacy = b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith(b"style-rename-"))
        with self.assertRaises(ValueError): compare_traces([legacy]*4,64)
        compare_traces([legacy]*4,64,require_style_rename_stages=False)
        for token in (b"style-rename-stage shrink-after\n", b"style-rename-segments raw-nul-to2 count=2 animated=1\n"):
            with self.assertRaises(ValueError): compare_traces([TRACE.replace(token,b"")]*4,64)
        for replacement in (b"count=0 animated=1", b"count=2 animated=0", b"count=2 animated=3"):
            with self.assertRaises(ValueError): compare_traces([TRACE.replace(b"count=2 animated=1",replacement)]*4,64)
        changed=TRACE.replace(b"style-rename-stage shrink-after\n",b"style-rename-stage shrink-after\nstyle value changed\n")
        with self.assertRaises(ValueError): compare_traces([TRACE,TRACE,changed,changed],64)

    def test_sort_gate_callbacks_and_archival_optout(self):
        legacy=b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith((b"sort-stage ",b"sort-callback ")))
        with self.assertRaises(ValueError): compare_traces([legacy]*4,64)
        compare_traces([legacy]*4,64,require_sort_stages=False)
        for old,new in ((b"sort-callback sort-time entries=12",b"missing sort-time entries=12"),
                        (b"segments=4 0 1",b"segments=4 1 1"),
                        (b"sort-stage sort-time-repeat\n",b"")):
            with self.assertRaises(ValueError): compare_traces([TRACE.replace(old,new)]*4,64)
        changed=TRACE.replace(b"segments=4 0 1",b"segments=4 1 0")
        with self.assertRaises(ValueError): compare_traces([TRACE,TRACE,changed,changed],64)

    def test_base_empty_stages_and_archival_optout(self):
        legacy=b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith(b"empty-stage "))
        with self.assertRaises(ValueError): compare_traces([legacy]*4,64)
        compare_traces([legacy]*4,64,require_base_empty_stages=False)
        missing=TRACE.replace(b"empty-stage empty-reused\n",b"")
        with self.assertRaises(ValueError): compare_traces([missing]*4,64)
        reordered=TRACE.replace(b"empty-stage empty-cleared",b"empty-stage temporary").replace(b"empty-stage empty-repeated",b"empty-stage empty-cleared").replace(b"empty-stage temporary",b"empty-stage empty-repeated")
        with self.assertRaises(ValueError): compare_traces([reordered]*4,64)
        changed=TRACE.replace(b"empty-stage empty-final\n",b"empty-stage empty-final\nretained metadata changed\n")
        with self.assertRaises(ValueError): compare_traces([TRACE,TRACE,changed,changed],64)

    def test_lcid_retention_and_archival_optout(self):
        legacy=b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith(b"lcid-stage "))
        with self.assertRaises(ValueError): compare_traces([legacy]*4,64)
        compare_traces([legacy]*4,64,require_lcid_stages=False)
        for old,new in ((b"lcid-stage lcid-copy-retained source=1031 destination=0",b"lcid-stage lcid-copy-retained source=1031 destination=1031"),
                        (b"lcid-stage lcid-repeated-empty source=1033 destination=0\n",b"")):
            with self.assertRaises(ValueError): compare_traces([TRACE.replace(old,new)]*4,64)

    def test_resolution_values_and_archival_optout(self):
        legacy=b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith((b"resolution-",b"resolved-style resolution-")))
        with self.assertRaises(ValueError): compare_traces([legacy]*4,64)
        compare_traces([legacy]*4,64,require_resolution_stages=False)
        for token in (b"resolution-stage resolution-empty-prefix\n",b"resolved-style resolution-raw-prefix charset=0 " + STYLE_VALUE + b"\n",b"resolution-mutated-output style 0 raw-font-mutated\n"):
            with self.assertRaises(ValueError): compare_traces([TRACE.replace(token,b"")]*4,64)
        truncated=TRACE.replace(STYLE_VALUE, b"style 0 1 0041 0000000000000000")
        with self.assertRaises(ValueError): compare_traces([truncated]*4,64)
        changed=TRACE.replace(b"raw-font-mutated",b"raw-font-corrupted")
        with self.assertRaises(ValueError): compare_traces([TRACE,TRACE,changed,changed],64)

    def test_layout_size_retention_callbacks_and_archival_optout(self):
        legacy=b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith(b"layout-size-"))
        with self.assertRaises(ValueError): compare_traces([legacy]*4,64)
        compare_traces([legacy]*4,64,require_layout_size_stages=False)
        for old,new in ((b"destination=640,600",b"destination=640,0"),
                        (b"layout-size-callback layout-size-parser-y width=640 height=360\n",b"")):
            with self.assertRaises(ValueError): compare_traces([TRACE.replace(old,new)]*4,64)

    def test_rejects_repeat_nondeterminism(self):
        changed = TRACE.replace(b"callback changed", b"callback empty")
        with self.assertRaises(ValueError):
            compare_traces([TRACE, changed, TRACE, TRACE], 64)


if __name__ == "__main__":
    unittest.main()
