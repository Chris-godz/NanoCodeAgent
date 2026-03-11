#!/usr/bin/env python3
"""doc_inventory.py — Index the current documentation and assess staleness risk.

Purpose:
    Scan the docs/ and book/ directories, list all documentation files, extract
    their top-level headings, and flag potential staleness based on last-modified
    timestamps relative to related source files.

Usage:
    python3 scripts/docgen/doc_inventory.py [--root <repo-root>] [--output <path>]

Arguments:
    --root    Repository root directory (default: cwd).
    --output  Output file path (default: stdout).

Exit codes:
    0 — success
    1 — invalid arguments or I/O error

Output format:
    Markdown table with columns: Path, Title, Last Modified, Staleness Risk.

Staleness heuristic:
    Compares each doc's mtime against the newest source file mtime under src/ and
    include/. Thresholds: <=7 days behind → low, <=30 days → medium, >30 days → high.
    Docs under docs/templates/ and docs/generated/ are always 'low' (infrastructure).
    If no source files exist, risk is 'unknown'.
"""

import argparse
import os
import re
import sys
from datetime import datetime, timezone


def extract_title(filepath: str) -> str:
    """Extract the first H1 heading from a Markdown file."""
    try:
        with open(filepath, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line.startswith("# "):
                    return line[2:].strip()
    except OSError:
        pass
    return "(no title)"


def newest_source_mtime(root: str) -> float | None:
    """Find the newest modification time among source files."""
    source_dirs = ["src", "include"]
    source_exts = {".cpp", ".hpp", ".h", ".cxx", ".c"}
    newest: float | None = None

    for sdir in source_dirs:
        full = os.path.join(root, sdir)
        if not os.path.isdir(full):
            continue
        for dirpath, _, filenames in os.walk(full):
            for fname in filenames:
                if os.path.splitext(fname)[1] in source_exts:
                    mt = os.path.getmtime(os.path.join(dirpath, fname))
                    if newest is None or mt > newest:
                        newest = mt

    return newest


def assess_staleness(doc_mtime: float, source_mtime: float | None, rel_path: str) -> str:
    """Determine staleness risk for a documentation file."""
    # Infrastructure docs are always low risk
    infra_prefixes = ("docs/templates/", "docs/generated/")
    for prefix in infra_prefixes:
        if rel_path.startswith(prefix):
            return "low"

    if source_mtime is None:
        return "unknown"

    delta_days = (source_mtime - doc_mtime) / 86400.0

    if delta_days <= 7:
        return "low"
    elif delta_days <= 30:
        return "medium"
    else:
        return "high"


def scan_docs(root: str, source_mtime: float | None) -> list[dict]:
    """Find all .md files under docs/ and book/src/."""
    doc_dirs = ["docs", os.path.join("book", "src")]
    entries: list[dict] = []

    for doc_dir in doc_dirs:
        full = os.path.join(root, doc_dir)
        if not os.path.isdir(full):
            continue
        for dirpath, _, filenames in os.walk(full):
            for fname in sorted(filenames):
                if not fname.endswith(".md"):
                    continue
                fpath = os.path.join(dirpath, fname)
                rel = os.path.relpath(fpath, root)
                mtime = os.path.getmtime(fpath)
                mtime_str = datetime.fromtimestamp(mtime, tz=timezone.utc).strftime("%Y-%m-%d")
                title = extract_title(fpath)
                risk = assess_staleness(mtime, source_mtime, rel)
                entries.append({
                    "path": rel,
                    "title": title,
                    "modified": mtime_str,
                    "risk": risk,
                })

    return entries


def main() -> None:
    parser = argparse.ArgumentParser(description="Index documentation files.")
    parser.add_argument("--root", default=".", help="Repository root directory.")
    parser.add_argument("--output", default=None, help="Output file path.")
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    source_mt = newest_source_mtime(root)
    entries = scan_docs(root, source_mt)

    lines = ["# Documentation Inventory\n"]

    if source_mt is not None:
        ref_date = datetime.fromtimestamp(source_mt, tz=timezone.utc).strftime("%Y-%m-%d")
        lines.append(f"Source reference date (newest src/include file): {ref_date}\n")
    else:
        lines.append("Source reference date: N/A (no source files found)\n")

    lines.append("Staleness heuristic: low (<=7 days behind source), medium (<=30 days), high (>30 days)\n")
    lines.append("| Path | Title | Last Modified | Staleness Risk |")
    lines.append("|------|-------|---------------|----------------|")

    for e in entries:
        lines.append(f"| `{e['path']}` | {e['title']} | {e['modified']} | {e['risk']} |")

    lines.append("")
    output = "\n".join(lines) + "\n"

    if args.output:
        os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
        with open(args.output, "w", encoding="utf-8") as fh:
            fh.write(output)
        print(f"Wrote doc inventory to {args.output}")
    else:
        print(output)


if __name__ == "__main__":
    main()
