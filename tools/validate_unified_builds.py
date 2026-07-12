#!/usr/bin/env python3
"""Build unified targets and classify failures against upstream baselines.

The generated manifest records the untouched upstream companion environment
used to create every unified target. If a unified build fails, this tool builds
that source environment before deciding whether the overlay introduced the
failure. Results and logs are persisted so a long local matrix run can resume.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Callable


RESULT_PASS = "passed"
RESULT_UPSTREAM_FAILURE = "upstream_failure"
RESULT_UNIFIED_FAILURE = "unified_failure"
COMPLETED_RESULTS = {RESULT_PASS, RESULT_UPSTREAM_FAILURE}


def load_json(path: Path, default: object) -> object:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def save_results(
    path: Path, fingerprint: str, results: dict[str, dict[str, object]]
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps({"fingerprint": fingerprint, "targets": results}, indent=2)
        + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def run_build(
    project_dir: Path,
    environment: str,
    log_path: Path,
    config: Path | None,
    verbose: bool,
) -> int:
    command = ["pio", "run"]
    if config is not None:
        command.extend(["-c", str(config)])
    command.extend(["-e", environment])

    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as log:
        process = subprocess.Popen(
            command,
            cwd=project_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        assert process.stdout is not None
        for line in process.stdout:
            log.write(line)
            if verbose:
                print(line, end="")
        return process.wait()


BuildRunner = Callable[[Path, str, Path, Path | None, bool], int]


def validation_fingerprint(
    project_dir: Path, manifest_path: Path, config: Path
) -> str:
    digest = hashlib.sha256()
    for path in (manifest_path, config):
        digest.update(str(path.relative_to(project_dir)).encode())
        digest.update(path.read_bytes())

    try:
        digest.update(
            subprocess.check_output(
                ["git", "rev-parse", "HEAD"], cwd=project_dir
            )
        )
        digest.update(
            subprocess.check_output(
                ["git", "diff", "--binary", "HEAD"], cwd=project_dir
            )
        )
        untracked = subprocess.check_output(
            ["git", "ls-files", "--others", "--exclude-standard", "-z"],
            cwd=project_dir,
        ).split(b"\0")
        for raw_name in sorted(name for name in untracked if name):
            path = project_dir / os.fsdecode(raw_name)
            if path.is_file():
                digest.update(raw_name)
                digest.update(path.read_bytes())
    except (OSError, subprocess.CalledProcessError):
        # The generated files still provide a useful identity outside Git.
        pass
    return digest.hexdigest()


def validate_target(
    project_dir: Path,
    item: dict[str, object],
    config: Path,
    logs_dir: Path,
    verbose: bool,
    runner: BuildRunner = run_build,
) -> dict[str, object]:
    target = str(item["target"])
    source = str(item["source_environment"])
    started = time.monotonic()

    unified_log = logs_dir / f"{target}.log"
    print(f"BUILD {target}", flush=True)
    unified_status = runner(project_dir, target, unified_log, config, verbose)
    result: dict[str, object] = {
        "target": target,
        "source_environment": source,
        "unified_exit_code": unified_status,
        "unified_log": str(unified_log),
    }

    if unified_status == 0:
        result["status"] = RESULT_PASS
    else:
        baseline_log = logs_dir / f"{target}.upstream.log"
        print(f"BASELINE {source}", flush=True)
        baseline_status = runner(
            project_dir, source, baseline_log, None, verbose
        )
        result["baseline_exit_code"] = baseline_status
        result["baseline_log"] = str(baseline_log)
        result["status"] = (
            RESULT_UNIFIED_FAILURE
            if baseline_status == 0
            else RESULT_UPSTREAM_FAILURE
        )

    result["duration_seconds"] = round(time.monotonic() - started, 3)
    print(f"RESULT {target}: {result['status']}", flush=True)
    if result["status"] == RESULT_UPSTREAM_FAILURE and os.environ.get("GITHUB_ACTIONS"):
        print(
            f"::warning title=Upstream target skipped::{source} also fails "
            "without the unified overlay",
            flush=True,
        )
    return result


def select_targets(
    manifest: list[dict[str, object]], requested: list[str]
) -> list[dict[str, object]]:
    if not requested:
        return manifest
    by_name = {str(item["target"]): item for item in manifest}
    missing = [name for name in requested if name not in by_name]
    if missing:
        raise SystemExit("Unknown unified target(s): " + ", ".join(missing))
    return [by_name[name] for name in requested]


def append_github_output(status: str) -> None:
    output_path = os.environ.get("GITHUB_OUTPUT")
    if not output_path:
        raise SystemExit("--github-output requires the GITHUB_OUTPUT environment variable")
    with Path(output_path).open("a", encoding="utf-8") as output:
        output.write(
            f"skipped={'true' if status == RESULT_UPSTREAM_FAILURE else 'false'}\n"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", default=".pio/unified-targets.json")
    parser.add_argument("--config", default=".pio/unified-platformio.ini")
    parser.add_argument("--results", default=".pio/unified-validation-results.json")
    parser.add_argument("--logs-dir", default=".pio/unified-validation-logs")
    parser.add_argument("--target", action="append", default=[])
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--github-output", action="store_true")
    args = parser.parse_args()

    project_dir = Path.cwd().resolve()
    manifest_path = (project_dir / args.manifest).resolve()
    config = (project_dir / args.config).resolve()
    results_path = (project_dir / args.results).resolve()
    logs_dir = (project_dir / args.logs_dir).resolve()

    manifest_value = load_json(manifest_path, [])
    if not isinstance(manifest_value, list) or not manifest_value:
        raise SystemExit(f"Missing or empty unified manifest: {manifest_path}")
    manifest = manifest_value
    selected = select_targets(manifest, args.target)
    fingerprint = validation_fingerprint(project_dir, manifest_path, config)

    previous_value = load_json(results_path, {}) if args.resume else {}
    if (
        isinstance(previous_value, dict)
        and previous_value.get("fingerprint") == fingerprint
        and isinstance(previous_value.get("targets"), dict)
    ):
        results = previous_value["targets"]
    else:
        if args.resume and previous_value:
            print("Previous results do not match this worktree; starting fresh")
        results = {}

    for item in selected:
        target = str(item["target"])
        previous = results.get(target, {})
        if args.resume and previous.get("status") in COMPLETED_RESULTS:
            print(f"SKIP {target}: {previous['status']}", flush=True)
            continue
        results[target] = validate_target(
            project_dir, item, config, logs_dir, args.verbose
        )
        save_results(results_path, fingerprint, results)

    statuses = [str(results[str(item["target"])]["status"]) for item in selected]
    counts = {status: statuses.count(status) for status in sorted(set(statuses))}
    print("SUMMARY " + json.dumps(counts, sort_keys=True), flush=True)

    if args.github_output:
        if len(selected) != 1:
            raise SystemExit("--github-output requires exactly one --target")
        append_github_output(statuses[0])

    return 1 if RESULT_UNIFIED_FAILURE in statuses else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("Interrupted", file=sys.stderr)
        sys.exit(130)
