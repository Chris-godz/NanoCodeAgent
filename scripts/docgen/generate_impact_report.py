#!/usr/bin/env python3
"""generate_impact_report.py — Build change_impact_report.md from change_context_output.md.

Purpose:
    Parse the output of changed_context.py and fill the change impact report
    template with heuristic classification. Used by run_change_impact.sh when
    the report does not exist.

Usage:
    python3 scripts/docgen/generate_impact_report.py [--root <repo-root>] [--context <path>] [--output <path>]

Arguments:
    --root    Repository root directory (default: cwd).
    --context Path to change_context_output.md (default: docs/generated/change_context_output.md).
    --output  Output report path (default: docs/generated/change_impact_report.md).

Exit codes:
    0 — success
    1 — missing context file or I/O error
"""

import argparse
import os
import re
import sys
from datetime import datetime, timezone


def parse_context_file(content: str) -> dict:
    """Parse change_context_output.md into structured data."""
    data: dict = {
        "ref": "",
        "changed_count": 0,
        "changed_files": [],
        "modules": [],
        "adjacent_docs": [],
        "related_examples": [],
        "candidate_docs": [],
        "likely_doc_impact": False,
        "reason": "",
    }
    lines = content.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("Diff reference:"):
            m = re.search(r"`([^`]+)`", line)
            if m:
                data["ref"] = m.group(1).strip()
        elif line.startswith("Changed files:"):
            m = re.search(r"(\d+)", line)
            if m:
                data["changed_count"] = int(m.group(1))
        elif line.startswith("## `") and "Candidate docs" not in line and "Impact perspective" not in line:
            m = re.match(r"## `(.+)`", line)
            if m:
                data["changed_files"].append(m.group(1).strip())
            i += 1
            while i < len(lines) and not lines[i].strip().startswith("##"):
                L = lines[i]
                if L.strip().startswith("- **Module**:"):
                    mod = L.split(":", 1)[-1].strip()
                    if mod and mod not in data["modules"]:
                        data["modules"].append(mod)
                elif L.strip().startswith("- **Adjacent docs**:"):
                    i += 1
                    while i < len(lines) and lines[i].strip().startswith("- `"):
                        doc = re.search(r"`([^`]+)`", lines[i])
                        if doc and doc.group(1) not in data["adjacent_docs"]:
                            data["adjacent_docs"].append(doc.group(1))
                        i += 1
                    i -= 1
                elif L.strip().startswith("- **Related examples**:"):
                    i += 1
                    while i < len(lines) and lines[i].strip().startswith("- `"):
                        ex = re.search(r"`([^`]+)`", lines[i])
                        if ex and ex.group(1) not in data["related_examples"]:
                            data["related_examples"].append(ex.group(1))
                        i += 1
                    i -= 1
                i += 1
            continue
        elif line.strip() == "## Candidate docs (README.md, docs/, book/src/)":
            i += 1
            while i < len(lines) and not lines[i].strip().startswith("##"):
                m = re.search(r"`([^`]+)`", lines[i])
                if m and "none found" not in lines[i]:
                    data["candidate_docs"].append(m.group(1))
                i += 1
            continue
        elif "## Impact perspective" in line:
            i += 1
            while i < len(lines):
                L = lines[i]
                if "likely_doc_impact" in L:
                    data["likely_doc_impact"] = "true" in L.lower()
                elif "reason" in L and ":" in L:
                    data["reason"] = L.split(":", 1)[-1].strip()
                i += 1
            break
        i += 1
    return data


def render_report(data: dict, root: str, generated_iso: str) -> str:
    """Produce markdown report from parsed data. All paths repo-relative."""
    ref = data["ref"] or "N/A"
    count = data["changed_count"] or len(data["changed_files"])
    reason = data["reason"] or "unknown"

    lines = ["# Change Impact Report\n"]
    lines.append(f"- **Diff ref**: {ref}")
    lines.append(f"- **Generated**: {generated_iso}\n")
    lines.append("## Change summary")
    lines.append(f"Diff ref {ref}. {count} file(s) changed. Heuristic: {reason}.\n")
    lines.append("## Affected modules")
    for m in data["modules"] or ["(none)"]:
        lines.append(f"- {m}")
    lines.append("")
    lines.append("## Affected docs")
    affected = list(dict.fromkeys(data["adjacent_docs"]))[:20]
    if not affected:
        affected = ["(none identified)"]
    for p in affected:
        lines.append(f"- `{p}`")
    lines.append("")
    lines.append("## Related examples")
    examples = list(dict.fromkeys(data["related_examples"]))[:20]
    if not examples:
        examples = ["(none)"]
    for p in examples:
        lines.append(f"- `{p}`")
    lines.append("")
    lines.append("## Impact classification")
    lines.append("")
    lines.append("### Required updates")
    if data["likely_doc_impact"] and affected and affected[0] != "(none identified)":
        for p in affected[:3]:
            lines.append(f"- `{p}` — heuristic: entrypoint or docs change.")
    else:
        lines.append("- (none)")
    lines.append("")
    lines.append("### Optional updates")
    if data["likely_doc_impact"] and len(affected) > 3:
        for p in affected[3:6]:
            lines.append(f"- `{p}` — may need refresh.")
    else:
        lines.append("- (none)")
    lines.append("")
    lines.append("### No update needed")
    if not data["likely_doc_impact"]:
        lines.append("- Implementation or tests only; no user-facing doc change.")
    else:
        lines.append("- (see required/optional above)")
    lines.append("")
    lines.append("## Rationale")
    lines.append(reason)
    lines.append("")
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate change impact report from context output.")
    parser.add_argument("--root", default=".", help="Repository root directory.")
    parser.add_argument("--context", default=None, help="Path to change_context_output.md.")
    parser.add_argument("--output", default=None, help="Path to write change_impact_report.md.")
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    context_path = args.context or os.path.join(root, "docs", "generated", "change_context_output.md")
    output_path = args.output or os.path.join(root, "docs", "generated", "change_impact_report.md")

    if not os.path.isfile(context_path):
        print(f"ERROR: context file not found: {context_path}", file=sys.stderr)
        sys.exit(1)

    with open(context_path, "r", encoding="utf-8") as f:
        content = f.read()
    data = parse_context_file(content)
    generated_iso = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    report = render_report(data, root, generated_iso)
    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(report)
    print(f"Wrote change impact report to {output_path}")


if __name__ == "__main__":
    main()
