#!/usr/bin/env python3
"""example_inventory.py — Map examples and sample code to tutorial sections.

Purpose:
    Scan for example files, code snippets in docs, and sample configurations.
    Produce a mapping that helps tutorial-update know which examples are relevant
    to which documentation sections.

Usage:
    python3 scripts/docgen/example_inventory.py [--root <repo-root>] [--output <path>]

Arguments:
    --root    Repository root directory (default: cwd).
    --output  Output file path (default: stdout).

Exit codes:
    0 — success
    1 — invalid arguments or I/O error

Output format:
    Markdown list of examples with their likely tutorial associations.
"""

import argparse
import os
import re
import sys


def find_examples(root: str) -> list[dict]:
    """Look for example files in common locations."""
    example_dirs = ["examples", "prompts", "docs/templates"]
    entries: list[dict] = []

    for edir in example_dirs:
        full = os.path.join(root, edir)
        if not os.path.isdir(full):
            continue
        for dirpath, _, filenames in os.walk(full):
            for fname in sorted(filenames):
                fpath = os.path.join(dirpath, fname)
                rel = os.path.relpath(fpath, root)
                entries.append({"path": rel, "type": edir})

    return entries


def find_code_blocks_in_docs(root: str) -> list[dict]:
    """Count fenced code blocks in documentation files."""
    docs_dir = os.path.join(root, "docs")
    results: list[dict] = []

    if not os.path.isdir(docs_dir):
        return results

    for dirpath, _, filenames in os.walk(docs_dir):
        for fname in sorted(filenames):
            if not fname.endswith(".md"):
                continue
            fpath = os.path.join(dirpath, fname)
            rel = os.path.relpath(fpath, root)
            try:
                with open(fpath, "r", encoding="utf-8") as f:
                    content = f.read()
                block_count = len(re.findall(r"^```", content, re.MULTILINE)) // 2
                if block_count > 0:
                    results.append({"doc": rel, "code_blocks": block_count})
            except OSError:
                pass

    return results


def main() -> None:
    parser = argparse.ArgumentParser(description="Inventory examples and code samples.")
    parser.add_argument("--root", default=".", help="Repository root directory.")
    parser.add_argument("--output", default=None, help="Output file path.")
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    examples = find_examples(root)
    code_blocks = find_code_blocks_in_docs(root)

    lines = ["# Example Inventory\n"]

    lines.append("## Example Files\n")
    if examples:
        for e in examples:
            lines.append(f"- `{e['path']}` (source: {e['type']}/)")
    else:
        lines.append("- No dedicated example files found.\n")

    lines.append("\n## Code Blocks in Documentation\n")
    if code_blocks:
        for cb in code_blocks:
            lines.append(f"- `{cb['doc']}`: {cb['code_blocks']} code block(s)")
    else:
        lines.append("- No code blocks found in docs.\n")

    lines.append("")
    output = "\n".join(lines) + "\n"

    if args.output:
        os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
        with open(args.output, "w", encoding="utf-8") as fh:
            fh.write(output)
        print(f"Wrote example inventory to {args.output}")
    else:
        print(output)


if __name__ == "__main__":
    main()
