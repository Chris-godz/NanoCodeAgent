#!/usr/bin/env python3
"""changed_context.py — Collect context around changed files for doc impact analysis.

Purpose:
    Given a list of changed files (from git diff), find each file's adjacent
    documentation, related examples, and module summary. This output feeds into
    the docgen-change-impact skill.

Usage:
    python3 scripts/docgen/changed_context.py [--root <repo-root>] [--ref <git-ref>] [--output <path>]

Arguments:
    --root    Repository root directory (default: cwd).
    --ref     Git ref to diff against (default: HEAD~1). Use 'main' to compare against default branch.
    --output  Output file path (default: stdout).

Exit codes:
    0 — success
    1 — invalid arguments or git error

Output format:
    Markdown with per-file context blocks:
    - File path
    - Module it belongs to
    - Adjacent docs (if any)
    - Adjacent examples (if any)
"""

import argparse
import os
import subprocess
import sys


def get_changed_files(root: str, ref: str) -> list[str]:
    """Get list of changed files via git diff."""
    try:
        result = subprocess.run(
            ["git", "diff", "--name-only", ref],
            cwd=root,
            capture_output=True,
            text=True,
            check=True,
        )
        return [f.strip() for f in result.stdout.strip().splitlines() if f.strip()]
    except subprocess.CalledProcessError as e:
        print(f"ERROR: git diff failed: {e.stderr}", file=sys.stderr)
        sys.exit(1)


def find_adjacent_docs(root: str, filepath: str) -> list[str]:
    """Find documentation files related to a source file."""
    adjacent: list[str] = []
    # Look for docs in the same directory or parent
    dirname = os.path.dirname(filepath)
    for search_dir in [dirname, os.path.dirname(dirname), "docs"]:
        full = os.path.join(root, search_dir)
        if os.path.isdir(full):
            for f in os.listdir(full):
                if f.endswith(".md"):
                    adjacent.append(os.path.join(search_dir, f))
    return adjacent


def classify_module(filepath: str) -> str:
    """Determine which module a file belongs to."""
    parts = filepath.split(os.sep)
    if parts and parts[0] in ("src", "include", "tests", "scripts", "docs", "prompts"):
        return parts[0]
    return "other"


def main() -> None:
    parser = argparse.ArgumentParser(description="Collect context for changed files.")
    parser.add_argument("--root", default=".", help="Repository root directory.")
    parser.add_argument("--ref", default="HEAD~1", help="Git ref to diff against.")
    parser.add_argument("--output", default=None, help="Output file path.")
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    changed = get_changed_files(root, args.ref)

    lines = ["# Changed File Context\n"]
    lines.append(f"Diff reference: `{args.ref}`\n")
    lines.append(f"Changed files: {len(changed)}\n")

    for fpath in changed:
        lines.append(f"## `{fpath}`\n")
        lines.append(f"- **Module**: {classify_module(fpath)}")
        adj_docs = find_adjacent_docs(root, fpath)
        if adj_docs:
            lines.append("- **Adjacent docs**:")
            for d in adj_docs[:5]:  # limit to avoid noise
                lines.append(f"  - `{d}`")
        else:
            lines.append("- **Adjacent docs**: none found")
        lines.append("")

    output = "\n".join(lines) + "\n"

    if args.output:
        os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
        with open(args.output, "w", encoding="utf-8") as fh:
            fh.write(output)
        print(f"Wrote changed context to {args.output}")
    else:
        print(output)


if __name__ == "__main__":
    main()
