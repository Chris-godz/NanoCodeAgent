#!/usr/bin/env python3
"""verify_doc_consistency.py — Check doc claims against repository artifacts.

Purpose:
    Verify that file paths and script references mentioned in documentation
    exist in the repository. Path references under ``scripts/`` and ``.agents/``
    are treated as errors (exit 1); other missing paths are warnings only.

Usage:
    python3 scripts/docgen/verify_doc_consistency.py <doc-file> [--root <repo-root>]

Arguments:
    doc-file  Path to the Markdown file to check.
    --root    Repository root directory (default: cwd).

Exit codes:
    0 — no errors (warnings are OK)
    1 — one or more errors detected

NOT_IMPLEMENTED:
    - CLI argument verification (--help parsing)
    - Config key / default-value verification
    - Environment variable verification

Status: NON-BLOCKING — path / script existence checks only.
"""

import argparse
import os
import re
import sys


# Prefixes whose absence is an error, not just a warning.
ERROR_PREFIXES = ("scripts/", ".agents/")
ROOT_FILE_NAMES = {"CMakeLists.txt", "Dockerfile", "LICENSE", "Makefile"}
ROOT_FILE_SUFFIXES = {
    ".cfg",
    ".cmake",
    ".conf",
    ".ini",
    ".json",
    ".md",
    ".rst",
    ".sh",
    ".toml",
    ".txt",
    ".xml",
    ".yaml",
    ".yml",
}
ROOT_DOTFILES = {
    ".clang-format",
    ".clang-tidy",
    ".editorconfig",
    ".env",
    ".env.example",
    ".gitattributes",
    ".gitignore",
}


def is_root_file_candidate(candidate: str) -> bool:
    """Return whether a bare token looks like a root-level repo file."""
    if candidate in ROOT_FILE_NAMES or candidate in ROOT_DOTFILES:
        return True

    stem, suffix = os.path.splitext(candidate)
    return bool(stem) and suffix in ROOT_FILE_SUFFIXES


def is_repo_reference(candidate: str) -> bool:
    """Return whether a backtick token should be checked against the repo."""
    if candidate.startswith("http") or candidate.startswith("-") or " " in candidate:
        return False

    return "/" in candidate or is_root_file_candidate(candidate)


def extract_filenames(content: str) -> list[str]:
    """Extract filenames mentioned in backticks."""
    filenames: set[str] = set()
    for match in re.finditer(r"`([^`]+)`", content):
        candidate = match.group(1).strip()
        if is_repo_reference(candidate):
            filenames.add(candidate)
    return sorted(filenames)


def main() -> None:
    parser = argparse.ArgumentParser(description="Check doc consistency with repo.")
    parser.add_argument("doc_file", help="Markdown file to check.")
    parser.add_argument("--root", default=".", help="Repository root directory.")
    args = parser.parse_args()

    root = os.path.abspath(args.root)

    if not os.path.isfile(args.doc_file):
        print(f"ERROR: {args.doc_file} not found.", file=sys.stderr)
        sys.exit(1)

    with open(args.doc_file, "r", encoding="utf-8") as f:
        content = f.read()

    filenames = extract_filenames(content)
    errors: list[str] = []
    warnings: list[str] = []

    for fname in filenames:
        full = os.path.join(root, fname)
        if os.path.exists(full):
            continue
        is_error = any(fname.startswith(p) for p in ERROR_PREFIXES)
        if is_error:
            errors.append(fname)
            print(f"  ERROR  {fname} — not found in repo")
        else:
            warnings.append(fname)
            print(f"  WARN   {fname} — not found (non-critical)")

    # Report NOT_IMPLEMENTED checks
    print(
        "\nNOT_IMPLEMENTED: CLI argument, config key, and environment variable checks."
    )

    if errors:
        print(
            f"\nFAILED: {len(errors)} error(s), {len(warnings)} warning(s) in {args.doc_file}."
        )
        sys.exit(1)
    elif warnings:
        print(f"\nPASSED with {len(warnings)} warning(s) in {args.doc_file}.")
    else:
        print(f"\nOK: all references in {args.doc_file} appear consistent.")


if __name__ == "__main__":
    main()
