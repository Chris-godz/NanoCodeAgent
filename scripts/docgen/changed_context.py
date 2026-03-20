#!/usr/bin/env python3
"""changed_context.py — Change context and change_facts for doc impact analysis.

Purpose:
    Default: output change_facts.json (structured facts only; no target-doc discovery).
    With --format md (deprecated): per-file context and impact heuristic only;
    adjacent docs and candidate docs are no longer produced (fixed target set: README + book).

Usage:
    python3 scripts/docgen/changed_context.py [--root <repo-root>] [--ref <git-ref>] [--output <path>]
    python3 scripts/docgen/changed_context.py [--root <repo-root>] --files <path> [--output <path>]
    python3 scripts/docgen/changed_context.py --format md ...  # deprecated legacy Markdown

Arguments:
    --root    Repository root directory (default: cwd).
    --ref     Git ref to diff against (default: HEAD~1). Ignored when --files is set.
    --files   Path to a file listing repo-relative paths (one per line). When set, --ref is ignored.
    --output  Output file path (default: stdout for md; for json default is docs/generated/change_facts.json).
    --format  json (default) or md (deprecated).

Exit codes:
    0 — success
    1 — invalid arguments or git error
"""

import argparse
import json
import os
import re
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


def read_files_list(root: str, path: str) -> list[str]:
    """Read repo-relative file paths from a file (one per line)."""
    full = os.path.join(root, path) if not os.path.isabs(path) else path
    if not os.path.isfile(full):
        return []
    with open(full, "r", encoding="utf-8") as f:
        return [line.strip() for line in f if line.strip()]


def find_adjacent_docs(root: str, filepath: str) -> list[str]:
    """Find documentation files related to a source file. Returns repo-relative paths."""
    adjacent: set[str] = set()
    dirname = os.path.dirname(filepath)
    for search_dir in {dirname, os.path.dirname(dirname), "docs"}:
        full = os.path.join(root, search_dir)
        if os.path.isdir(full):
            for f in os.listdir(full):
                if f.endswith(".md"):
                    adjacent.add(os.path.join(search_dir, f))
    return sorted(adjacent)


def find_examples(root: str) -> list[dict]:
    """Look for example files in examples/, prompts/, docs/templates (same as example_inventory)."""
    example_dirs = ["examples", "prompts", "docs/templates"]
    entries: list[dict] = []
    for edir in example_dirs:
        full = os.path.join(root, edir)
        if not os.path.isdir(full):
            continue
        for dirpath, _, filenames in os.walk(full):
            for fname in sorted(filenames):
                fpath = os.path.join(dirpath, fname)
                rel = os.path.relpath(fpath, root).replace("\\", "/")
                entries.append({"path": rel, "type": edir})
    return entries


def find_related_examples(root: str, changed_path: str, all_examples: list[dict]) -> list[str]:
    """For a changed file, return repo-relative paths of related examples."""
    changed_path = changed_path.replace("\\", "/")
    changed_dir = os.path.dirname(changed_path)
    related: set[str] = set()
    for e in all_examples:
        p = e["path"]
        if os.path.dirname(p) == changed_dir:
            related.add(p)
        if p == changed_path:
            related.add(p)
        if changed_dir and p.startswith(changed_dir + "/"):
            related.add(p)
    return sorted(related)


def candidate_docs(root: str) -> list[str]:
    """Scan README.md (if exists), docs/, book/src/ for .md. Returns repo-relative paths."""
    paths: list[str] = []
    if os.path.isfile(os.path.join(root, "README.md")):
        paths.append("README.md")
    doc_dirs = ["docs", os.path.join("book", "src")]
    for doc_dir in doc_dirs:
        full = os.path.join(root, doc_dir)
        if not os.path.isdir(full):
            continue
        for dirpath, _, filenames in os.walk(full):
            for fname in sorted(filenames):
                if fname.endswith(".md"):
                    fpath = os.path.join(dirpath, fname)
                    rel = os.path.relpath(fpath, root).replace("\\", "/")
                    paths.append(rel)
    return sorted(paths)


