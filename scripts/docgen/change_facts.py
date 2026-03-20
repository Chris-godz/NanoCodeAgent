#!/usr/bin/env python3
"""change_facts.py — Extract objective change facts for doc scope decision.

Purpose:
    Given git diff (or --files list), output structured facts only: changed files
    with change type (add/modify/delete/rename), public surface signals, and
    large-refactor signals. Does NOT discover target docs (no adjacent/candidate).
    Target docs are fixed: README.md and book/src/**/*.md only.

Usage:
    python3 scripts/docgen/change_facts.py [--root <repo-root>] [--ref <git-ref>] [--output <path>]
    python3 scripts/docgen/change_facts.py [--root <repo-root>] --files <path> [--output <path>]

Output:
    JSON to --output or stdout: change_facts.json schema.
"""

import argparse
import json
import os
import re
import subprocess
import sys


def get_changed_files_with_status(root: str, ref: str) -> list[dict]:
    """Get changed files with status via git diff --name-status. Returns list of {path, change_type, rename_to?}."""
    try:
        result = subprocess.run(
            ["git", "diff", "--name-status", ref],
            cwd=root,
            capture_output=True,
            text=True,
            check=True,
        )
    except subprocess.CalledProcessError as e:
        print(f"ERROR: git diff failed: {e.stderr}", file=sys.stderr)
        sys.exit(1)
    entries: list[dict] = []
    for line in result.stdout.strip().splitlines():
        line = line.strip()
        if not line:
            continue
        # A=added, M=modified, D=deleted, R=renamed (R old new), C=copied
        parts = line.split("\t")
        if len(parts) < 2:
            continue
        status, path = parts[0], parts[1].replace("\\", "/")
        if status == "R" and len(parts) >= 3:
            rename_to = parts[2].replace("\\", "/")
            entries.append({"path": rename_to, "change_type": "rename", "rename_from": path})
        elif status == "A":
            entries.append({"path": path, "change_type": "add"})
        elif status == "D":
            entries.append({"path": path, "change_type": "delete"})
        elif status in ("M", "C"):
            entries.append({"path": path, "change_type": "modify"})
        else:
            entries.append({"path": path, "change_type": "modify"})
    return entries


def read_files_list(root: str, path: str) -> list[str]:
    """Read repo-relative file paths from a file (one per line)."""
    full = os.path.join(root, path) if not os.path.isabs(path) else path
    if not os.path.isfile(full):
        return []
    with open(full, "r", encoding="utf-8") as f:
        return [line.strip() for line in f if line.strip()]


def public_surface_signals(changed_entries: list[dict]) -> list[str]:
    """Heuristic: paths or tags that suggest public surface impact (CLI, config, README, entry)."""
    signals: list[str] = []
    seen: set[str] = set()
    for e in changed_entries:
        path = e.get("path", "").replace("\\", "/")
        if path in seen:
            continue
        if path == "README.md":
            if "README.md" not in seen:
                signals.append("README.md")
                seen.add("README.md")
        elif path.startswith("book/src/"):
            if "book/src/" not in seen:
                signals.append("book/src/")
                seen.add("book/src/")
        elif "main" in path and (path.startswith("src/") or path.startswith("include/")):
            if "main_entry" not in seen:
                signals.append("main_entry")
                seen.add("main_entry")
        elif "include/" in path and "cli" in path.lower():
            if "include/cli" not in seen:
                signals.append("include/cli")
                seen.add("include/cli")
        elif path.startswith("scripts/") and ("docgen" in path or "cli" in path.lower()):
            if "scripts/" not in seen:
                signals.append("scripts/")
                seen.add("scripts/")
    return signals


def large_refactor_signals(changed_entries: list[dict]) -> str | None:
    """Heuristic: many renames or directory moves suggest large refactor."""
    renames = sum(1 for e in changed_entries if e.get("change_type") == "rename")
    deletes = sum(1 for e in changed_entries if e.get("change_type") == "delete")
    if renames >= 3:
        return "many_renames"
    if deletes >= 5:
        return "many_deletes"
    return None


def collect_facts(root: str, ref: str | None, files_path: str | None) -> tuple[dict, str]:
    """Collect change facts. Returns (facts_dict, diff_ref_label)."""
    if files_path:
        paths = read_files_list(root, files_path)
        changed_entries = [{"path": p.replace("\\", "/"), "change_type": "modify"} for p in paths]
        diff_ref = f"(from file {files_path})"
    else:
        ref = ref or "HEAD~1"
        changed_entries = get_changed_files_with_status(root, ref)
        diff_ref = ref

    facts = {
        "diff_ref": diff_ref,
        "changed_files": changed_entries,
        "public_surface_signals": public_surface_signals(changed_entries),
        "large_refactor_signals": large_refactor_signals(changed_entries),
    }
    return facts, diff_ref


def main() -> None:
    parser = argparse.ArgumentParser(description="Extract change facts (JSON) for doc scope decision.")
    parser.add_argument("--root", default=".", help="Repository root directory.")
    parser.add_argument("--ref", default=None, help="Git ref to diff against (default: HEAD~1). Ignored if --files set.")
    parser.add_argument("--files", default=None, metavar="PATH", help="File listing repo-relative paths (one per line).")
    parser.add_argument("--output", default=None, help="Output JSON path (default: stdout).")
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    facts, _ = collect_facts(root, args.ref, args.files)
    out = json.dumps(facts, indent=2, ensure_ascii=False) + "\n"

    if args.output:
        os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
        with open(args.output, "w", encoding="utf-8") as fh:
            fh.write(out)
        print(f"Wrote change facts to {args.output}")
    else:
        print(out, end="")


if __name__ == "__main__":
    main()
