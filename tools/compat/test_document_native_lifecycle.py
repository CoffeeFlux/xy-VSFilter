import unittest

from document_native_lifecycle import compare_traces


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
         + b"saved-file ssa exists=1 bytes=1 41\nsaved-file ass exists=1 bytes=1 41\n"
         + b"saved-file srt exists=1 bytes=1 41\nsaved-file srt-sidecar exists=1 bytes=1 41\n"
         + b"saved-file ssa-sidecar exists=0\nsaved-file ass-sidecar exists=0\n"
         + b"saved-file srt-no-map exists=1 bytes=1 41\nsaved-file srt-no-map-sidecar exists=0\n"
         + b"document-native-lifecycle-v1 complete\n")


class NativeLifecycleEvidenceTests(unittest.TestCase):
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

    def test_rejects_repeat_nondeterminism(self):
        changed = TRACE.replace(b"callback changed", b"callback empty")
        with self.assertRaises(ValueError):
            compare_traces([TRACE, changed, TRACE, TRACE], 64)


if __name__ == "__main__":
    unittest.main()