def classify_module(filepath: str) -> str:
    """Determine which module a file belongs to."""
    parts = filepath.replace("\\", "/").split("/")
    if parts and parts[0] in ("src", "include", "tests", "scripts", "docs", "prompts", "book", "examples"):
        return parts[0]
    return "other"


def impact_perspective(changed: list[str]) -> tuple[bool, str]:
    """Heuristic: likely_doc_impact and short reason. Does not replace skill judgment."""
    if not changed:
        return False, "no changes"
    reasons: list[str] = []
    for f in changed:
        f = f.replace("\\", "/")
        if f == "README.md":
            reasons.append("README.md")
        elif f == "src/main.cpp" or (f.startswith("src/") and "main" in f):
            reasons.append("main_entry")
        elif f.startswith("include/") and "cli" in f.lower():
            reasons.append("cli_header")
        elif re.search(r"src/.*main.*\.(cpp|cc|c)", f):
            reasons.append("main_entry")
        elif f.startswith("docs/"):
            reasons.append("docs_change")
        elif f.startswith("examples/") or f.startswith("prompts/") or f.startswith("book/src/"):
            reasons.append("examples_or_tutorial")
    if reasons:
        return True, "; ".join(reasons)
    # Only tests/ or src/ impl (no main, no cli)
    only_internal = all(
        f.replace("\\", "/").startswith(("tests/", "src/")) and "main" not in f and "cli" not in f.lower()
        for f in changed
    )
    if only_internal:
        return False, "tests_or_internal_impl_only"
    return False, "other"


def main() -> None:
    parser = argparse.ArgumentParser(description="Collect context or change facts for changed files.")
    parser.add_argument("--root", default=".", help="Repository root directory.")
    parser.add_argument("--ref", default="HEAD~1", help="Git ref to diff against (ignored if --files set).")
    parser.add_argument("--files", default=None, metavar="PATH", help="File listing repo-relative paths (one per line); overrides --ref.")
    parser.add_argument("--output", default=None, help="Output file path.")
    parser.add_argument("--format", choices=("json", "md"), default="json", help="Output format: json (default) or md (deprecated).")
    args = parser.parse_args()

    root = os.path.abspath(args.root)

    if args.format == "json":
        _script_dir = os.path.dirname(os.path.abspath(__file__))
        if _script_dir not in sys.path:
            sys.path.insert(0, _script_dir)
        from change_facts import collect_facts
        facts, _ = collect_facts(root, args.ref, args.files)
        out_path = args.output or os.path.join(root, "docs", "generated", "change_facts.json")
        os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
        with open(out_path, "w", encoding="utf-8") as fh:
            json.dump(facts, fh, indent=2, ensure_ascii=False)
            fh.write("\n")
        print(f"Wrote change facts to {out_path}")
        return

    # Legacy --format md (deprecated): no adjacent docs, no candidate docs
    if args.files:
        changed = read_files_list(root, args.files)
        ref_label = f"(from file {args.files})"
    else:
        changed = get_changed_files(root, args.ref)
        ref_label = args.ref

    all_examples = find_examples(root)
    likely_impact, impact_reason = impact_perspective(changed)

    lines = ["# Changed File Context\n"]
    lines.append(f"Diff reference: `{ref_label}`\n")
    lines.append(f"Changed files: {len(changed)}\n")

    for fpath in changed:
        fpath_norm = fpath.replace("\\", "/")
        lines.append(f"## `{fpath_norm}`\n")
        lines.append(f"- **Module**: {classify_module(fpath_norm)}")
        related_ex = find_related_examples(root, fpath_norm, all_examples)
        if related_ex:
            lines.append("- **Related examples**:")
            for ex in related_ex[:10]:
                lines.append(f"  - `{ex}`")
        else:
            lines.append("- **Related examples**: none found")
        lines.append("")

    lines.append("## Impact perspective (heuristic only)\n")
    lines.append(f"- **likely_doc_impact**: {str(likely_impact).lower()}")
    lines.append(f"- **reason**: {impact_reason}\n")

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
