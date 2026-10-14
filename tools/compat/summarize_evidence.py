#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify downloaded CI artifacts and summarize a migration increment.

Download artifacts with `gh run download RUN --dir artifacts/ci-RUN`.
A native selector RUN/ARCH chooses one successful architecture job from a
partially failed run. Miri/job status must be checked separately in GitHub.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
TARGETS = {"aarch64-apple-darwin", "x86_64-unknown-linux-gnu",
           "i686-pc-windows-msvc", "x86_64-pc-windows-msvc"}
COMPONENTS = ["result", "text_reader_result", "font_result", "lexer_result",
              "drawing_result", "drawing_tokens_result", "fields_result",
              "segments_result", "timeline_search_result", "segment_insert_result",
              "hdmv_result", "dvb_result", "dvb_field_result", "golomb_result", "fill_result", "fill_debug_result", "pixel_result", "mask_result", "bilinear_result", "be_blur_result", "be_filter_result", "weighted_filter_result", "transpose_result", "float_transpose_result", "byte_float_result", "gauss_integer_result", "vobsub_packet_result", "vobsub_pixels_result", "vobsub_rle_result", "vobsub_trim_result", "vobsub_scale_result", "vobsub_outline_result", "vobsub_segment_result", "vobsub_split_result", "vobsub_path_result", "subrip_result", "webvtt_result", "realtext_result", "realtext_attributes_result", "realtext_timecode_result", "realtext_tag_result", "realtext_text_result", "realtext_lower_result", "realtext_timeline_result", "realtext_format_result", "realtext_lists_result", "realtext_document_result", "cc_result", "find_char_result", "microdvd_result", "mpl2_result", "remove_tags_result", "case_replace_result", "sami_result", "encoding_result", "ass_dialogue_result", "ass_style_result", "ass_document_result", "microdvd_style_result", "sami_file_result", "subrip_cue_result", "pipe_line_result", "text_writer_result", "layout_rects_result", "text_runs_result", "wrap_width_result", "line_layout_result", "text_seek_result", "drawing_storage_result", "path_align_result", "span_merge_result", "arc_queue_result", "ellipse_arc_result", "ellipse_storage_result", "path_buffer_result", "edge_storage_result", "span_coverage_result"]
BITMAP_SOURCES = ["hdmv_source_sha256", "hdmv_golomb_source_sha256",
                  "hdmv_golomb_header_sha256", "hdmv_raster_source_sha256", "xy_filter_source_sha256", "vobsub_source_sha256", "realtext_source_sha256", "realtext_header_sha256", "cc_source_sha256", "cc_header_sha256"]


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def one(root, pattern):
    files = list(root.rglob(pattern))
    assert len(files) == 1, (root, pattern, files)
    return files[0]


def summarize_comparison(root, base, name, reference, mutation_count):
    path = base / name / "report.json"
    report = load(path)
    assert all(report.get(key) is True for key in
               ["passed", "complete", "controls_passed", "exports_equal", "environment_unchanged"])
    assert report["rendered_frames"] > 0
    assert all(case["verdict"]["kind"] == "equal" for case in report["cases"])
    assert report["reference_source"]["commit"] == reference
    if name == "mutation-results":
        assert len(report["cases"]) == mutation_count
    summary = {
        "report_sha256": sha(path),
        "case_count": len(report["cases"]),
        "rendered_frames": report["rendered_frames"],
        "changed_frames": report["changed_frames"],
        "case_verdicts": dict(Counter(case["verdict"]["kind"] for case in report["cases"])),
        "candidate_statuses": dict(Counter(case["candidate_status"] for case in report["cases"])),
        "distinct_frame_controls": report["distinct_frame_controls"],
        "manifest_sha256": report["manifest_sha256"],
    }
    dialogs_by_case, timeline_by_case = {}, {}
    for case in report["cases"]:
        captures = [load(base / name / case["directory"] / side / "result.json")
                    for side in ["reference-0", "reference-1", "candidate"]]
        dialogs = captures[-1].get("dialogs", [])
        assert all(capture.get("dialogs", []) == dialogs for capture in captures)
        if dialogs:
            dialogs_by_case[case["id"]] = dialogs
        if case["id"].startswith(("timeline-", "incremental-")):
            frames = captures[-1]["frames"]
            assert all(capture["frames"] == frames for capture in captures)
            assert any(frame["changed"] for frame in frames)
            assert all(frame["guards_intact"] for frame in frames)
            timeline_by_case[case["id"]] = {
                "frame_count": len(frames),
                "changed_frames": sum(frame["changed"] for frame in frames),
                "distinct_frame_hashes": len({frame["sha256"] for frame in frames}),
            }
    summary["dialog_observations"] = dialogs_by_case
    summary["timeline_observations"] = timeline_by_case
    for side, directory in [("reference", "baseline"), ("candidate", "candidate")]:
        assert sha(one(root / directory, "VSFilter.dll")) == report[side + "_dll"]["sha256"]
    return summary


