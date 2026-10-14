#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify downloaded CI artifacts and summarize a migration increment.

Download artifacts with `gh run download RUN --dir artifacts/ci-RUN`.
A native selector RUN/ARCH chooses one successful architecture job from a
partially failed run. Miri/job status must be checked separately in GitHub.
"""
import argparse
from collections import Counter
from functools import lru_cache
import hashlib
import json
import re
from pathlib import Path, PureWindowsPath
import posixpath
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
TARGETS = {"aarch64-apple-darwin", "x86_64-unknown-linux-gnu",
           "i686-pc-windows-msvc", "x86_64-pc-windows-msvc"}
COMPONENTS = ["result", "text_reader_result", "font_result", "lexer_result",
              "drawing_result", "drawing_tokens_result", "fields_result",
              "segments_result", "timeline_search_result", "segment_insert_result",
              "hdmv_result", "dvb_result", "dvb_field_result", "golomb_result", "fill_result", "fill_debug_result", "pixel_result", "mask_result", "bilinear_result", "be_blur_result", "be_filter_result", "weighted_filter_result", "transpose_result", "float_transpose_result", "byte_float_result", "gauss_integer_result", "vobsub_packet_result", "vobsub_pixels_result", "vobsub_rle_result", "vobsub_trim_result", "vobsub_scale_result", "vobsub_outline_result", "vobsub_segment_result", "vobsub_split_result", "vobsub_path_result", "subrip_result", "webvtt_result", "realtext_result", "realtext_attributes_result", "realtext_timecode_result", "realtext_tag_result", "realtext_text_result", "realtext_lower_result", "realtext_timeline_result", "realtext_format_result", "realtext_lists_result", "realtext_document_result", "cc_result", "find_char_result", "microdvd_result", "mpl2_result", "remove_tags_result", "case_replace_result", "sami_result", "encoding_result", "ass_dialogue_result", "ass_style_result", "ass_document_result", "microdvd_style_result", "sami_file_result", "subrip_cue_result", "pipe_line_result", "text_writer_result", "layout_rects_result", "text_runs_result", "wrap_width_result", "line_layout_result", "text_seek_result", "drawing_storage_result", "path_align_result", "span_merge_result", "arc_queue_result", "ellipse_arc_result", "ellipse_storage_result", "path_buffer_result", "edge_storage_result", "span_coverage_result", "variance_result", "blur_planes_result", "legacy_blur_result", "gauss_storage_result", "gauss_planes_result", "rle_storage_result", "clip_mask_result", "hdmv_storage_result", "clip_fade_result", "dvb_storage_result", "vobsub_storage_result", "fraction_storage_result", "bitmap_storage_result", "composite_storage_result", "coeff_storage_result", "cc_packet_result", "csri_filename_result", "ssf_stream_result", "ssf_output_result", "ssf_arabic_result", "ssf_memory_result", "ssf_wide_result", "ssf_bom_result", "ssf_split_result", "ssf_literal_result"]
BITMAP_SOURCES = ["hdmv_source_sha256", "hdmv_golomb_source_sha256",
                  "hdmv_golomb_header_sha256", "hdmv_raster_source_sha256", "xy_filter_source_sha256", "vobsub_source_sha256", "realtext_source_sha256", "realtext_header_sha256", "cc_source_sha256", "cc_header_sha256"]


def document_lifecycle_requirements(verified_probe_source):
    """Archive gates follow invocations in the hash-verified candidate probe."""
    calls = set(re.findall(rb"(?m)^\s*(named_style_lifecycle|identity_lifecycle)\(\);\s*$", verified_probe_source))
    return {"require_named_stages": b"named_style_lifecycle" in calls,
            "require_identity_stages": b"identity_lifecycle" in calls}


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def normalize_windows_source(name):
    """Resolve compiler include spelling, retaining a repository-relative path."""
    path = posixpath.normpath(name.replace("\\", "/"))
    if path == ".." or path.startswith(("../", "/")) or ":" in path:
        raise ValueError(f"Header path escapes checkout: {name}")
    return path.casefold()


@lru_cache(maxsize=None)
def source_paths(commit):
    raw = subprocess.run(["git", "ls-tree", "-rz", "--name-only", commit],
                         cwd=ROOT, check=True, capture_output=True).stdout
    paths = {}
    for name in raw.decode().split("\0"):
        if name:
            key = normalize_windows_source(name)
            assert key not in paths, "Ambiguous Windows source path"
            paths[key] = name
    return paths


def windows_source_bytes(commit, name):
    # /showIncludes may retain ../ and spell DSUtil for tracked dsutil. Windows
    # resolves those names identically; Git's case-sensitive object lookup does not.
    canonical = source_paths(commit)[normalize_windows_source(name)]
    return subprocess.run(["git", "show", f"{commit}:{canonical}"], cwd=ROOT,
                          check=True, capture_output=True).stdout


def one(root, pattern):
    files = list(root.rglob(pattern))
    assert len(files) == 1, (root, pattern, files)
    return files[0]


def production_dll(root, side, name):
    # Instrumented style-test DLLs live under candidate/artifacts as well.
    # Only the original build output is eligible for renderer/export evidence.
    files = list((root / side / "bin").glob(f"lib*/**/Release/{name}"))
    assert len(files) == 1, (root, side, name, files)
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
        assert sha(production_dll(root, directory, "VSFilter.dll")) == report[side + "_dll"]["sha256"]
    return summary


def summarize_native(run, platform, candidate, reference, document_storage="native"):
    assert document_storage in ("native", "rust-experimental")
    suffix = "-rust-document" if document_storage == "rust-experimental" else ""
    root = ROOT / f"artifacts/ci-{run}/renderer-evidence-{platform}{suffix}"
    build_path = one(root, "build-info.json")
    base, build = build_path.parent, load(build_path)
    assert build["candidate_commit"] == candidate
    assert build["reference_commit"] == reference
    assert not build["candidate_status"]
    assert build["platform"] == platform
    assert build.get("document_storage", "native") == document_storage, "Wrong document storage variant"
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
            "baseline_sha256": sha(production_dll(root, "baseline", name)),
            "candidate_sha256": sha(production_dll(root, "candidate", name)),
        }
    com_path = base / "com-contracts/report.json"
    # Older increments predate this contract gate. Once the driver's source is
    # present in a candidate commit, missing or failed evidence is an error.
    has_com_probe = subprocess.run(["git", "cat-file", "-e", f"{candidate}:tools/compat/com_contract_probe.cpp"],
                                   cwd=ROOT, capture_output=True).returncode == 0
    if has_com_probe:
        com = load(com_path)
        assert com["complete"] and com["passed"] and com["inputs_unchanged"]
        assert com["reference_commit"] == reference and com["candidate_commit"] == candidate
        assert com["platform"] == platform and len(com["cases"]) == 10
        assert com["probe_sha256"] == sha(base / "com-contract-probe.exe")
        for name, digest in com["harness_sources"].items():
            raw = subprocess.run(["git", "show", f"{candidate}:{name}"], cwd=ROOT,
                                 check=True, capture_output=True).stdout
            lf = raw.replace(b"\r\n", b"\n")
            assert digest in {hashlib.sha256(data).hexdigest() for data in (raw, lf, lf.replace(b"\n", b"\r\n"))}
        for name, hashes in com["binaries"].items():
            assert hashes["reference"] == entry["dlls"][name]["baseline_sha256"]
            assert hashes["candidate"] == entry["dlls"][name]["candidate_sha256"]
        for case in com["cases"]:
            assert case["verdict"]["kind"] == "equal"
            traces = []
            for side, capture in case["captures"].items():
                assert capture["status"] == "complete" and capture["returncode"] == 0
                path = com_path.parent / case["id"] / side / "stdout.txt"
                assert capture["stdout_sha256"] == sha(path)
                traces.append(path.read_bytes())
            assert len(traces) == 4 and all(trace == traces[0] for trace in traces)
        assert len(com["candidate_only_controls"]) == 2
        for control in com["candidate_only_controls"]:
            assert control["passed"] is True
            traces = []
            for side, capture in control["captures"].items():
                assert capture["status"] == "complete" and capture["returncode"] == 0
                path = com_path.parent / control["id"] / side / "stdout.txt"
                assert capture["stdout_sha256"] == sha(path)
                traces.append(path.read_bytes())
            assert len(traces) == 2 and traces[0] == traces[1]
            assert all(line in traces[0].splitlines() for line in (b"pin.0.id hr 0", b"pin.0.id utf16", b"pin.0.find hr 0", b"pin.0.find.identity value 1"))
        entry["checks"]["com-contracts"] = {"report_sha256": sha(com_path), "cases": len(com["cases"]),
                                                "passed": True, "scope": com["scope"], "headers": com["headers"],
                                                "reference_exclusions": com["reference_exclusions"], "candidate_only_controls": 2,
                                                "settings_environment": com["settings_environment"]}
    has_document_probe = subprocess.run(
        ["git", "cat-file", "-e", f"{candidate}:tools/compat/document_native_lifecycle.cpp"],
        cwd=ROOT, capture_output=True).returncode == 0
    if has_document_probe:
        from document_native_lifecycle import compare_traces
        directory = base / "document-lifecycle"
        path = directory / "report.json"
        document = load(path)
        for flag in ("complete", "equal", "repeat_equal", "stderr_empty", "inputs_unchanged"):
            assert document[flag] is True
        assert document["candidate_commit"] == candidate and document["reference_commit"] == reference
        assert document["platform"] == platform and document["document_storage"] == document_storage
        sources = [(candidate, "tools/compat/document_native_lifecycle.cpp", document["probe_sha256"]),
                   (candidate, "tools/compat/document_native_lifecycle.py", document["runner_sha256"])]
        traces = []
        for side, commit in (("reference", reference), ("candidate", candidate)):
            details = document["builds"][side]
            assert details["binary_sha256"] == sha(directory / side / "document-native-lifecycle.exe")
            assert details["source_header_sha256"], "Native source headers were not captured"
            sources.extend((commit, name, digest) for name, digest in details["source_header_sha256"].items())
            for repeat in (1, 2):
                traces.append((directory / f"{side}-trace-{repeat}.stdout").read_bytes())
                assert not (directory / f"{side}-trace-{repeat}.stderr").read_bytes()
        for commit, name, digest in sources:
            raw = windows_source_bytes(commit, name)
            lf = raw.replace(b"\r\n", b"\n")
            assert digest in {hashlib.sha256(data).hexdigest() for data in (raw, lf, lf.replace(b"\n", b"\r\n"))}
        candidate_probe = windows_source_bytes(candidate, "tools/compat/document_native_lifecycle.cpp")
        compare_traces(traces, 32 if platform == "Win32" else 64,
                       **document_lifecycle_requirements(candidate_probe))
        assert [hashlib.sha256(trace).hexdigest() for trace in traces] == document["trace_sha256"]
        entry["checks"]["document-lifecycle"] = {
            "report_sha256": sha(path), "equal": True, "trace_lines": document["trace_lines"],
            "limits": document["limits"],
            "library_hash_scope": "Build-library hashes are runner-reported; executables, source headers and traces rehashed",
        }
    has_style_probe = subprocess.run(
        ["git", "cat-file", "-e", f"{candidate}:tools/compat/document_style_api_check.py"],
        cwd=ROOT, capture_output=True).returncode == 0
    if has_style_probe and document_storage == "rust-experimental":
        entry["checks"]["document-style-api"] = summarize_style_api(
            base / "document-style-api", candidate, reference, platform, entry["dlls"])
    has_style_value_probe = subprocess.run(
        ["git", "cat-file", "-e", f"{candidate}:tools/compat/document_style_value_check.py"],
        cwd=ROOT, capture_output=True).returncode == 0
    if has_style_value_probe and document_storage == "rust-experimental":
        entry["checks"]["document-style-value"] = summarize_style_value(
            base / "document-style-value", candidate, reference, platform)
    log = (base / "candidate.log").read_text(errors="replace")
    assert "XY_VSFILTER_RUST_TEXT" in log and "xy_vsfilter_ffi.lib" in log
    if document_storage == "rust-experimental":
        assert "XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL" in log
    entry["candidate_log_sha256"] = sha(base / "candidate.log")
    return entry


def summarize_style_api(directory, candidate, reference, platform, production_dlls):
    from document_style_api_check import validate_trace
    path = directory / "report.json"
    report = load(path)
    assert report["schema"] == "document-style-api-v1" and report["status"] == "complete"
    assert report["candidate_commit"] == candidate and report["reference_commit"] == reference
    assert report["platform"] == platform and report["document_storage"] == "rust-experimental"
    artifacts = report["artifacts"]
    for key, name in (("helper", "document_style_api_probe.cpp"),
                      ("host_source", "document_style_api_host.cpp"),
                      ("runner", "document_style_api_check.py")):
        raw = windows_source_bytes(candidate, "tools/compat/" + name)
        lf = raw.replace(b"\r\n", b"\n")
        assert artifacts[key] in {hashlib.sha256(v).hexdigest() for v in (raw, lf, lf.replace(b"\n", b"\r\n"))}
    for key, name in (("host", "host.exe"), ("normal.ass", "normal.ass"), ("long-name.ass", "long-name.ass")):
        assert artifacts[key] == sha(directory / name)
    assert len(report["builds"]) == 4
    expected_builds = {(side, name) for side in ("reference", "candidate") for name in ("VSFilter.dll", "XySubFilter.dll")}
    assert {(b["side"], b["dll"]) for b in report["builds"]} == expected_builds
    for build in report["builds"]:
        side, name = build["side"], build["dll"]
        assert build["wrapper_sha256"] == sha(directory / PureWindowsPath(build["wrapper"]).name)
        command = build["command"]
        for flag in ("/p:RustTextDecoder=true", "/p:RustDocumentOwnerExperimental=true"):
            assert (flag in command) == (side == "candidate")
        binary = directory / side / name[:-4] / name
        recorded = [v for k, v in artifacts.items() if PureWindowsPath(k).parts[-3:] == (side, name[:-4], name)]
        assert recorded == [sha(binary)]
        source_hashes = {build["project"]: build["project_sha256"], **build["props"]}
        for source, digest in source_hashes.items():
            parts = PureWindowsPath(source).parts
            relative = "/".join(parts[parts.index("src"):])
            raw = windows_source_bytes(reference if side == "reference" else candidate, relative)
            lf = raw.replace(b"\r\n", b"\n")
            assert digest in {hashlib.sha256(v).hexdigest() for v in (raw, lf, lf.replace(b"\n", b"\r\n"))}
        original = [v for k, v in report["production_artifacts"][side].items() if PureWindowsPath(k).name == name]
        assert original == [production_dlls[name]["baseline_sha256" if side == "reference" else "candidate_sha256"]]
    runs = report["runs"]
    expected = {(side, cls, mode, repeat) for side in ("reference", "candidate")
                for cls in ("CDirectVobSubFilter", "XySubFilter")
                for mode in ("normal", "long-name") for repeat in range(2)}
    assert len(runs) == 16 and {(r["side"], r["class"], r["mode"], r["repeat"]) for r in runs} == expected
    groups = {}
    has_video_setup = b'status("setup.video_check",h)' in windows_source_bytes(
        candidate, "tools/compat/document_style_api_probe.cpp")
    for run in runs:
        stdout = directory / run["trace"]
        stderr = stdout.with_suffix(".stderr")
        assert sha(stdout) == run["stdout_sha256"] and sha(stderr) == run["stderr_sha256"]
        data = stdout.read_bytes()
        validate_trace(data, stderr.read_bytes(), run["returncode"], run["mode"], 32 if platform == "Win32" else 64,
                       video_setup=has_video_setup and run["class"] == "CDirectVobSubFilter")
        groups.setdefault((run["class"], run["mode"]), []).append(data)
    assert all(len(traces) == 4 and all(t == traces[0] for t in traces) for traces in groups.values())
    return {"report_sha256": sha(path), "equal": True, "class_mode_pairs": 4, "executions": 16,
            "scope": "Test-instrumented copies of actual DLLs; native style values remain in-module; production DLL hashes unchanged. Build-library hashes are runner-reported. No graph/real-host/callback-count coverage."}


def summarize_style_value(directory, candidate, reference, platform):
    from document_style_value_check import compare_traces
    path = directory / "report.json"
    report = load(path)
    assert report["schema"] == "document-style-value-v1" and report["status"] == "complete"
    assert all(report[key] is True for key in ("complete", "equal", "repeat_equal", "stderr_empty", "inputs_unchanged"))
    assert report["candidate_commit"] == candidate and report["reference_commit"] == reference
    assert report["platform"] == platform
    checked = set()
    for source, digest in report["artifacts"].items():
        parts = PureWindowsPath(source).parts
        if "document-style-value" in parts:
            relative = parts[parts.index("document-style-value") + 1:]
            assert sha(directory.joinpath(*relative)) == digest
        elif "candidate" in parts or "baseline" in parts:
            side = "candidate" if "candidate" in parts else "baseline"
            name = "/".join(parts[parts.index(side) + 1:])
            raw = windows_source_bytes(candidate if side == "candidate" else reference, name)
            lf = raw.replace(b"\r\n", b"\n")
            assert digest in {hashlib.sha256(v).hexdigest() for v in (raw, lf, lf.replace(b"\n", b"\r\n"))}
            checked.add((side, normalize_windows_source(name)))
    for name in ("rust/compat/src/document_style.rs", "rust/compat/examples/document_style_codec.rs",
                 "tools/compat/document_style_value_probe.cpp", "tools/compat/document_style_value_check.py"):
        assert ("candidate", name) in checked
    assert ("baseline", "src/subtitles/sts.h") in checked
    expected = {"native-0", "native-1", "rust-0", "rust-1"}
    executions = [item for item in report["executions"] if item["name"] in expected]
    assert len(executions) == 4 and {item["name"] for item in executions} == expected
    for item in executions:
        assert item["status"] == "complete" and item["returncode"] == 0
        assert not (directory / (item["name"] + ".stderr")).read_bytes()
        for suffix in ("stdout", "stderr"):
            assert item[suffix + "_sha256"] == sha(directory / (item["name"] + "." + suffix))
    native = [(directory / f"native-{i}.stdout").read_bytes() for i in range(2)]
    rust = [(directory / f"rust-{i}.stdout").read_bytes() for i in range(2)]
    count = compare_traces(native, rust, 32 if platform == "Win32" else 64)
    assert count == report["value_count"] and count * count == report["equality_cells"]
    return {"report_sha256": sha(path), "equal": True, "values": count, "equality_cells": count * count,
            "scope": report["scope"], "provenance_scope": "Retained sources, headers, executables, input and traces rehashed; external toolchain and native library hashes runner-reported."}


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
    parser.add_argument("--document-storage", choices=["native", "rust-experimental"], default="native",
                        help="Select separate native or experimental document-owner artifacts")
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
        "document_storage": args.document_storage,
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
            result["native_runs"].append(summarize_native(run, selected, args.candidate, reference, args.document_storage))
    result["portable"] = summarize_portable(args.portable, args.candidate, reference)
    with output.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(result, stream, indent=2)
        stream.write("\n")
    print("Wrote", args.output)


if __name__ == "__main__":
    main()
