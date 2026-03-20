#!/usr/bin/env python3
"""generate_candidates.py — Generate candidate doc drafts from change_impact_report.md.

Purpose:
    Parse change_impact_report.md for Required/Optional doc paths; copy originals
    to docs/generated/candidates/ with an appended "Suggested updates" placeholder.
    Does not overwrite real docs.

Usage:
    python3 scripts/docgen/generate_candidates.py [--root <repo-root>] [--report <path>] [--out-dir <path>]

Arguments:
    --root    Repository root directory (default: cwd).
    --report  Path to change_impact_report.md (default: docs/generated/change_impact_report.md).
    --out-dir Output directory for candidates (default: docs/generated/candidates/).

Exit codes:
    0 — success
    1 — invalid args or I/O error
"""

import argparse
import os
import re
import sys


def parse_impact_report(content: str) -> dict:
    """Parse change_impact_report.md for paths and summary. Returns dict with
    required_paths, optional_paths, change_summary, affected_modules.
    """
    data = {
        "required_paths": [],
        "optional_paths": [],
        "change_summary": "",
        "affected_modules": [],
    }
    lines = content.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.strip() == "### Required updates":
            i += 1
            while i < len(lines) and not lines[i].strip().startswith("###") and not lines[i].strip().startswith("##"):
                m = re.search(r"`([^`]+)`", lines[i])
                if m:
                    data["required_paths"].append(m.group(1).strip())
                i += 1
            continue
        if line.strip() == "### Optional updates":
            i += 1
            while i < len(lines) and not lines[i].strip().startswith("###") and not lines[i].strip().startswith("##"):
                m = re.search(r"`([^`]+)`", lines[i])
                if m:
                    data["optional_paths"].append(m.group(1).strip())
                i += 1
            continue
        if line.strip() == "## Change summary" and i + 1 < len(lines):
            data["change_summary"] = lines[i + 1].strip()
        if line.strip() == "## Affected modules":
            i += 1
            while i < len(lines) and lines[i].strip().startswith("- ") and not lines[i].strip().startswith("- **"):
                mod = lines[i].strip()[2:].strip()
                if mod and mod != "(none)":
                    data["affected_modules"].append(mod)
                i += 1
            continue
        i += 1
    return data


def is_allowed_path(path: str) -> bool:
    """Only README, docs/*.md (excl. generated), book/src/*.md."""
    path = path.replace("\\", "/")
    if path in ("README.md", "README_zh.md"):
        return True
    if path.startswith("docs/") and path.endswith(".md"):
        if path.startswith("docs/generated/"):
            return False
        return True
    if path.startswith("book/src/") and path.endswith(".md"):
        return True
    return False


def safe_candidate_name(repo_path: str) -> str:
    """Turn repo-relative path into a safe filename under candidates/."""
    return repo_path.replace("/", "_").replace("\\", "_")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate candidate docs from change impact report.")
    parser.add_argument("--root", default=".", help="Repository root.")
    parser.add_argument("--report", default=None, help="Path to change_impact_report.md.")
    parser.add_argument("--out-dir", default=None, help="Output directory for candidates.")
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    report_path = args.report or os.path.join(root, "docs", "generated", "change_impact_report.md")
    out_dir = args.out_dir or os.path.join(root, "docs", "generated", "candidates")

    if not os.path.isfile(report_path):
        print(f"ERROR: report not found: {report_path}", file=sys.stderr)
        sys.exit(1)

    with open(report_path, "r", encoding="utf-8") as f:
        data = parse_impact_report(f.read())

    all_paths = list(dict.fromkeys(data["required_paths"] + data["optional_paths"]))
    allowed = [p for p in all_paths if is_allowed_path(p)]
    summary = data["change_summary"] or "See change impact report."
    modules = data["affected_modules"] or ["(none)"]

    os.makedirs(out_dir, exist_ok=True)
    generated = []

    for repo_path in allowed:
        full_src = os.path.join(root, repo_path.replace("/", os.sep))
        if not os.path.isfile(full_src):
            continue
        safe_name = safe_candidate_name(repo_path)
        out_path = os.path.join(out_dir, safe_name)
        try:
            with open(full_src, "r", encoding="utf-8") as f:
                body = f.read()
        except OSError as e:
            print(f"WARN: skip {repo_path}: {e}", file=sys.stderr)
            continue

        placeholder = "\n\n---\n\n## Suggested updates (auto-generated placeholder)\n\n"
        placeholder += f"{summary}\n\n"
        placeholder += "**Affected modules:** " + ", ".join(modules) + "\n\n"
        placeholder += "*This section was added by docgen; update manually as needed.*\n"
        with open(out_path, "w", encoding="utf-8") as f:
            f.write(body)
            f.write(placeholder)
        rel_out = os.path.relpath(out_path, root).replace("\\", "/")
        generated.append(rel_out)
        print(f"Wrote candidate: {rel_out}")

    if not generated:
        print("No candidate files written (no allowed paths or sources missing).")
    else:
        print(f"Generated {len(generated)} candidate(s) under {os.path.relpath(out_dir, root)}.")


if __name__ == "__main__":
    main()
