#!/usr/bin/env python3
"""verify_paths.py — Check that paths referenced in documentation actually exist.

Purpose:
    Scan a Markdown document for path-like references (backtick-enclosed paths,
    link targets) and verify each one exists relative to the repository root.

Usage:
    python3 scripts/docgen/verify_paths.py <doc-file> [--root <repo-root>]

Arguments:
    doc-file  Path to the Markdown file to check.
    --root    Repository root directory (default: cwd).

Exit codes:
    0 — all paths valid
    1 — one or more paths not found
"""

import argparse
import os
import re
import sys

try:
    from .path_utils import (
        extract_markdown_link_targets,
        is_repo_reference,
        resolve_doc_relative_target,
    )
except ImportError:
    from path_utils import (
        extract_markdown_link_targets,
        is_repo_reference,
        resolve_doc_relative_target,
    )


def extract_paths(content: str) -> tuple[list[str], list[str]]:
    """Extract potential file paths from Markdown content.

    Returns (backtick_paths, link_targets) — backtick paths are repo-relative,
    link targets are document-relative.
    """
    backtick_paths: set[str] = set()
    link_targets: set[str] = set()

    # Backtick-enclosed paths (e.g., `src/main.cpp`, `build.sh`)
    for match in re.finditer(r"`([^`]+)`", content):
        candidate = match.group(1).strip()
        if is_repo_reference(candidate):
            backtick_paths.add(candidate)

    for target in extract_markdown_link_targets(content):
        link_targets.add(target)

    return sorted(backtick_paths), sorted(link_targets)


def main() -> None:
    parser = argparse.ArgumentParser(description="Verify paths in documentation.")
    parser.add_argument("doc_file", help="Markdown file to check.")
    parser.add_argument("--root", default=".", help="Repository root directory.")
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    doc_dir = os.path.dirname(os.path.abspath(args.doc_file))

    if not os.path.isfile(args.doc_file):
        print(f"ERROR: {args.doc_file} not found.", file=sys.stderr)
        sys.exit(1)

    with open(args.doc_file, "r", encoding="utf-8") as f:
        content = f.read()

    backtick_paths, link_targets = extract_paths(content)
    errors: list[str] = []

    # Backtick paths are repo-relative
    for p in backtick_paths:
        full = os.path.join(root, p)
        if not os.path.exists(full):
            errors.append(p)

    # Link targets are document-relative
    for p in link_targets:
        _, full, in_repo = resolve_doc_relative_target(root, args.doc_file, p)
        if not in_repo:
            errors.append(f"{p} (escapes repo root)")
            continue
        if not os.path.exists(full):
            errors.append(p)

    total = len(backtick_paths) + len(link_targets)

    if errors:
        print(f"FAIL: {len(errors)} path(s) not found in {args.doc_file}:")
        for e in errors:
            print(f"  - {e}")
        sys.exit(1)
    else:
        print(f"OK: all {total} path(s) in {args.doc_file} are valid.")


if __name__ == "__main__":
    main()
