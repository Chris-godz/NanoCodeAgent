#!/usr/bin/env python3
"""run_review_report.py — Heuristic review of candidate docs; write docgen_review_report.md.

Purpose:
    Run a deterministic checklist on each candidate (structure, absolute paths,
    suggested-updates section, length). Does NOT rewrite content. Output is for
    human follow-up. This is a heuristic checklist, not full semantic review.

Usage:
    python3 scripts/docgen/run_review_report.py [--root <repo-root>] [--candidates-dir <path>] [--impact-report <path>] [--output <path>]

Exit codes:
    0 — report written (review step is non-blocking in E2E)
    1 — I/O error
"""

import argparse
import os
import re
import sys


def review_one(content: str, path: str) -> dict:
    """Run heuristic checks; return dict with has_h1, has_suggested, has_absolute_path, line_count, issues."""
    issues: list[str] = []
    lines = content.splitlines()
    line_count = len(lines)

    has_h1 = any(re.match(r"^#\s+\S", line) for line in lines)
    if not has_h1:
        issues.append("No H1 heading found.")

    has_suggested = "Suggested updates" in content or "auto-generated placeholder" in content
    if not has_suggested:
        issues.append("No 'Suggested updates' placeholder section (candidate may be unchanged copy).")

    if re.search(r"/home/|/Users/|C:\\|D:\\", content):
        issues.append("Possible absolute path in content.")

    if line_count < 5:
        issues.append("Very short file; may be incomplete.")

    return {
        "path": path,
        "line_count": line_count,
        "has_h1": has_h1,
        "has_suggested": has_suggested,
        "issues": issues,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Heuristic review of candidate docs.")
    parser.add_argument("--root", default=".", help="Repository root.")
    parser.add_argument("--candidates-dir", default=None, help="Candidates directory.")
    parser.add_argument("--impact-report", default=None, help="Path to change_impact_report.md (for context).")
    parser.add_argument("--output", default=None, help="Output report path.")
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    candidates_dir = args.candidates_dir or os.path.join(root, "docs", "generated", "candidates")
    output_path = args.output or os.path.join(root, "docs", "generated", "docgen_review_report.md")

    reviews: list[dict] = []
    if os.path.isdir(candidates_dir):
        for fname in sorted(os.listdir(candidates_dir)):
            if not fname.endswith(".md"):
                continue
            full = os.path.join(candidates_dir, fname)
            try:
                with open(full, "r", encoding="utf-8") as f:
                    content = f.read()
            except OSError:
                continue
            rel = os.path.relpath(full, root).replace("\\", "/")
            reviews.append(review_one(content, rel))

    must_fix: list[str] = []
    should_fix: list[str] = []
    nice_to_have: list[str] = []

    for r in reviews:
        for i in r["issues"]:
            if "absolute path" in i:
                must_fix.append(f"`{r['path']}`: {i}")
            elif "No H1" in i or "Very short" in i:
                should_fix.append(f"`{r['path']}`: {i}")
            else:
                nice_to_have.append(f"`{r['path']}`: {i}")

    lines = ["# Docgen Review Report\n"]
    lines.append("*This report is from a **heuristic checklist only**, not full semantic review.*\n")
    lines.append("## Overall assessment\n")
    if not reviews:
        lines.append("No candidate files to review.\n")
    else:
        lines.append(f"Heuristic checklist only; {len(reviews)} candidate(s) reviewed.\n")
    lines.append("## Must fix\n")
    if must_fix:
        for m in must_fix:
            lines.append(f"- {m}")
        lines.append("")
    else:
        lines.append("- (none)\n")
    lines.append("## Should fix\n")
    if should_fix:
        for s in should_fix:
            lines.append(f"- {s}")
        lines.append("")
    else:
        lines.append("- (none)\n")
    lines.append("## Nice to have\n")
    if nice_to_have:
        for n in nice_to_have:
            lines.append(f"- {n}")
        lines.append("")
    else:
        lines.append("- (none)\n")
    lines.append("## Per-file notes\n")
    for r in reviews:
        lines.append(f"- **{r['path']}**: {r['line_count']} lines; H1={r['has_h1']}, Suggested section={r['has_suggested']}. " + ("Issues: " + "; ".join(r["issues"]) if r["issues"] else "OK"))
    lines.append("")

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"Wrote {output_path}")


if __name__ == "__main__":
    main()
