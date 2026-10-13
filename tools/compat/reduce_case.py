#!/usr/bin/env python3
"""Reduce a reproducible renderer mismatch by deleting raw byte ranges.

The original fixture is immutable. Two reference workers must agree on EVERY
trial; a crash/hang/guard failure cannot substitute for a mismatch. This tool
does not reduce unresolved reference crashes. Results are locally minimal only
if the bounded reduction completes, and never prove semantic equivalence.
"""
import argparse
from pathlib import Path

from csri_compare import (ROOT, compare_captures, digest, environment_manifest,
                          read_json, run_worker, validate_case, write_json)


def mismatch_signature(left, repeat, right, left_dir, repeat_dir, right_dir):
    if compare_captures(left, repeat, left_dir, repeat_dir)["kind"] != "equal":
        return None
    verdict = compare_captures(left, right, left_dir, right_dir)
    if verdict["kind"] != "mismatch":
        return None
    # Preserve the statuses and broad failure category. Do not turn a pixel
    # mismatch into an open rejection, crash, or unrelated metadata mismatch.
    return [left["status"], right["status"], verdict["changed_fields"], bool(verdict["byte_differences"])]


def relative_path(path):
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def reduce(args):
    case = read_json(args.case)
    validate_case(case)
    original = (ROOT / case["script"]).read_bytes()
    if digest(original) != case["sha256"]:
        raise ValueError("original fixture hash mismatch")
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    reference, candidate = Path(args.reference).resolve(), Path(args.candidate).resolve()
    environment = environment_manifest()
    report = {"complete": False, "original_case": case, "original_bytes": len(original),
              "reference_sha256": digest(reference.read_bytes()), "candidate_sha256": digest(candidate.read_bytes()),
              "environment": environment, "trials": []}
    best = original
    best_trial = None

    def save_best(data):
        path = output / "minimized.ass"
        path.write_bytes(data)
        minimized = dict(case, script=relative_path(path), sha256=digest(data))
        write_json(output / "case.json", minimized)
        write_json(output / "cases.json", {"schema": 1, "cases": [minimized]})
        report["best_bytes"] = len(data)
        report["best_sha256"] = digest(data)
        write_json(output / "report.json", report)

    def trial(data):
        directory = output / f"trial-{len(report['trials']):05d}"
        directory.mkdir()
        script = directory / "input.ass"
        script.write_bytes(data)
        path = directory / "case.json"
        write_json(path, dict(case, script=relative_path(script), sha256=digest(data)))
        a_dir, b_dir, c_dir = (directory / name for name in ("reference-0", "reference-1", "candidate"))
        a = run_worker(reference, path, a_dir, args.timeout)
        b = run_worker(reference, path, b_dir, args.timeout)
        c = run_worker(candidate, path, c_dir, args.timeout)
        signature = mismatch_signature(a, b, c, a_dir, b_dir, c_dir)
        report["trials"].append({"directory": directory.name, "bytes": len(data), "sha256": digest(data),
                                 "signature": signature, "statuses": [a["status"], b["status"], c["status"]]})
        write_json(output / "report.json", report)
        print(f"{directory.name}: {len(data)} bytes, statuses {a['status']}/{b['status']}/{c['status']}", flush=True)
        return signature, directory

    def remove_frames(directory):
        if directory:
            for frame in directory.glob("*/frame-*.bin"):
                frame.unlink()

    save_best(best)
    wanted, best_trial = trial(best)
    if wanted is None:
        report["reason"] = "input is not a stable, resolved reference/candidate mismatch"
        write_json(output / "report.json", report)
        return 2
    report["signature"] = wanted
    granularity = 2
    exhausted = False
    while best:
        width = max(1, (len(best) + granularity - 1) // granularity)
        accepted = False
        for start in range(0, len(best), width):
            if len(report["trials"]) >= args.max_attempts:
                exhausted = True
                break
            proposal = best[:start] + best[start + width:]
            signature, directory = trial(proposal)
            if signature == wanted:
                # Keep the initial and current best frame evidence only.
                if best_trial.name != "trial-00000":
                    remove_frames(best_trial)
                best, best_trial = proposal, directory
                save_best(best)
                granularity = max(2, granularity - 1)
                accepted = True
                break
            remove_frames(directory)
        if exhausted:
            break
        if not accepted:
            if width == 1:
                break
            granularity = min(len(best), granularity * 2)
    report["complete"] = True
    report["budget_exhausted"] = exhausted
    report["best_trial"] = best_trial.name
    after = environment_manifest()
    report["environment_unchanged"] = after == environment
    if after != environment:
        report["environment_after"] = after
    save_best(best)
    return 0 if report["environment_unchanged"] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--case", required=True, help="one saved case.json, not a multi-case manifest")
    parser.add_argument("--output", required=True, help="new directory")
    parser.add_argument("--timeout", type=float, default=15)
    parser.add_argument("--max-attempts", type=int, default=100)
    args = parser.parse_args()
    if args.timeout <= 0 or args.max_attempts < 1:
        parser.error("positive timeout and max-attempts are required")
    return reduce(args)


if __name__ == "__main__":
    raise SystemExit(main())
