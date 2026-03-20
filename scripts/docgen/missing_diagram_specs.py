#!/usr/bin/env python3
"""missing_diagram_specs.py — List missing diagram spec artifacts for scoped docs.

Purpose:
    Read doc_scope_decision.json, inspect approved target docs that contain
    Mermaid blocks, and report any missing diagram spec artifact paths.

Usage:
    python3 scripts/docgen/missing_diagram_specs.py [--root <repo-root>] [--scope <path>] [--output <path>]
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

try:
    from .verify_mermaid import extract_mermaid_blocks, slugify_doc_path
except ImportError:
    from verify_mermaid import extract_mermaid_blocks, slugify_doc_path


def load_json(path: Path) -> dict | None:
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return None


def target_docs_from_scope(root: Path, scope: dict) -> list[str]:
    approved = scope.get("approved_targets")
    if isinstance(approved, list) and approved:
        return [p for p in approved if (root / p).is_file()]
    paths: list[str] = []
    if scope.get("update_readme") and (root / "README.md").is_file():
        paths.append("README.md")
    if scope.get("update_book"):
        for p in scope.get("book_dirs_or_chapters") or []:
            if (root / p).is_file():
                paths.append(p)
        summary = "book/src/SUMMARY.md"
        if (root / summary).is_file():
            paths.append(summary)
    return sorted(set(paths))


def main() -> None:
    parser = argparse.ArgumentParser(description="List missing diagram spec artifacts for scoped docs.")
    parser.add_argument("--root", default=".", help="Repository root.")
    parser.add_argument("--scope", default=None, help="Path to doc_scope_decision.json.")
    parser.add_argument("--output", default=None, help="Optional JSON output path.")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    scope_path = Path(args.scope) if args.scope else root / "docs" / "generated" / "doc_scope_decision.json"
    if not scope_path.is_absolute():
        scope_path = (root / scope_path).resolve()
    scope = load_json(scope_path)
    if not scope:
        print(f"ERROR: scope file not found or invalid: {scope_path}", file=sys.stderr)
        sys.exit(1)

    missing: list[dict[str, object]] = []
    for rel_doc in target_docs_from_scope(root, scope):
        doc_path = root / rel_doc
        content = doc_path.read_text(encoding="utf-8")
        blocks, fence_errors = extract_mermaid_blocks(content)
        if fence_errors or not blocks:
            continue
        slug = slugify_doc_path(rel_doc)
        for index, block in enumerate(blocks, start=1):
            spec_rel = f"docs/generated/diagram_specs/{slug}/block-{index:02d}.md"
            if not (root / spec_rel).is_file():
                missing.append(
                    {
                        "doc_file": rel_doc,
                        "block_index": index,
                        "start_line": block["start_line"],
                        "spec_path": spec_rel,
                    }
                )

    payload = {"missing_specs": missing}
    if args.output:
        output_path = Path(args.output)
        if not output_path.is_absolute():
            output_path = (root / output_path).resolve()
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    print(json.dumps(payload, indent=2, ensure_ascii=False))
    if missing:
        sys.exit(1)


if __name__ == "__main__":
    main()
