#!/usr/bin/env python3
"""reference_context.py — Collect reference material for doc update (no target-doc discovery).

Purpose:
    After doc_scope_decision has been made, collect reference-only context:
    examples, tests, templates, relevant source paths, optional CLI help/config,
    and excerpts from existing README and book. This is for Agent use when
    updating README and book only. Does not list or discover target docs.

Usage:
    python3 scripts/docgen/reference_context.py [--root <repo-root>] [--scope <path>] [--facts <path>] [--output <path>]

Arguments:
    --root   Repository root directory (default: cwd).
    --scope  Path to doc_scope_decision.json (default: docs/generated/doc_scope_decision.json).
    --facts  Path to change_facts.json (default: docs/generated/change_facts.json).
    --output Output JSON path (default: docs/generated/reference_context.json).

Exit codes:
    0 — success
    1 — missing scope file or I/O error
"""

import argparse
import json
import os
import sys


def load_json(root: str, path: str) -> dict | None:
    """Load JSON file; path may be relative to root."""
    full = os.path.join(root, path) if not os.path.isabs(path) else path
    if not os.path.isfile(full):
        return None
    try:
        with open(full, "r", encoding="utf-8") as f:
            return json.load(f)
    except (json.JSONDecodeError, OSError):
        return None


def collect_examples(root: str) -> list[dict]:
    """List example/prompt/template paths (reference only)."""
    entries: list[dict] = []
    for edir in ["examples", "prompts", "docs/templates"]:
        full = os.path.join(root, edir)
        if not os.path.isdir(full):
            continue
        for dirpath, _, filenames in os.walk(full):
            for fname in sorted(filenames):
                fpath = os.path.join(dirpath, fname)
                rel = os.path.relpath(fpath, root).replace("\\", "/")
                entries.append({"path": rel, "type": edir})
    return entries


def collect_related_tests_and_source(root: str, changed_paths: list[str]) -> tuple[list[str], list[str]]:
    """From change_facts changed_files paths, list related tests and source (by directory)."""
    tests: set[str] = set()
    source: set[str] = set()
    for e in changed_paths:
        if isinstance(e, dict):
            path = e.get("path", "")
        else:
            path = str(e)
        path = path.replace("\\", "/")
        top = path.split("/")[0] if "/" in path else path
        if top == "scripts" or top == "src" or top == "include":
            source.add(path)
        if top == "tests" or path.startswith("tests/"):
            tests.add(path)
        # If changed file is under scripts/docgen, add scripts/docgen as source context
        if path.startswith("scripts/"):
            source.add(path)
    return sorted(tests), sorted(source)


def collect_existing_doc_excerpts(root: str, update_readme: bool, update_book: bool, book_paths: list[str]) -> dict:
    """Read excerpts from README and in-scope book files (for reference only)."""
    excerpts: dict = {"readme": None, "book": {}}
    if update_readme:
        readme_path = os.path.join(root, "README.md")
        if os.path.isfile(readme_path):
            try:
                with open(readme_path, "r", encoding="utf-8") as f:
                    content = f.read()
                excerpts["readme"] = content[:8000] + ("..." if len(content) > 8000 else "")
            except OSError:
                pass
    if update_book and book_paths:
        book_src = os.path.join(root, "book", "src")
        if os.path.isdir(book_src):
            for dirpath, _, filenames in os.walk(book_src):
                for fname in sorted(filenames):
                    if not fname.endswith(".md"):
                        continue
                    fpath = os.path.join(dirpath, fname)
                    rel = os.path.relpath(fpath, root).replace("\\", "/")
                    in_scope = any(rel == p or rel.startswith(p.rstrip("/") + "/") or p == "book/src" for p in book_paths)
                    if not book_paths or in_scope or "book/src" in str(book_paths):
                        try:
                            with open(fpath, "r", encoding="utf-8") as f:
                                content = f.read()
                            excerpts["book"][rel] = content[:6000] + ("..." if len(content) > 6000 else "")
                        except OSError:
                            pass
    return excerpts


def main() -> None:
    parser = argparse.ArgumentParser(description="Collect reference context for doc update (README + book only).")
    parser.add_argument("--root", default=".", help="Repository root.")
    parser.add_argument("--scope", default=None, help="Path to doc_scope_decision.json.")
    parser.add_argument("--facts", default=None, help="Path to change_facts.json.")
    parser.add_argument("--output", default=None, help="Output reference_context.json path.")
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    scope_path = args.scope or os.path.join(root, "docs", "generated", "doc_scope_decision.json")
    facts_path = args.facts or os.path.join(root, "docs", "generated", "change_facts.json")
    out_path = args.output or os.path.join(root, "docs", "generated", "reference_context.json")

    scope = load_json(root, scope_path)
    if not scope:
        print(f"ERROR: doc_scope_decision not found at {scope_path}. Run doc_scope_decision (Agent) first.", file=sys.stderr)
        sys.exit(1)

    facts = load_json(root, facts_path)
    changed_entries = (facts or {}).get("changed_files", [])
    changed_paths = [e.get("path", e) if isinstance(e, dict) else e for e in changed_entries]

    examples = collect_examples(root)
    tests, source_paths = collect_related_tests_and_source(root, changed_paths)
    templates = [e for e in examples if e.get("type") == "docs/templates"]

    update_readme = scope.get("update_readme", False)
    update_book = scope.get("update_book", False)
    book_dirs = scope.get("book_dirs_or_chapters") or (["book/src"] if update_book else [])
    existing_doc_excerpts = collect_existing_doc_excerpts(root, update_readme, update_book, book_dirs)

    ref = {
        "_comment": "Reference only. Target docs are README.md and book/src/**/*.md only; this file does not list target docs.",
        "examples": [e["path"] for e in examples],
        "tests": tests,
        "templates": [e["path"] for e in templates],
        "source_paths": source_paths,
        "existing_doc_excerpts": existing_doc_excerpts,
        "cli_help": None,
        "config_schema": None,
    }

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(ref, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print(f"Wrote reference context to {out_path}")


if __name__ == "__main__":
    main()
