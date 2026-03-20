#!/usr/bin/env python3
"""verify_diagram_specs.py — Require diagram spec artifacts for Mermaid docs.

Purpose:
    For an in-scope Markdown document, ensure every Mermaid block has a matching
    diagram spec artifact under docs/generated/diagram_specs/<slug>/block-XX.md.

Usage:
    python3 scripts/docgen/verify_diagram_specs.py <doc-file> [--root <repo-root>]
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

try:
    from .verify_mermaid import extract_mermaid_blocks, slugify_doc_path
except ImportError:
    from verify_mermaid import extract_mermaid_blocks, slugify_doc_path


def main() -> None:
    parser = argparse.ArgumentParser(description="Verify diagram spec artifacts for Mermaid docs.")
    parser.add_argument("doc_file", help="Markdown file to check.")
    parser.add_argument("--root", default=".", help="Repository root directory.")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    doc_file = Path(args.doc_file)
    if not doc_file.is_absolute():
        doc_file = (root / doc_file).resolve()

    if not doc_file.is_file():
        print(f"ERROR: {doc_file} not found.", file=sys.stderr)
        sys.exit(1)

    content = doc_file.read_text(encoding="utf-8")
    rel_doc = os.path.relpath(doc_file, root).replace("\\", "/")
    blocks, fence_errors = extract_mermaid_blocks(content)

    if fence_errors:
        for error in fence_errors:
            print(f"  FAIL  {error}")
        print(f"\nFAILED: Mermaid fence errors detected in {rel_doc}.")
        sys.exit(1)

    if not blocks:
        print(f"OK: no Mermaid diagram spec required for {rel_doc}.")
        return

    slug = slugify_doc_path(rel_doc)
    spec_dir = root / "docs" / "generated" / "diagram_specs" / slug
    missing: list[str] = []
    for index, block in enumerate(blocks, start=1):
        spec_rel = f"docs/generated/diagram_specs/{slug}/block-{index:02d}.md"
        if not (root / spec_rel).is_file():
            missing.append(f"block {index} (line {block['start_line']}): {spec_rel}")

    if missing:
        print(f"FAIL: {len(missing)} missing diagram spec artifact(s) for {rel_doc}:")
        for item in missing:
            print(f"  - {item}")
        sys.exit(1)

    print(f"OK: all {len(blocks)} Mermaid block(s) in {rel_doc} have diagram specs in {spec_dir.relative_to(root)}.")


if __name__ == "__main__":
    main()
