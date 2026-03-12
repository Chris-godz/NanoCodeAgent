#!/usr/bin/env python3
"""verify_doc_consistency.py — Check doc claims against repository artifacts.

Purpose:
    Verify that file paths, script references, and local Markdown link targets
    mentioned in documentation exist in the repository. Path references under
    ``scripts/`` and ``.agents/`` are treated as errors (exit 1); other missing
    paths are warnings only.

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

try:
    from .path_utils import extract_markdown_link_targets, is_repo_reference
except ImportError:
    from path_utils import extract_markdown_link_targets, is_repo_reference


# Prefixes whose absence is an error, not just a warning.
ERROR_PREFIXES = ("scripts/", ".agents/")


def extract_filenames(content: str) -> list[str]:
    """Extract filenames mentioned in backticks."""
    filenames: set[str] = set()
    for match in re.finditer(r"`([^`]+)`", content):
        candidate = match.group(1).strip()
        if is_repo_reference(candidate):
            filenames.add(candidate)
    return sorted(filenames)


def extract_link_references(
    content: str, doc_file: str, root: str
) -> list[tuple[str, str]]:
    """Extract local Markdown link targets and resolve them for existence checks."""
    doc_dir = os.path.dirname(os.path.abspath(doc_file))
    repo_root = os.path.abspath(root)
    references: dict[str, str] = {}

    for target in extract_markdown_link_targets(content):
        full_path = os.path.abspath(os.path.join(doc_dir, target))
        try:
            in_repo = os.path.commonpath((repo_root, full_path)) == repo_root
        except ValueError:
            in_repo = False

        reference = os.path.relpath(full_path, repo_root) if in_repo else target
        references.setdefault(reference, full_path)

    return sorted(references.items())


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

    references: dict[str, str] = {
        filename: os.path.join(root, filename)
        for filename in extract_filenames(content)
    }
    references.update(extract_link_references(content, args.doc_file, root))
    errors: list[str] = []
    warnings: list[str] = []

    for fname, full in sorted(references.items()):
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
