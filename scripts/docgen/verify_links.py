#!/usr/bin/env python3
"""verify_links.py — Check internal Markdown links and anchors.

Purpose:
    Scan a Markdown document for internal links (relative paths and anchors)
    and verify they resolve to valid targets.

Usage:
    python3 scripts/docgen/verify_links.py <doc-file> [--root <repo-root>]

Arguments:
    doc-file  Path to the Markdown file to check.
    --root    Repository root directory (default: cwd).

Exit codes:
    0 — all links valid
    1 — one or more broken links
"""

import argparse
import os
import re
import sys


def extract_links(content: str) -> list[dict]:
    """Extract Markdown links with their targets."""
    links: list[dict] = []
    for match in re.finditer(r"\[([^\]]*)\]\(([^)]+)\)", content):
        text = match.group(1)
        target = match.group(2).strip()
        if not target.startswith("http"):
            links.append({"text": text, "target": target, "pos": match.start()})
    return links


def check_anchor(filepath: str, anchor: str) -> bool:
    """Check if a heading anchor exists in a Markdown file."""
    if not os.path.isfile(filepath):
        return False
    try:
        with open(filepath, "r", encoding="utf-8") as f:
            content = f.read()
    except OSError:
        return False

    # Convert headings to anchors (simplified GitHub-style)
    for line in content.splitlines():
        if line.startswith("#"):
            heading = line.lstrip("#").strip()
            slug = re.sub(r"[^\w\s-]", "", heading.lower())
            slug = re.sub(r"\s+", "-", slug).strip("-")
            if slug == anchor:
                return True
    return False


def main() -> None:
    parser = argparse.ArgumentParser(description="Verify internal Markdown links.")
    parser.add_argument("doc_file", help="Markdown file to check.")
    parser.add_argument("--root", default=".", help="Repository root directory.")
    args = parser.parse_args()

    root = os.path.abspath(args.root)

    if not os.path.isfile(args.doc_file):
        print(f"ERROR: {args.doc_file} not found.", file=sys.stderr)
        sys.exit(1)

    with open(args.doc_file, "r", encoding="utf-8") as f:
        content = f.read()

    links = extract_links(content)
    errors: list[str] = []
    doc_dir = os.path.dirname(os.path.abspath(args.doc_file))

    for link in links:
        target = link["target"]
        path_part, _, anchor = target.partition("#")

        if path_part:
            # Resolve relative to the document's directory
            full_path = os.path.normpath(os.path.join(doc_dir, path_part))
            if not os.path.exists(full_path):
                errors.append(f"broken path: {target}")
                continue
            if anchor and not check_anchor(full_path, anchor):
                errors.append(f"broken anchor: {target}")
        elif anchor:
            # Same-file anchor
            if not check_anchor(os.path.abspath(args.doc_file), anchor):
                errors.append(f"broken anchor: #{anchor}")

    if errors:
        print(f"FAIL: {len(errors)} broken link(s) in {args.doc_file}:")
        for e in errors:
            print(f"  - {e}")
        sys.exit(1)
    else:
        print(f"OK: all {len(links)} link(s) in {args.doc_file} are valid.")


if __name__ == "__main__":
    main()
