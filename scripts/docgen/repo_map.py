#!/usr/bin/env python3
"""repo_map.py — Generate a structured map of the repository.

Purpose:
    Produce a machine- and human-readable summary of the repository layout
    including directory tree, key entry files, core module locations, and
    test / example distribution.

Usage:
    python3 scripts/docgen/repo_map.py [--root <repo-root>] [--output <path>]

Arguments:
    --root    Repository root directory (default: current working directory).
    --output  Output file path (default: stdout).

Exit codes:
    0 — success
    1 — invalid arguments or I/O error

Output format (stdout or file):
    Markdown with the following sections:
    - Directory Tree (depth-limited)
    - Key Entry Files
    - Core Modules
    - Test Distribution
    - Example Distribution
"""

import argparse
import os
import sys


def relative_depth(root: str, dirpath: str) -> int:
    """Return the directory depth of dirpath relative to root."""
    relative = os.path.relpath(dirpath, root)
    if relative == ".":
        return 0
    return relative.count(os.sep) + 1


def build_tree(root: str, max_depth: int = 3) -> list[str]:
    """Walk the directory tree up to max_depth and return indented lines."""
    lines: list[str] = []
    root = os.path.abspath(root)
    skip_dirs = {".git", "node_modules", "__pycache__", "build", ".cache"}

    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in sorted(dirnames) if d not in skip_dirs]
        depth = relative_depth(root, dirpath)
        if depth >= max_depth:
            dirnames.clear()
            continue
        indent = "  " * depth
        basename = os.path.basename(dirpath) or os.path.basename(root)
        lines.append(f"{indent}{basename}/")
        sub_indent = "  " * (depth + 1)
        for fname in sorted(filenames):
            lines.append(f"{sub_indent}{fname}")
    return lines


def find_key_files(root: str) -> list[str]:
    """Identify key entry-point files."""
    candidates = [
        "CMakeLists.txt",
        "Makefile",
        "build.sh",
        "README.md",
        "README_zh.md",
        "AGENTS.md",
        "src/main.cpp",
    ]
    found = []
    for c in candidates:
        if os.path.exists(os.path.join(root, c)):
            found.append(c)
    return found


def list_modules(root: str, dirs: list[str]) -> dict[str, list[str]]:
    """List files under given subdirectories."""
    result: dict[str, list[str]] = {}
    for d in dirs:
        full = os.path.join(root, d)
        if os.path.isdir(full):
            result[d] = sorted(os.listdir(full))
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate repository map.")
    parser.add_argument("--root", default=".", help="Repository root directory.")
    parser.add_argument(
        "--output", default=None, help="Output file path (default: stdout)."
    )
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print(f"ERROR: {root} is not a directory.", file=sys.stderr)
        sys.exit(1)

    sections: list[str] = []

    # Directory tree
    sections.append("# Repository Map\n")
    sections.append("## Directory Tree\n")
    sections.append("```")
    sections.extend(build_tree(root))
    sections.append("```\n")

    # Key entry files
    sections.append("## Key Entry Files\n")
    for f in find_key_files(root):
        sections.append(f"- `{f}`")
    sections.append("")

    # Core modules
    sections.append("## Core Modules\n")
    for dirname, files in list_modules(root, ["src", "include"]).items():
        sections.append(f"### {dirname}/\n")
        for f in files:
            sections.append(f"- `{f}`")
        sections.append("")

    # Tests
    sections.append("## Test Distribution\n")
    for dirname, files in list_modules(root, ["tests"]).items():
        sections.append(f"### {dirname}/\n")
        for f in files:
            sections.append(f"- `{f}`")
        sections.append("")

    output = "\n".join(sections) + "\n"

    if args.output:
        os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
        with open(args.output, "w", encoding="utf-8") as fh:
            fh.write(output)
        print(f"Wrote repo map to {args.output}")
    else:
        print(output)


if __name__ == "__main__":
    main()