def summarize_native(run, platform, candidate, reference):
    root = ROOT / f"artifacts/ci-{run}/renderer-evidence-{platform}"
    build_path = one(root, "build-info.json")
    base, build = build_path.parent, load(build_path)
    assert build["candidate_commit"] == candidate
    assert build["reference_commit"] == reference
    assert not build["candidate_status"]
    assert build["platform"] == platform
    entry = {
        "run_url": f"https://github.com/CoffeeFlux/xy-VSFilter/actions/runs/{run}",
        "build": build, "checks": {}, "dlls": {},
    }
    for name in ["smoke", "csri", "mutation-results"]:
        entry["checks"][name] = summarize_comparison(root, base, name, reference, build["mutation_count"])
    for name in ["VSFilter.dll", "XySubFilter.dll", "xy_sub_filter_consumer.dll"]:
        exports = base / (name + ".exports.json")
        dependencies = base / (name + ".dependencies.json")
        export_report, dependency_report = load(exports), load(dependencies)
        assert export_report["equal"] is True
        assert export_report["reference"] == export_report["candidate"]
        assert dependency_report["compatible_declared_requirements"] is True
        assert not dependency_report["added_imports"] and not dependency_report["changed_headers"]
        entry["dlls"][name] = {
            "exports_equal": True,
            "no_added_imports_or_changed_platform_headers": True,
            "exports_report_sha256": sha(exports),
            "dependencies_report_sha256": sha(dependencies),
            "baseline_sha256": sha(one(root / "baseline", name)),
            "candidate_sha256": sha(one(root / "candidate", name)),
        }
    log = (base / "candidate.log").read_text(errors="replace")
    assert "XY_VSFILTER_RUST_TEXT" in log and "xy_vsfilter_ffi.lib" in log
    entry["candidate_log_sha256"] = sha(base / "candidate.log")
    return entry


def summarize_portable(run, candidate, reference):
    reports = list((ROOT / f"artifacts/ci-{run}").rglob("report.json"))
    assert len(reports) == 4, "Expected all four platform reports"
    assert {load(path)["target"] for path in reports} == TARGETS, "Missing or duplicated platform"
    source_hashes, summaries = {}, []
    for path in sorted(reports):
        report = load(path)
        assert report["complete"] is True
        assert report["candidate_commit"] == candidate and report["reference"]["commit"] == reference
        for name, digest in report["candidate_inputs"].items():
            if name not in source_hashes:
                raw = subprocess.run(["git", "show", f"{candidate}:{name}"], cwd=ROOT,
                                     check=True, capture_output=True).stdout
                lf = raw.replace(b"\r\n", b"\n")
                source_hashes[name] = {hashlib.sha256(data).hexdigest()
                                      for data in (raw, lf, lf.replace(b"\n", b"\r\n"))}
            assert digest in source_hashes[name], f"Input differs from candidate source: {name}"
        summaries.append({
            "target": report["target"], "report_sha256": sha(path),
            "sanitizers": report["sanitizers"],
            **{key: report[key].strip() for key in COMPONENTS if key in report},
            "candidate_inputs": report["candidate_inputs"],
            **{key: report[key] for key in BITMAP_SOURCES if key in report},
        })
    return summaries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", help="Full candidate commit SHA")
    parser.add_argument("portable", help="Portable workflow run ID")
    parser.add_argument("output", type=Path, help="New summary path, relative to the repository")
    parser.add_argument("native", nargs="+", help="Native run ID or RUN/Win32 or RUN/x64")
    args = parser.parse_args()
    if sys.flags.optimize:
        parser.error("Run without -O: evidence validation assertions must remain enabled")
    assert len(args.candidate) == 40 and all(c in "0123456789abcdef" for c in args.candidate)
    assert args.portable.isdecimal(), "Expected numeric portable run ID"
    output = ROOT / args.output
    assert not output.exists(), "Choose a new output file; existing evidence is never overwritten"
    reference = load(ROOT / "tools/compat/reference.json")["commit"]
    result = {
        "schema": 1, "reference_commit": reference, "candidate_commit": args.candidate,
        "native_runs": [],
        "portable_run_url": f"https://github.com/CoffeeFlux/xy-VSFilter/actions/runs/{args.portable}",
        "portable": [],
        "limits": [
            "Finite fixtures and mutation campaigns do not establish universal compatibility or memory safety.",
            "CSRI BGRX rendering and format queries; real Aegisub/player and other host integration remains outstanding.",
            "Comparisons pair reference and candidate on the same architecture and environment, not across architectures.",
            "Most native parsing, font handling, rasterization, compositing and third-party code remains C++.",
            "Drawing preprocessing, numeric conversion, scaling and final path storage remain native.",
            "Native sanitizers cover extracted C++ tests, not the full renderer or Rust static library.",
            "Native out-of-memory recovery, Windows 7 and older CPUs remain unverified.",
            "WebVTT end-of-list insertion has candidate-only validation and is excluded from reference-parity counts.",
        ],
    }
    for selector in args.native:
        run, _, platform = selector.partition("/")
        assert run.isdecimal() and platform in ("", "Win32", "x64"), "Invalid native run selector"
        for selected in ([platform] if platform else ["Win32", "x64"]):
            result["native_runs"].append(summarize_native(run, selected, args.candidate, reference))
    result["portable"] = summarize_portable(args.portable, args.candidate, reference)
    with output.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(result, stream, indent=2)
        stream.write("\n")
    print("Wrote", args.output)


if __name__ == "__main__":
    main()
