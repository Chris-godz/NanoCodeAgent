#!/usr/bin/env python3
"""verify_paths.py — Check that paths referenced in documentation actually exist.

Purpose:
    Scan a Markdown document for path-like references (backtick-enclosed paths,
    link targets) and verify each one exists relative to the repository root.

Usage:
    python3 scripts/docgen/verify_paths.py <doc-file> [--root <repo-root>] [--ignore-repo-prefix <path-prefix>]

Arguments:
    doc-file              Path to the Markdown file to check.
    --root                Repository root directory (default: cwd).
    --ignore-repo-prefix  Repo-relative path prefix to skip during existence checks.

Exit codes:
    0 — all paths valid
    1 — one or more path references are invalid or not found
"""

import argparse
import os
import sys

try:
    from . import path_utils
except ImportError:
    import path_utils


def extract_paths(content: str) -> tuple[list[str], list[str]]:
    """Extract potential file paths from Markdown content.

    Returns (backtick_paths, link_targets) — backtick paths are repo-relative,
    link targets are document-relative.
    """
    backtick_paths = path_utils.extract_backtick_repo_references(content)
    link_targets = path_utils.extract_markdown_link_targets(content)
    return backtick_paths, link_targets


def normalize_repo_reference(path: str) -> str:
    normalized = os.path.normpath(path).replace("\\", "/")
    return normalized.rstrip("/")


def should_ignore_reference(path: str, ignored_prefixes: list[str]) -> bool:
    normalized = normalize_repo_reference(path)
    for prefix in ignored_prefixes:
        if normalized == prefix or normalized.startswith(prefix + "/"):
            return True
    return False


def main() -> None:
    parser = argparse.ArgumentParser(description="Verify paths in documentation.")
    parser.add_argument("doc_file", help="Markdown file to check.")
    parser.add_argument("--root", default=".", help="Repository root directory.")
    parser.add_argument(
        "--ignore-repo-prefix",
        action="append",
        default=[],
        help="Repo-relative path prefix to skip during existence checks. Repeat as needed.",
    )
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    ignored_prefixes = [
        normalize_repo_reference(prefix)
        for prefix in args.ignore_repo_prefix
        if prefix and prefix.strip()
    ]

    if not os.path.isfile(args.doc_file):
        print(f"ERROR: {args.doc_file} not found.", file=sys.stderr)
        sys.exit(1)

    with open(args.doc_file, "r", encoding="utf-8") as f:
        content = f.read()

    backtick_paths, link_targets = extract_paths(content)
    errors: list[str] = []

    # Backtick paths are repo-relative
    for p in backtick_paths:
        if path_utils.is_absolute_reference(p):
            errors.append(f"{p} (absolute path forbidden)")
            continue
        if should_ignore_reference(p, ignored_prefixes):
            continue
        full = os.path.join(root, p)
        if not os.path.exists(full):
            errors.append(p)

    # Link targets are document-relative
    for p in link_targets:
        if path_utils.is_absolute_reference(p):
            errors.append(f"{p} (absolute path forbidden)")
            continue
        reference, full, in_repo = path_utils.resolve_doc_relative_target(
            root, args.doc_file, p
        )
        if not in_repo:
            errors.append(f"{p} (escapes repo root)")
            continue
        if should_ignore_reference(reference, ignored_prefixes):
            continue
        if not os.path.exists(full):
            errors.append(p)

    total = len(backtick_paths) + len(link_targets)

    if errors:
        print(f"FAIL: {len(errors)} invalid path reference(s) in {args.doc_file}:")
        for e in errors:
            print(f"  - {e}")
        sys.exit(1)
    else:
        print(f"OK: all {total} path(s) in {args.doc_file} are valid.")


if __name__ == "__main__":
    main()
