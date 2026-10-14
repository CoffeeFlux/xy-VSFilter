#!/usr/bin/env python3
"""Compare separately linked native SSF parser/renderer probes in fresh processes."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HARNESS = ["tools/compat/ssf_native_probe.cpp", "tools/compat/ssf_native_compare.py",
           "tools/compat/run_ssf_native.ps1", ".github/workflows/ssf-native-compat.yml"]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def cases():
    def subtitle(text):
        return ('subtitle { time {start: 0; stop: 2;}; '
                'frame.resolution {cx: 320; cy: 180;}; '
                'style.font {face: "Arial"; size: 20;}; @ { ' + text + ' }; };')
    plain = subtitle("SSF compatibility")
    return {"plain": plain,
            "comments": "/* before */ // line\r\n" + plain.replace("time {", "time /* middle */ {") + "/* after */",
            "arabic": subtitle("\u0628\u0627\u0628 \u0644\u0627 \u0626\u0628"),
            "arabic-boundaries": subtitle("A \u0604\u0626\u0644\u06ff Z"),
            "mixed": subtitle("ABC \u0628\u0627\u0628 123"),
            "multiline": subtitle("first\\nsecond\\nthird")}


def inspect(directory, returncode, timed_out=False):
    trace = (directory / "stdout.txt").read_bytes()
    result = {"returncode": returncode, "stdout_sha256": sha(directory / "stdout.txt"),
              "stderr_sha256": sha(directory / "stderr.txt"), "files": {}, "status": "incomplete"}
    if timed_out:
        result["status"] = "timeout"
        return result
    if returncode != 0 or not trace.endswith(b"ssf-native-v1 complete\n"):
        return result
    frames = re.findall(rb"^pixels (\d+) changed (\d+) bounds (-?\d+) (-?\d+) (-?\d+) (-?\d+)$", trace, re.M)
    dumps = re.findall(rb"^dump (\d+) units (\d+)$", trace, re.M)
    segments = re.findall(rb"^ssf-native-v1 parsed segments (\d+)$", trace, re.M)
    headers = re.findall(rb"^frame (\d+) found [01] subtitles (\d+)$", trace, re.M)
    if [int(h[0]) for h in headers] != list(range(6)):
        return result
    if len(segments) != 1 or int(segments[0]) == 0 or [int(f[0]) for f in frames] != list(range(6)) or [int(d[0]) for d in dumps] != list(range(6)):
        return result
    if any(int(frame[1]) != 0 for frame in frames) or not re.search(rb"^text units [0-9a-f]{4}", trace, re.M) or re.search(rb"^rendered 1 glyphs [0-9a-f]{4}", trace, re.M):
        result["status"] = "control_failed"
        return result
    for index, count in dumps:
        name = f"dump-{int(index)}.units"
        path = directory / name
        if not path.is_file() or path.stat().st_size != int(count) * 4 or (int(index) >= 2 and int(count) == 0):
            return result
        result["files"][name] = sha(path)
    for frame in frames:
        name = f"frame-{int(frame[0])}.bgra"
        path = directory / name
        if not path.is_file() or path.stat().st_size != 320 * 180 * 4:
            return result
        pixels = path.read_bytes()
        changed = sum(pixels[i:i + 4] != b"\x00\x00\x00\xff" for i in range(0, len(pixels), 4))
        if changed != int(frame[1]):
            result["status"] = "control_failed"
            return result
        result["files"][name] = sha(path)
    result.update(status="complete", blank_frame_observations=6, visible_rendering_parity_frames=0, changed_pixels=[int(frame[1]) for frame in frames], segments=int(segments[0]))
    return result


def capture(probe, source, directory):
    directory.mkdir()
    timed_out = False
    with (directory / "stdout.txt").open("wb") as out, (directory / "stderr.txt").open("wb") as err:
        try:
            run = subprocess.run([str(probe), str(source), str(directory)], stdout=out, stderr=err, timeout=20)
            code = run.returncode
        except subprocess.TimeoutExpired:
            timed_out, code = True, None
    # Windows CRT stdout is text mode. Compare the canonical trace and preserve
    # the original capture as well; binary dump/frame files are never rewritten.
    raw = (directory / "stdout.txt").read_bytes()
    (directory / "stdout.raw").write_bytes(raw)
    (directory / "stdout.txt").write_bytes(raw.replace(b"\r\n", b"\n"))
    return inspect(directory, code, timed_out)


def equivalent(captures):
    return (len(captures) == 4 and all(c["status"] == "complete" for c in captures)
            and all(c["stdout_sha256"] == captures[0]["stdout_sha256"] and c["files"] == captures[0]["files"] for c in captures))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--baseline-root", type=Path, required=True)
    parser.add_argument("--platform", choices=["Win32", "x64"], required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    reference = json.loads((ROOT / "tools/compat/reference.json").read_text())["commit"]
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    baseline = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=args.baseline_root, text=True).strip()
    if baseline != reference:
        raise RuntimeError("Baseline does not match pinned reference")
    probes = {"reference": sha(args.reference), "candidate": sha(args.candidate)}
    harness = {name: sha(ROOT / name) for name in HARNESS}
    report = {"complete": False, "passed": False, "reference_commit": reference, "candidate_commit": commit,
              "platform": args.platform, "probes": probes, "harness_sources": harness, "cases": [],
              "scope": "Actual separately linked libssf parser, parsed text and node dump in six encodings. Native renderer traces and blank RGBA frames are characterized separately: the pinned FontCache::Create returns NULL after successful CreateFontIndirect. No visible rendering parity is established. WCHAR input with bounded valid SSF fixtures, each side repeated in fresh processes. DirectShow, host playback, byte-stream decoding and malformed-document coverage remain separate."}
    path = args.output / "report.json"
    path.write_text(json.dumps(report, indent=2) + "\n")
    for name, text in cases().items():
        root = args.output / name
        root.mkdir()
        source = root / "input.utf16"
        source.write_bytes(text.encode("utf-16-le"))
        entry = {"id": name, "input_sha256": sha(source), "captures": {}}
        report["cases"].append(entry)
        for side, probe in (("reference", args.reference), ("candidate", args.candidate)):
            for repeat in range(2):
                label = f"{side}-{repeat}"
                result = capture(probe, source, root / label)
                entry["captures"][label] = result
                if result["status"] != "complete":
                    path.write_text(json.dumps(report, indent=2) + "\n")
                    raise SystemExit(f"Incomplete {name}/{label}; retained evidence, no repeat or parity pass")
        entry["equal"] = equivalent(list(entry["captures"].values()))
        path.write_text(json.dumps(report, indent=2) + "\n")
        if not entry["equal"]:
            raise SystemExit(f"SSF mismatch: {name}; retained raw evidence")
    by_name = {c["id"]: c for c in report["cases"]}
    report["controls"] = {}
    for side in ("reference-0", "reference-1", "candidate-0", "candidate-1"):
        a = by_name["plain"]["captures"][side]["files"]
        b = by_name["comments"]["captures"][side]["files"]
        c = by_name["arabic"]["captures"][side]["files"]
        report["controls"][side] = a == b and a["dump-5.units"] != c["dump-5.units"]
    report["inputs_unchanged"] = (probes == {"reference": sha(args.reference), "candidate": sha(args.candidate)}
                                    and harness == {name: sha(ROOT / name) for name in HARNESS}
                                    and all(c["input_sha256"] == sha(args.output / c["id"] / "input.utf16") for c in report["cases"])
                                    and not subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)
                                    and not subprocess.check_output(["git", "status", "--porcelain"], cwd=args.baseline_root)
                                    and commit == subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip())
    report["complete"] = True
    report["passed"] = report["inputs_unchanged"] and all(report["controls"].values())
    path.write_text(json.dumps(report, indent=2) + "\n")
    if not report["passed"]:
        raise SystemExit("SSF controls or source stability failed")
    print(f"{len(report['cases'])} native SSF parser/dump cases matched; {len(report['cases']) * 6} blank-frame observations, zero visible rendering parity frames")


if __name__ == "__main__":
    main()
